// wlcli — drives the image engine without UI (docs/ENGINE.md §4).
// Every engine capability gets a command here before any page uses it. `--json` output is stable
// and parsed by integration tests and tools.
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/Appx.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/dism/OptionalFeatures.h"
#include "core/iso/IsoBuilder.h"
#include "core/ops/ApplyJob.h"
#include "core/image/wim/WimGapi.h"
#include "core/ops/Applier.h"
#include "core/ops/Planner.h"
#include "core/image/WindowsRelease.h"
#include "core/postsetup/PostSetup.h"
#include "core/system/Privileges.h"
#include "core/unattend/Unattend.h"

#include <json.hpp>

#include <windows.h>

#include <cstdio>
#include <cwctype>
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

// Image index from the command line: whole positive number or 0 (DISM/WIMGAPI then reject it)
// — never an exception (std::stoi threw on "abc" and accepted "2x").
int parseIndex(const std::wstring& text) {
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return !text.empty() && *end == L'\0' && value > 0 && value < 100000 ? static_cast<int>(value) : 0;
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
    if (auto r = (*d)->mount(wim, parseIndex(index), dir, readOnly, task); !r) {
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
    auto checks = wl::core::inspectMounts(**d);
    if (!checks) {
        return reportError(checks.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& c : *checks) {
            json hives = json::array();
            for (const auto& h : c.loadedHives) {
                hives.push_back(narrow(h));
            }
            out.push_back({{"mountPath", narrow(c.folder.wstring())},
                           {"imagePath", narrow(c.record ? c.record->imagePath.wstring() : L"")},
                           {"index", c.record ? c.record->index : 0},
                           {"readOnly", c.record && c.record->readOnly},
                           {"dismStatus", narrow(c.record ? wl::core::dismMountStatusName(c.record->status) : L"")},
                           {"state", narrow(wl::core::mountStateName(c.state))},
                           {"action", narrow(wl::core::mountActionName(c.action))},
                           {"windowsImage", c.windowsImage},
                           {"loadedHives", hives}});
        }
        printJson(out);
        return 0;
    }
    if (checks->empty()) {
        print(L"  no mounted images\n");
    }
    for (const auto& c : *checks) {
        print(std::format(L"  {}  <- {} [{}]\n    state: {}  (dism: {}{})  action: {}\n", c.folder.wstring(),
                          c.record ? c.record->imagePath.wstring() : L"-", c.record ? c.record->index : 0,
                          wl::core::mountStateName(c.state),
                          c.record ? wl::core::dismMountStatusName(c.record->status) : L"-",
                          c.record && c.record->readOnly ? L", read-only" : L"", wl::core::mountActionName(c.action)));
        for (const auto& h : c.loadedHives) {
            print(L"    hive loaded: " + h + L"\n");
        }
    }
    return 0;
}

// Inspect one folder and carry out the recommended action (MountHealth.h).
int cmdRepair(const std::wstring& dir) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto before = wl::core::inspectMount(**d, dir);
    if (!before) {
        return reportError(before.error());
    }
    print(std::format(L"  {}: {} -> {}\n", dir, wl::core::mountStateName(before->state),
                      wl::core::mountActionName(before->action)));
    auto after = wl::core::repairMount(**d, *before, core::TaskContext{g_cancel, {}});
    if (!after) {
        return reportError(after.error());
    }
    print(std::format(L"  now: {}\n", wl::core::mountStateName(after->state)));
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

// P04 data: features + present capabilities with display names and sizes (what the page shows).
int cmdOptionalFeatures(const std::wstring& dir, bool asJson) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    const core::TaskContext task{g_cancel, {}};
    auto list = core::readOptionalFeatures(**d, dir, task);
    if (!list) {
        return reportError(list.error());
    }
    json out = json::array();
    for (const auto& f : *list) {
        const bool feature = f.kind == core::OptionalFeature::Kind::Feature;
        if (asJson) {
            out.push_back({{"kind", feature ? "feature" : "capability"}, {"name", narrow(f.name)},
                           {"displayName", narrow(f.displayName)}, {"state", narrow(core::servicingStateName(f.state))},
                           {"size", f.size}, {"restart", f.restartRequired}});
        } else {
            print(std::format(L"  {:<3} {:<18} {:<48} {}\n", feature ? L"F" : L"C", core::servicingStateName(f.state),
                              f.displayName.substr(0, 48), f.name));
        }
    }
    if (asJson) {
        printJson(out);
    }
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

int cmdApply(const std::wstring& changeSetPath, const std::wstring& mountDir, bool commit, const std::wstring& source) {
    auto set = loadChangeSet(changeSetPath);
    if (!set) {
        return reportError(set.error());
    }
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    // Same job as the app: errors are skipped and reported; --commit saves + unmounts at the end.
    const auto p = core::ops::plan(*set);
    core::ops::ApplyJobOptions options;
    options.commitAndUnmount = commit;
    if (!source.empty()) {
        options.apply.featureSources.push_back(source);
    }
    core::ops::ApplyJobCallbacks callbacks;
    callbacks.steps.stepStarted = [&](std::size_t i, const core::ops::PlanStep& step) {
        print(std::format(L"  [{}/{}] {} {}\n", i + 1, p.steps.size(), utf8::toWide(core::ops::opKindKey(step.operation.kind)),
                          step.operation.target));
    };
    callbacks.steps.stepFinished = [&](std::size_t, const core::ops::StepResult& r) {
        print(r.outcome ? std::wstring(L"        ok\n") : L"        FAILED: " + describe(r.outcome.error()) + L"\n");
    };
    callbacks.committing = [] { print(L"  saving and unmounting...\n"); };
    auto job = core::ops::runApplyJob(**d, mountDir, p, options, core::TaskContext{g_cancel, {}}, callbacks);
    if (!job) {
        return reportError(job.error());
    }
    const auto& report = job->report;
    const std::wstring commitText = job->committed ? std::wstring(L"ok")
                                    : job->commitError ? describe(*job->commitError)
                                                       : std::wstring(L"skipped");
    print(std::format(L"  {} of {} step(s) ran, {} failed{}; commit: {} ({} ms)\n", report.results.size(), p.steps.size(),
                      report.failures(), report.completed ? L"" : L" (stopped)", commitText, job->elapsed.count()));
    return report.completed && report.failures() == 0 && !job->commitError ? 0 : 3;
}

// P07 data: provisioned apps with on-disk size.
int cmdAppx(const std::wstring& dir, bool asJson) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto list = core::readAppx(**d, dir, core::TaskContext{g_cancel, {}});
    if (!list) {
        return reportError(list.error());
    }
    json out = json::array();
    for (const auto& a : *list) {
        if (asJson) {
            out.push_back({{"packageName", narrow(a.package.packageName)}, {"name", narrow(a.package.displayName)},
                           {"version", narrow(a.package.version)}, {"architecture", a.package.architecture},
                           {"size", a.size}});
        } else {
            print(std::format(L"  {:>12}  {}\n", a.size, a.package.packageName));
        }
    }
    if (asJson) {
        printJson(out);
    }
    return 0;
}

// P07 system components: the CBS packages as the image's SOFTWARE hive lists them (hidden ones
// included, which DismGetPackages leaves out). `filter`: part of the identity.
int cmdCbs(const std::wstring& dir, const std::wstring& filter, bool asJson) {
    auto list = core::readCbsPackages(std::filesystem::path(dir));
    if (!list) {
        return reportError(list.error());
    }
    auto lower = [](std::wstring text) {
        for (auto& c : text) {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        return text;
    };
    const std::wstring needle = lower(filter);
    json out = json::array();
    std::size_t shown = 0;
    for (const auto& p : *list) {
        if (!needle.empty() && lower(p.identity).find(needle) == std::wstring::npos) {
            continue;
        }
        ++shown;
        if (asJson) {
            out.push_back({{"identity", narrow(p.identity)}, {"visibility", p.visibility}, {"state", p.state}});
        } else {
            print(std::format(L"  {}  0x{:02X}  {}\n", p.visibility == 1 ? L"visible" : L"hidden ", p.state, p.identity));
        }
    }
    if (asJson) {
        printJson(out);
    } else {
        print(std::format(L"{} of {} package(s)\n", shown, list->size()));
    }
    return 0;
}

// One component recipe (the value of a removeComponent operation: title, packages, paths,
// registry) against a mounted image: what is there, or — with --remove — take it out.
int cmdComponent(const std::wstring& dir, const std::wstring& recipeFile, bool remove) {
    std::ifstream file{std::filesystem::path(recipeFile), std::ios::binary};
    if (!file) {
        return reportError(Error{ErrorCode::NotFound, L"cannot open the recipe", recipeFile});
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto recipe = core::componentRecipeFromJson(buffer.str());
    if (!recipe) {
        return reportError(recipe.error());
    }
    if (auto valid = core::validateComponentRecipe(*recipe); !valid) {
        return reportError(valid.error());
    }
    const auto presence = core::probeComponent(dir, *recipe);
    print(std::format(L"{}: {}, {} bytes in {} path(s)\n", recipe->title, presence.present ? L"present" : L"not found",
                      presence.size, recipe->paths.size()));
    if (!recipe->packages.empty()) {
        auto all = core::readCbsPackages(std::filesystem::path(dir));
        if (!all) {
            return reportError(all.error());
        }
        for (const auto& p : core::cbsRemovalOrder(recipe->packages, *all)) {
            print(std::format(L"  package  {}  0x{:02X}  {}\n", p.visibility == 1 ? L"visible" : L"hidden ", p.state, p.identity));
        }
    }
    if (!remove) {
        return 0;
    }
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto session = (*d)->openSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    const core::TaskContext task{g_cancel, [](double fraction, std::wstring_view) {
                                     print(std::format(L"\r  {:5.1f}%", fraction * 100));
                                 }};
    const auto removed = core::removeComponent(**session, *recipe, task);
    print(L"\n");
    if (!removed) {
        return reportError(removed.error());
    }
    print(L"removed (image not committed: wlcli unmount <dir> --commit)\n");
    return 0;
}

int cmdStoreCleanup(const std::wstring& dir, bool resetBase) {
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    auto session = (*d)->openSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    const core::TaskContext task{g_cancel, [](double fraction, std::wstring_view) {
                                     print(std::format(L"\r  {:5.1f}%", fraction * 100));
                                 }};
    const auto cleaned = core::cleanupComponentStore(**session, resetBase, task);
    print(L"\n");
    if (!cleaned) {
        return reportError(cleaned.error());
    }
    print(L"component store cleaned (image not committed)\n");
    return 0;
}

// P10 data: services of the image's SYSTEM hive; `set` = "Name=start" writes one start type.
int cmdServices(const std::wstring& dir, const std::wstring& set, bool asJson) {
    if (!set.empty()) {
        const auto eq = set.find(L'=');
        const auto start = eq == std::wstring::npos ? std::nullopt : core::startTypeFromKey(set.substr(eq + 1));
        if (!start) {
            print(L"--set=<Name>=auto|autoDelayed|manual|disabled\n");
            return 1;
        }
        if (auto r = core::setServiceStart(dir, set.substr(0, eq), *start); !r) {
            return reportError(r.error());
        }
    }
    auto list = core::readServices(dir);
    if (!list) {
        return reportError(list.error());
    }
    json out = json::array();
    for (const auto& s : *list) {
        if (asJson) {
            out.push_back({{"name", narrow(s.name)}, {"displayName", narrow(s.displayName)},
                           {"start", narrow(core::startTypeKey(s.start))}, {"type", s.type},
                           {"account", narrow(s.account)}, {"imagePath", narrow(s.imagePath)}});
        } else {
            print(std::format(L"  {:<12} {:<28} {}\n", core::startTypeKey(s.start), s.name, s.displayName));
        }
    }
    if (asJson) {
        printJson(out);
    }
    return 0;
}

// P13: read an answer file for the options WinLove knows and print the file it would write.
int cmdUnattend(const std::wstring& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return reportError(Error{ErrorCode::NotFound, L"could not open the answer file", file, 0});
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto options = core::parseUnattendXml(bytes);
    if (!options) {
        return reportError(options.error());
    }
    print(core::buildUnattendXml(*options));
    const auto problems = core::validateUnattend(*options);
    for (const auto problem : problems) {
        static constexpr const wchar_t* kNames[] = {L"computer name", L"account name", L"product key",
                                                   L"automatic logon needs a local account"};
        print(std::format(L"problem: {}\n", kNames[static_cast<std::size_t>(problem)]));
    }
    return problems.empty() ? 0 : 1;
}

// P14: write a post-setup plan (the JSON a preset holds) into a mounted image — or any folder
// standing in for one: scripts, task definition, copy payloads, SetupComplete.cmd line.
int cmdPostSetup(const std::wstring& planFile, const std::wstring& mountDir) {
    std::ifstream in(planFile, std::ios::binary);
    if (!in) {
        return reportError(Error{ErrorCode::NotFound, L"could not open the plan", planFile, 0});
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto plan = core::postSetupFromJson(bytes);
    if (!plan) {
        return reportError(plan.error());
    }
    if (auto r = core::applyPostSetup(mountDir, *plan, core::TaskContext{}); !r) {
        return reportError(r.error());
    }
    print(std::format(L"{} step(s) written under {}\\Windows\\Setup\\Scripts\\WinLove\n", plan->steps.size(), mountDir));
    return 0;
}

// P11: parse a .reg file (no admin) and optionally apply it to a mounted image's hives.
// --first-logon: what the app does with an imported .reg file — also record every value for the
// post-setup import (SetupComplete.cmd / default-user RunOnce, D-026).
int cmdReg(const std::wstring& file, const std::wstring& mountDir, bool firstLogon) {
    auto writes = core::readRegFile(file);
    if (!writes) {
        return reportError(writes.error());
    }
    std::unique_ptr<core::OfflineRegistry> registry;
    std::unique_ptr<core::DeferredRegistry> deferred;
    if (!mountDir.empty()) {
        registry = std::make_unique<core::OfflineRegistry>(mountDir);
        if (firstLogon) {
            deferred = std::make_unique<core::DeferredRegistry>(mountDir);
        }
    }
    int failures = 0;
    for (const auto& w : *writes) {
        const auto mapped = core::mapOfflineKey(w.key);
        const bool later = !mapped && core::isPostSetupOnlyKey(w.key);
        std::wstring status = mapped ? L"" : later ? L"  [after setup only]" : L"  [unsupported]";
        if (registry && (mapped || (later && deferred))) {
            auto r = deferred ? core::deferRegistryWrite(*registry, *deferred, w) : registry->apply(w);
            status = r ? (deferred ? L"  [ok + after setup]" : L"  [ok]") : L"  [" + r.error().message + L"]";
            failures += r ? 0 : 1;
        }
        print(core::registryTarget(w) + L" = " + core::formatRegValue(w) + status + L"\n");
    }
    return failures == 0 ? 0 : 1;
}

// P06: bootable ISO from a setup folder (IMAPI2FS, no admin).
int cmdIso(const std::wstring& folder, const std::wstring& output, const std::wstring& label, const std::wstring& boot,
           bool sha, bool noPrompt) {
    core::IsoOptions options;
    options.sourceFolder = folder;
    options.output = output;
    options.volumeLabel = label;
    options.boot = boot == L"uefi" ? core::BootMode::UefiOnly
                   : boot == L"bios" ? core::BootMode::BiosOnly
                                     : core::BootMode::UefiAndBios;
    options.writeSha256 = sha;
    options.noPrompt = noPrompt;
    const auto task = progressTask(L"iso");
    auto result = core::buildIso(options, task);
    print(L"\n");
    if (!result) {
        return reportError(result.error());
    }
    print(std::format(L"  {} ({} bytes){}\n", output, result->bytes,
                      result->sha256.empty() ? std::wstring() : L"\n  sha256 " + result->sha256));
    return 0;
}

int cmdExport(const std::wstring& source, const std::wstring& index, const std::wstring& destination,
              const std::wstring& compression) {
    core::WimCompression c = core::WimCompression::Lzx;
    if (compression == L"none") c = core::WimCompression::None;
    else if (compression == L"fast" || compression == L"xpress") c = core::WimCompression::Xpress;
    else if (compression == L"max" || compression == L"lzx" || compression.empty()) c = core::WimCompression::Lzx;
    else if (compression == L"recovery" || compression == L"lzms") c = core::WimCompression::Lzms;
    else return reportError(Error{ErrorCode::InvalidArgument, L"--compress must be none|fast|max|recovery", compression});
    const auto task = progressTask(L"export");
    if (auto r = core::exportImage(source, parseIndex(index), destination, c, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    print(std::format(L"\n  exported index {} -> {}\n", index, destination));
    return 0;
}

int cmdDeleteIndex(const std::wstring& wim, const std::wstring& index) {
    if (auto r = core::deleteImage(wim, parseIndex(index)); !r) {
        return reportError(r.error());
    }
    print(std::format(L"  deleted index {} from {}\n", index, wim));
    return 0;
}

int cmdExtractAll(const std::wstring& iso, const std::wstring& destination) {
    auto image = core::UdfImage::open(iso);
    if (!image) {
        return reportError(image.error());
    }
    const auto task = progressTask(L"extract");
    if (auto r = image->extractAll(destination, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    print(std::format(L"\n  extracted {} -> {}\n", iso, destination));
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
          L"\n  Needs an elevated shell (DISM):\n"
          L"  wlcli mount <wim> <index> <dir> [--readonly]\n"
          L"  wlcli unmount <dir> --commit|--discard\n"
          L"  wlcli mounts | cleanup\n"
          L"  wlcli packages|features|capabilities <mountdir>\n"
          L"  wlcli iso <setup-folder> <out.iso> [--label=X] [--boot=both|uefi|bios] [--sha256] [--no-prompt]\n"
          L"  wlcli unattend <answer.xml>         (read an answer file; print it as WinLove writes it, P13)\n"
          L"  wlcli postsetup <plan.json> <mountdir>   (write post-setup scripts and payloads into the image, P14)\n"
          L"  wlcli reg <file.reg> [<mountdir>] [--first-logon]   (parse; with a mount: write into the image's\n"
          L"                                      hives, P11; --first-logon: also re-import after setup)\n"
          L"  wlcli services <mountdir> [--set=Name=auto|autoDelayed|manual|disabled]   (P10)\n"
          L"  wlcli appx <mountdir>   (provisioned apps + size, as on P07)\n"
          L"  wlcli cbs <mountdir> [text]   (CBS packages from the image's registry, hidden ones too)\n"
          L"  wlcli component <mountdir> <recipe.json> [--remove]   (P07 system component: probe / remove)\n"
          L"  wlcli store-cleanup <mountdir> [--resetbase]   (dism /Cleanup-Image /StartComponentCleanup)\n"
          L"  wlcli optional-features <mountdir>   (features + capabilities with names, as on P04)\n"
          L"  wlcli apply <changeset.json> <mountdir> [--commit] [--source=<sources\\sxs>]\n"
          L"\n  Change sets (no admin):\n"
          L"  wlcli plan <changeset.json>              Show the ordered apply plan\n"
          L"  wlcli extract-all <iso> <dir>             Copy the whole ISO into a folder (resumable)\n"
          L"  wlcli export <wim|esd> <index> <dst.wim> [--compress=max|fast|none|recovery]\n"
          L"  wlcli delete-index <wim> <index>\n"
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
    std::wstring compress;
    std::wstring source;
    std::wstring label;
    std::wstring serviceSet;
    std::wstring boot;
    bool sha = false;
    bool noPrompt = false;
    bool firstLogon = false;
    bool remove = false;
    bool resetBase = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        if (a == L"--json") {
            asJson = true;
        } else if (a.starts_with(L"--compress=")) {
            compress = std::wstring(a.substr(11));
        } else if (a.starts_with(L"--set=")) {
            serviceSet = std::wstring(a.substr(6));
        } else if (a.starts_with(L"--label=")) {
            label = std::wstring(a.substr(8));
        } else if (a.starts_with(L"--boot=")) {
            boot = std::wstring(a.substr(7));
        } else if (a == L"--sha256") {
            sha = true;
        } else if (a == L"--no-prompt") {
            noPrompt = true;
        } else if (a == L"--first-logon") {
            firstLogon = true;
        } else if (a == L"--remove") {
            remove = true;
        } else if (a == L"--resetbase") {
            resetBase = true;
        } else if (a.starts_with(L"--source=")) {
            source = std::wstring(a.substr(9));
        } else if (a == L"--skip-errors") {
            // accepted for old scripts: apply always skips failed steps and reports them
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
    if (command == L"export" && args.size() == 4) {
        return cmdExport(args[1], args[2], args[3], compress);
    }
    if (command == L"delete-index" && args.size() == 3) {
        return cmdDeleteIndex(args[1], args[2]);
    }
    if (command == L"extract-all" && args.size() == 3) {
        return cmdExtractAll(args[1], args[2]);
    }
    if (command == L"plan" && args.size() == 2) {
        return cmdPlan(args[1]);
    }
    if (command == L"apply" && args.size() == 3) {
        return cmdApply(args[1], args[2], commit == 1, source);
    }
    if (command == L"mounts") {
        return cmdMounts(asJson);
    }
    if (command == L"cleanup") {
        return cmdCleanup();
    }
    if (command == L"repair" && args.size() == 2) {
        return cmdRepair(args[1]);
    }
    if (command == L"cbs" && (args.size() == 2 || args.size() == 3)) {
        return cmdCbs(args[1], args.size() == 3 ? args[2] : std::wstring(), asJson);
    }
    if (command == L"component" && args.size() == 3) {
        return cmdComponent(args[1], args[2], remove);
    }
    if (command == L"store-cleanup" && args.size() == 2) {
        return cmdStoreCleanup(args[1], resetBase);
    }
    if (command == L"appx" && args.size() == 2) {
        return cmdAppx(args[1], asJson);
    }
    if (command == L"iso" && args.size() == 3) {
        return cmdIso(args[1], args[2], label, boot, sha, noPrompt);
    }
    if (command == L"postsetup" && args.size() == 3) {
        return cmdPostSetup(args[1], args[2]);
    }
    if (command == L"unattend" && args.size() == 2) {
        return cmdUnattend(args[1]);
    }
    if (command == L"reg" && (args.size() == 2 || args.size() == 3)) {
        return cmdReg(args[1], args.size() == 3 ? args[2] : std::wstring(), firstLogon);
    }
    if (command == L"services" && args.size() == 2) {
        return cmdServices(args[1], serviceSet, asJson);
    }
    if (command == L"optional-features" && args.size() == 2) {
        return cmdOptionalFeatures(args[1], asJson);
    }
    if ((command == L"packages" || command == L"features" || command == L"capabilities") && args.size() == 2) {
        return cmdServicing(command, args[1], asJson);
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
