// wlcli — drives the image engine without UI (docs/ENGINE.md §4).
// Every engine capability gets a command here before any page uses it. `--json` output is stable
// and parsed by integration tests and tools.
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/image/dism/Dism.h"
#include "core/ops/Applier.h"
#include "core/ops/Planner.h"
#include "core/image/WindowsRelease.h"
#include "core/system/LiveSystem.h"
#include "core/system/Privileges.h"

#include <json.hpp>

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace wl;
using nlohmann::json;

core::CancelToken g_cancel;

void print(std::wstring_view text) {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        // Redirected (pipe/file): emit UTF-8 so tests and tools can parse it.
        const std::string utf8 = utf8::fromWide(text);
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
        std::fflush(stdout);
    }
}

void printJson(const json& value) {
    const std::string text = value.dump(2) + "\n";
    print(utf8::toWide(text));
}

int reportError(const Error& error) {
    print(L"error: " + describe(error) + L"\n");
    return error.code == ErrorCode::Cancelled ? 130 : 2;
}

BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_cancel.cancel(); // long operations stop at the next chunk and clean up
        return TRUE;
    }
    return FALSE;
}

std::string narrow(std::wstring_view text) {
    return utf8::fromWide(text);
}

json imageJson(const core::ImageInfo& image) {
    json languages = json::array();
    for (const auto& l : image.languages) {
        languages.push_back(narrow(l));
    }
    return {{"index", image.index},
            {"name", narrow(image.name)},
            {"displayName", narrow(image.displayName)},
            {"description", narrow(image.description)},
            {"editionId", narrow(image.editionId)},
            {"installationType", narrow(image.installationType)},
            {"architecture", narrow(core::architectureName(image.architecture))},
            {"version", narrow(image.versionString())},
            {"build", image.build},
            {"languages", languages},
            {"defaultLanguage", narrow(image.defaultLanguage)},
            {"totalBytes", image.totalBytes},
            {"fileCount", image.fileCount},
            {"directoryCount", image.directoryCount}};
}

json wimJson(const core::WimFile& wim) {
    json images = json::array();
    for (const auto& image : wim.images) {
        images.push_back(imageJson(image));
    }
    return {{"imageCount", wim.header.imageCount},
            {"compression", narrow(core::compressionName(wim.header.compression))},
            {"solid", wim.header.solid},
            {"bootIndex", wim.header.bootIndex},
            {"parts", wim.header.totalParts},
            {"images", images}};
}

std::wstring gib(std::uint64_t bytes) {
    return std::format(L"{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

int cmdInfo(const std::wstring& path, bool asJson) {
    auto source = core::openSource(path);
    if (!source) {
        return reportError(source.error());
    }
    if (asJson) {
        json out{{"path", narrow(source->path.wstring())},
                 {"format", narrow(core::formatName(source->format))},
                 {"volumeLabel", narrow(source->volumeLabel)},
                 {"installImage", narrow(source->installImage)},
                 {"installImageSize", source->installImageSize},
                 {"install", wimJson(source->install)}};
        if (source->boot) {
            out["boot"] = wimJson(*source->boot);
        }
        printJson(out);
        return 0;
    }
    const auto& h = source->install.header;
    print(std::format(L"{}  [{}{}{}]\n", source->path.wstring(), core::formatName(source->format),
                      source->volumeLabel.empty() ? L"" : L", label ", source->volumeLabel));
    print(std::format(L"  {}  {}  compression {}{}  {} image(s)\n\n", source->installImage, gib(source->installImageSize),
                      core::compressionName(h.compression), h.solid ? L" (solid)" : L"", h.imageCount));
    for (const auto& image : source->install.images) {
        print(std::format(L"  {:>2}  {:<42} {:<6} {:<26} {:<6} {}\n", image.index, image.name,
                          core::architectureName(image.architecture),
                          core::releaseSummary(image.build, image.spBuild, core::architectureName(image.architecture)),
                          image.defaultLanguage, gib(image.totalBytes)));
    }
    return 0;
}

int cmdList(const std::wstring& iso, const std::wstring& directory, bool asJson) {
    auto image = core::UdfImage::open(iso);
    if (!image) {
        return reportError(image.error());
    }
    auto entries = image->list(directory);
    if (!entries) {
        return reportError(entries.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& e : *entries) {
            out.push_back({{"name", narrow(e.name)}, {"directory", e.directory}, {"size", e.size}});
        }
        printJson(out);
        return 0;
    }
    for (const auto& e : *entries) {
        print(std::format(L"{:>14}  {}{}\n", e.directory ? std::wstring(L"<DIR>") : std::format(L"{}", e.size), e.name,
                          e.directory ? L"\\" : L""));
    }
    return 0;
}

int cmdExtract(const std::wstring& iso, const std::wstring& inner, const std::wstring& destination) {
    auto image = core::UdfImage::open(iso);
    if (!image) {
        return reportError(image.error());
    }
    auto node = image->find(inner);
    if (!node) {
        return reportError(node.error());
    }
    int lastPercent = -1;
    const core::TaskContext task{g_cancel, [&](double fraction, std::wstring_view) {
                                     const int percent = static_cast<int>(fraction * 100);
                                     if (percent != lastPercent) {
                                         lastPercent = percent;
                                         print(std::format(L"\r  {:>3}%  {}", percent, gib(static_cast<std::uint64_t>(fraction * static_cast<double>(node->size)))));
                                     }
                                 }};
    if (auto done = image->extract(*node, destination, task); !done) {
        print(L"\n");
        return reportError(done.error());
    }
    print(std::format(L"\n  extracted {} -> {}\n", inner, destination));
    return 0;
}

core::TaskContext progressTask(const wchar_t* label) {
    auto last = std::make_shared<int>(-1);
    return core::TaskContext{g_cancel, [label, last](double fraction, std::wstring_view) {
                                 const int percent = static_cast<int>(fraction * 100);
                                 if (percent != *last) {
                                     *last = percent;
                                     print(std::format(L"\r  {} {:>3}%", label, percent));
                                 }
                             }};
}

Result<core::Dism*> dism() {
    return core::Dism::instance();
}

int cmdMount(const std::wstring& wim, const std::wstring& index, const std::wstring& dir, bool readOnly) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    const auto task = progressTask(L"mount");
    if (auto r = (*d)->mount(wim, std::stoi(index), dir, readOnly, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    print(std::format(L"\n  mounted index {} at {}\n", index, dir));
    return 0;
}

int cmdUnmount(const std::wstring& dir, bool commit) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    const auto task = progressTask(commit ? L"commit" : L"discard");
    if (auto r = (*d)->unmount(dir, commit, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    print(std::format(L"\n  unmounted {} ({})\n", dir, commit ? L"committed" : L"discarded"));
    return 0;
}

int cmdMounts(bool asJson) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto mounts = (*d)->mounts();
    if (!mounts) {
        return reportError(mounts.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& m : *mounts) {
            out.push_back({{"mountPath", narrow(m.mountPath.wstring())}, {"imagePath", narrow(m.imagePath.wstring())},
                           {"index", m.index}, {"readOnly", m.readOnly}, {"healthy", m.healthy},
                           {"status", narrow(m.status)}});
        }
        printJson(out);
        return 0;
    }
    if (mounts->empty()) {
        print(L"  no mounted images\n");
    }
    for (const auto& m : *mounts) {
        print(std::format(L"  {}  <- {} [{}]  {}{}\n", m.mountPath.wstring(), m.imagePath.wstring(), m.index,
                          m.status, m.readOnly ? L", read-only" : L""));
    }
    return 0;
}

int cmdCleanup() {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    if (auto r = (*d)->cleanupMountpoints(); !r) {
        return reportError(r.error());
    }
    print(L"  mount points cleaned up\n");
    return 0;
}

int cmdServicing(const std::wstring& what, const std::wstring& dir, bool asJson) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto session = (*d)->openSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    json out = json::array();
    auto emit = [&](const std::wstring& name, core::ServicingState state) {
        if (asJson) {
            out.push_back({{"name", narrow(name)}, {"state", narrow(core::servicingStateName(state))}});
        } else {
            print(std::format(L"  {:<18} {}\n", core::servicingStateName(state), name));
        }
    };
    if (what == L"packages") {
        auto list = (*session)->packages();
        if (!list) return reportError(list.error());
        for (const auto& p : *list) emit(p.name, p.state);
    } else if (what == L"features") {
        auto list = (*session)->features();
        if (!list) return reportError(list.error());
        for (const auto& f : *list) emit(f.name, f.state);
    } else {
        auto list = (*session)->capabilities();
        if (!list) return reportError(list.error());
        for (const auto& c : *list) emit(c.name, c.state);
    }
    if (asJson) {
        printJson(out);
    }
    return 0;
}

Result<core::ops::ChangeSet> loadChangeSet(const std::wstring& path) {
    std::ifstream file{std::filesystem::path(path), std::ios::binary};
    if (!file) {
        return fail(ErrorCode::NotFound, L"cannot read change set", path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return core::ops::ChangeSet::fromJson(buffer.str());
}

const wchar_t* phaseName(core::ops::Phase phase) {
    switch (phase) {
    case core::ops::Phase::Remove: return L"remove";
    case core::ops::Phase::Features: return L"features";
    case core::ops::Phase::Drivers: return L"drivers";
    case core::ops::Phase::Updates: return L"updates";
    case core::ops::Phase::Settings: return L"settings";
    }
    return L"?";
}

int cmdPlan(const std::wstring& changeSetPath) {
    auto set = loadChangeSet(changeSetPath);
    if (!set) {
        return reportError(set.error());
    }
    const auto p = core::ops::plan(*set);
    for (std::size_t i = 0; i < p.steps.size(); ++i) {
        const auto& op = p.steps[i].operation;
        print(std::format(L"  {:>3}. [{:<8}] {:<18} {}{}\n", i + 1, phaseName(p.steps[i].phase),
                          utf8::toWide(core::ops::opKindKey(op.kind)), op.target,
                          op.value.empty() ? L"" : L" = " + op.value));
    }
    for (const auto& w : p.warnings) {
        print(L"  warning: " + w + L"\n");
    }
    return 0;
}

int cmdApply(const std::wstring& changeSetPath, const std::wstring& mountDir, bool skipErrors) {
    auto set = loadChangeSet(changeSetPath);
    if (!set) {
        return reportError(set.error());
    }
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto session = (*d)->openSession(mountDir);
    if (!session) {
        return reportError(session.error());
    }
    const auto p = core::ops::plan(*set);
    core::ops::ApplyCallbacks callbacks;
    callbacks.stepStarted = [&](std::size_t i, const core::ops::PlanStep& step) {
        print(std::format(L"  [{}/{}] {} {}\n", i + 1, p.steps.size(), utf8::toWide(core::ops::opKindKey(step.operation.kind)),
                          step.operation.target));
    };
    callbacks.stepFinished = [&](std::size_t, const core::ops::StepResult& r) {
        print(r.outcome ? std::wstring(L"        ok\n") : L"        FAILED: " + describe(r.outcome.error()) + L"\n");
    };
    const auto report = core::ops::apply(p, **session, core::TaskContext{g_cancel, {}},
                                         skipErrors ? core::ops::ErrorPolicy::Skip : core::ops::ErrorPolicy::Stop,
                                         callbacks);
    print(std::format(L"  {} of {} step(s) ran, {} failed{}\n", report.results.size(), p.steps.size(), report.failures(),
                      report.completed ? L"" : L" — stopped"));
    return report.completed && report.failures() == 0 ? 0 : 3;
}

int cmdLive(bool asJson) {
    const auto live = core::readLiveSystem();
    if (asJson) {
        printJson({{"productName", narrow(live.productName)}, {"displayVersion", narrow(live.displayVersion)},
                   {"build", live.build}, {"ubr", live.ubr},
                   {"architecture", narrow(core::architectureName(live.architecture))},
                   {"systemDrive", narrow(live.systemDrive)}, {"driveFree", live.driveFree},
                   {"driveTotal", live.driveTotal}, {"elevated", core::isElevated()}});
        return 0;
    }
    print(std::format(L"  {} {} · {}.{} · {}\n  {} {} free / {}\n  elevated: {}\n", live.productName,
                      live.displayVersion, live.build, live.ubr, core::architectureName(live.architecture),
                      live.systemDrive, gib(live.driveFree), gib(live.driveTotal), core::isElevated() ? L"yes" : L"no"));
    return 0;
}

void printUsage() {
    print(L"wlcli " WL_VERSION_STRING L" - WinLove image engine CLI\n"
          L"\n"
          L"Usage:\n"
          L"  wlcli info <iso|wim|esd|swm> [--json]     List editions (reads ISOs in place, no admin)\n"
          L"  wlcli ls <iso> [dir] [--json]             List a directory inside an ISO\n"
          L"  wlcli extract <iso> <path-in-iso> <dest>  Copy a file out of an ISO (Ctrl+C cancels)\n"
          L"  wlcli elevated                            Print whether this process is elevated\n"
          L"  wlcli live [--json]                       The running Windows (Source page card)\n"
          L"\n  Needs an elevated shell (DISM):\n"
          L"  wlcli mount <wim> <index> <dir> [--readonly]\n"
          L"  wlcli unmount <dir> --commit|--discard\n"
          L"  wlcli mounts | cleanup\n"
          L"  wlcli packages|features|capabilities <mountdir>\n"
          L"  wlcli apply <changeset.json> <mountdir> [--skip-errors]\n"
          L"\n  Change sets (no admin):\n"
          L"  wlcli plan <changeset.json>              Show the ordered apply plan\n"
          L"  wlcli version | help\n"
          L"\n"
          L"Options: --json (machine-readable), --verbose (log to stdout)\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    std::vector<std::wstring> args;
    bool asJson = false;
    bool readOnly = false;
    int commit = -1;
    bool skipErrors = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        if (a == L"--json") {
            asJson = true;
        } else if (a == L"--skip-errors") {
            skipErrors = true;
        } else if (a == L"--readonly") {
            readOnly = true;
        } else if (a == L"--commit") {
            commit = 1;
        } else if (a == L"--discard") {
            commit = 0;
        } else if (a == L"--verbose") {
            log::addSink(log::makeStdoutSink());
        } else {
            args.emplace_back(a);
        }
    }
    if (args.empty()) {
        printUsage();
        return 1;
    }
    const std::wstring& command = args[0];
    if (command == L"version") {
        print(L"" WL_VERSION_STRING L"\n");
        return 0;
    }
    if (command == L"info" && args.size() == 2) {
        return cmdInfo(args[1], asJson);
    }
    if (command == L"ls" && (args.size() == 2 || args.size() == 3)) {
        return cmdList(args[1], args.size() == 3 ? args[2] : L"", asJson);
    }
    if (command == L"extract" && args.size() == 4) {
        return cmdExtract(args[1], args[2], args[3]);
    }
    if (command == L"mount" && args.size() == 4) {
        return cmdMount(args[1], args[2], args[3], readOnly);
    }
    if (command == L"unmount" && args.size() == 2 && commit >= 0) {
        return cmdUnmount(args[1], commit == 1);
    }
    if (command == L"plan" && args.size() == 2) {
        return cmdPlan(args[1]);
    }
    if (command == L"apply" && args.size() == 3) {
        return cmdApply(args[1], args[2], skipErrors);
    }
    if (command == L"mounts") {
        return cmdMounts(asJson);
    }
    if (command == L"cleanup") {
        return cmdCleanup();
    }
    if ((command == L"packages" || command == L"features" || command == L"capabilities") && args.size() == 2) {
        return cmdServicing(command, args[1], asJson);
    }
    if (command == L"live") {
        return cmdLive(asJson);
    }
    if (command == L"elevated") {
        print(core::isElevated() ? L"yes\n" : L"no\n");
        return 0;
    }
    if (command == L"help" || command == L"--help" || command == L"-h") {
        printUsage();
        return 0;
    }
    print(L"unknown command or wrong arguments: " + command + L"\n\n");
    printUsage();
    return 1;
}
