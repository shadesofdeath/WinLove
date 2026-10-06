// wlcli — drives the image engine without UI (docs/ENGINE.md §4).
// Every engine capability gets a command here before any page uses it. `--json` output is stable
// and parsed by integration tests and tools.
#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/ComponentStore.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/RegistryEdit.h"
#include "core/image/RegistryRead.h"
#include "core/updates/UpdateCatalog.h"
#include "core/updates/UupLanguages.h"
#include "core/store/MsStore.h"
#include "core/usb/UsbMedia.h"
#include "core/image/AppxInstall.h"
#include "core/image/LanguagePacks.h"
#include "core/image/dism/DefaultApps.h"
#include "core/image/dism/Intl.h"
#include "core/image/Fonts.h"
#include "core/system/Files.h"
#include "core/system/Hash.h"
#include "core/postsetup/Wifi.h"
#include "core/system/Picture.h"
#include "core/system/HostExport.h"
#include "core/image/Services.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/image/BootImage.h"
#include "core/image/icons/IconPatch.h"
#include "core/image/icons/ResFile.h"
#include "core/image/StartMenu.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/Edition.h"
#include "core/image/dism/Appx.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/dism/OptionalFeatures.h"
#include "core/iso/IsoBuilder.h"
#include "core/ops/ApplyJob.h"
#include "core/image/wim/WimGapi.h"
#include "core/image/wim/WimVerify.h"
#include "core/ops/Applier.h"
#include "core/ops/Planner.h"
#include "core/image/WindowsRelease.h"
#include "core/postsetup/PostSetup.h"
#include "core/system/Privileges.h"
#include "core/unattend/Unattend.h"

#include <json.hpp>

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cwctype>
#include <sstream>
#include <format>
#include <optional>
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

// ["a","b"] from wide strings (UTF-8).
json stringArray(const std::vector<std::wstring>& items) {
    json array = json::array();
    for (const auto& item : items) {
        array.push_back(narrow(item));
    }
    return array;
}

json imageJson(const core::ImageInfo& image) {
    json languages = stringArray(image.languages);
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
                                 if (fraction < 0) {
                                     return; // indeterminate (verifying, …)
                                 }
                                 const int percent = static_cast<int>(fraction * 100);
                                 if (percent != *last) {
                                     *last = percent;
                                     print(std::format(L"\r  {} {:>3}%", label, percent));
                                 }
                             }};
}

// "2,3,5" → {2, 3, 5}; empty or non-positive parts are left out.
std::vector<int> parseIndexList(std::wstring_view text) {
    std::vector<int> indexes;
    while (!text.empty()) {
        const auto comma = text.find(L',');
        if (const int i = parseIndex(std::wstring(text.substr(0, comma))); i > 0) {
            indexes.push_back(i);
        }
        text = comma == std::wstring_view::npos ? std::wstring_view{} : text.substr(comma + 1);
    }
    return indexes;
}

// --compress= of export / recompress / append / capture: one vocabulary for all of them.
// Empty: `fallback`, or an error when the command needs a value (no fallback).
Result<core::WimCompression> parseCompression(const std::wstring& text, std::optional<core::WimCompression> fallback) {
    if (text.empty()) {
        if (fallback) {
            return *fallback;
        }
        return fail(ErrorCode::InvalidArgument, L"--compress is required: lzx|xpress|none|esd");
    }
    if (text == L"max" || text == L"lzx") {
        return core::WimCompression::Lzx;
    }
    if (text == L"fast" || text == L"xpress") {
        return core::WimCompression::Xpress;
    }
    if (text == L"none") {
        return core::WimCompression::None;
    }
    if (text == L"recovery" || text == L"lzms" || text == L"esd") {
        return core::WimCompression::Lzms;
    }
    return fail(ErrorCode::InvalidArgument, L"--compress takes lzx (max), xpress (fast), none, esd (recovery / lzms)", text);
}

Result<core::Dism*> dism() {
    return core::Dism::instance();
}

// DISM loaded and a session on the mounted image in `dir` (the start of most commands).
Result<std::unique_ptr<core::DismSession>> openImageSession(const std::wstring& dir) {
    auto d = dism();
    if (!d) {
        return std::unexpected(d.error());
    }
    return (*d)->openSession(dir);
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
            json hives = stringArray(c.loadedHives);
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
    auto session = openImageSession(dir);
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
    const auto bytes = readFileBytes(path);
    if (!bytes) {
        return fail(ErrorCode::NotFound, L"cannot read change set", path);
    }
    return core::ops::ChangeSet::fromJson(*bytes);
}

const wchar_t* phaseName(core::ops::Phase phase) {
    switch (phase) {
    case core::ops::Phase::Edition: return L"edition";
    case core::ops::Phase::Remove: return L"remove";
    case core::ops::Phase::Features: return L"features";
    case core::ops::Phase::Drivers: return L"drivers";
    case core::ops::Phase::Updates: return L"updates";
    case core::ops::Phase::Apps: return L"apps";
    case core::ops::Phase::DeepRemove: return L"deep-remove";
    case core::ops::Phase::Cleanup: return L"cleanup";
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

int cmdApply(const std::wstring& changeSetPath, const std::wstring& mountDir, bool commit, const std::wstring& source,
             const std::wstring& also, const std::wstring& wim, const std::wstring& setupFolder) {
    if (!also.empty() && (!commit || wim.empty())) {
        // Silently skipped before: the further editions are applied after a commit, from that WIM.
        return reportError(Error{ErrorCode::InvalidArgument, L"--also needs --commit and --wim=<file>", also});
    }
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
    options.setupFolder = setupFolder; // D-062: <folder>\sources\lang.ini follows the languages
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
    if (job->langIniWritten) {
        print(L"  Setup's language list (sources\\lang.ini) written from the image\n");
    }
    int result = report.completed && report.failures() == 0 && !job->commitError ? 0 : 3;
    // D-055: the same plan on further editions of `wim` (--also=2,3 --wim=<file>), after a commit.
    if (!also.empty() && job->committed && !wim.empty()) {
        for (const int index : parseIndexList(also)) {
            print(std::format(L"\n  edition {} of {}\n", index, wim));
            auto other = core::ops::applyToEdition(**d, wim, index, mountDir, p, options, core::TaskContext{g_cancel, {}}, callbacks);
            if (!other) {
                reportError(other.error());
                result = 3;
                continue;
            }
            print(std::format(L"  edition {}: {} failed; commit: {}\n", index, other->report.failures(),
                              other->committed ? L"ok" : L"no"));
            if (!other->committed || other->report.failures() > 0) {
                result = 3;
            }
        }
        if (auto rewritten = core::optimizeWim(wim, core::TaskContext{g_cancel, {}}); !rewritten) {
            reportError(rewritten.error());
        } else {
            print(L"  WIM rewritten without the commits' leftovers\n");
        }
    }
    return result;
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
    const auto bytes = readFileBytes(recipeFile);
    if (!bytes) {
        return reportError(Error{ErrorCode::NotFound, L"cannot open the recipe", recipeFile});
    }
    auto recipe = core::componentRecipeFromJson(*bytes);
    if (!recipe) {
        return reportError(recipe.error());
    }
    if (auto valid = core::validateComponentRecipe(*recipe); !valid) {
        return reportError(valid.error());
    }
    // Packages: the component store says whether they are there and what only they own (D-059).
    std::optional<core::ComponentStoreIndex> store;
    if (!recipe->packages.empty() || !recipe->driverClasses.empty()) {
        const auto started = std::chrono::steady_clock::now();
        auto built = core::ComponentStoreIndex::build(dir, core::TaskContext{g_cancel, {}});
        if (!built) {
            return reportError(built.error());
        }
        store = std::move(*built);
        print(std::format(L"  component store read in {} ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                    std::chrono::steady_clock::now() - started).count()));
    }
    const auto presence = core::probeComponent(dir, *recipe, store ? &*store : nullptr);
    print(std::format(L"{}: {}, {} bytes ({} path(s), {} package famil(ies))\n", recipe->title,
                      presence.present ? L"present" : L"not found", presence.size, recipe->paths.size(), recipe->packages.size()));
    if (!recipe->packages.empty()) {
        auto all = core::readCbsPackages(std::filesystem::path(dir));
        if (!all) {
            return reportError(all.error());
        }
        for (const auto& p : core::cbsRemovalOrder(recipe->packages, *all)) {
            print(std::format(L"  package  {}  0x{:02X}  {}\n", p.visibility == 1 ? L"visible" : L"hidden ", p.state, p.identity));
        }
    }
    if (store && !recipe->driverClasses.empty()) {
        for (const auto& d : store->drivers(recipe->driverClasses)) {
            print(std::format(L"  driver   {}  {}  {}\n", d.classGuid, d.inf, d.package));
        }
    }
    if (!remove) {
        return 0;
    }
    auto session = openImageSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    const auto task = progressTask(L"remove");
    const auto removed = core::removeComponent(**session, *recipe, task);
    print(L"\n");
    if (!removed) {
        return reportError(removed.error());
    }
    print(L"removed (image not committed: wlcli unmount <dir> --commit)\n");
    return 0;
}

// Rewrites a WIM without the streams a commit left unreferenced ("[DELETED]" in 7-Zip).
int cmdOptimize(const std::wstring& wim) {
    std::error_code ec;
    const auto before = std::filesystem::file_size(wim, ec);
    const auto task = progressTask(L"optimize");
    const auto done = core::optimizeWim(wim, task);
    print(L"\n");
    if (!done) {
        return reportError(done.error());
    }
    const auto after = std::filesystem::file_size(wim, ec);
    print(std::format(L"rewritten: {} -> {} bytes\n", before, after));
    return 0;
}

// Reads every stream and checks its SHA-1 (what the Images page's "Dogrula" does). Exit code 3:
// the image is damaged.
int cmdVerify(const std::wstring& path) {
    auto source = core::openSource(path);
    if (!source) {
        return reportError(source.error());
    }
    auto bytes = core::openInstallImage(*source);
    if (!bytes) {
        return reportError(bytes.error());
    }
    const auto task = progressTask(L"verify");
    const auto started = GetTickCount64();
    const auto report = core::verifyWim(**bytes, task);
    print(L"\n");
    if (!report) {
        return reportError(report.error());
    }
    print(std::format(L"  {} streams, {} bytes in {:.1f} s: {}\n", report->streams, report->bytes,
                      static_cast<double>(GetTickCount64() - started) / 1000.0,
                      report->sound() ? std::wstring(L"sound") : std::format(L"{} DAMAGED", report->damaged)));
    for (const auto& damage : report->first) {
        print(std::format(L"  offset {} ({} bytes{}): {}\n", damage.offset, damage.size,
                          damage.metadata ? L", metadata" : L"", damage.reason));
    }
    return report->sound() ? 0 : 3;
}

// Whether a file is in an edition, from its file list (no mount). Exit 0: there, 1: not there.
int cmdWimFile(const std::wstring& path, const std::wstring& index, const std::wstring& file) {
    auto source = core::openSource(path);
    if (!source) {
        return reportError(source.error());
    }
    auto bytes = core::openInstallImage(*source);
    if (!bytes) {
        return reportError(bytes.error());
    }
    const auto started = GetTickCount64();
    const auto found = core::wimFileExists(**bytes, _wtoi(index.c_str()), file);
    if (!found) {
        return reportError(found.error());
    }
    print(std::format(L"  [{}] {}: {} ({:.1f} s)\n", index, file, *found ? L"present" : L"MISSING",
                      static_cast<double>(GetTickCount64() - started) / 1000.0));
    return *found ? 0 : 1;
}

// D-058: Images page tools (no admin except capture).

int cmdHash(const std::wstring& file, const std::wstring& expect) {
    const auto started = GetTickCount64();
    auto hash = core::sha256File(file, progressTask(L"sha256"));
    print(L"\n");
    if (!hash) {
        return reportError(hash.error());
    }
    print(std::format(L"  SHA-256 {}  ({:.1f} s)\n", *hash, static_cast<double>(GetTickCount64() - started) / 1000.0));
    if (!expect.empty()) {
        const std::wstring want = core::normalizeSha256(expect);
        print(want.empty() ? L"  expected: not a SHA-256\n" : want == *hash ? L"  matches\n" : L"  DOES NOT MATCH\n");
        return want == *hash ? 0 : 3;
    }
    return 0;
}

int cmdRecompress(const std::wstring& wim, const std::wstring& compress) {
    const auto target = parseCompression(compress, std::nullopt);
    if (!target) {
        return reportError(target.error());
    }
    auto result = core::recompressWim(wim, *target, progressTask(L"recompress"));
    print(L"\n");
    if (!result) {
        return reportError(result.error());
    }
    print(std::format(L"  -> {} ({} bytes)\n", result->wstring(), std::filesystem::file_size(*result)));
    return 0;
}

int cmdSwmMerge(const std::wstring& first, const std::wstring& out) {
    auto r = core::mergeSplitWim(first, out, progressTask(L"merge"));
    print(L"\n");
    if (!r) {
        return reportError(r.error());
    }
    print(std::format(L"  -> {} ({} bytes)\n", out, std::filesystem::file_size(out)));
    return 0;
}

int cmdSwmSplit(const std::wstring& wim, const std::wstring& first, const std::wstring& sizeMb) {
    const std::uint64_t mb = sizeMb.empty() ? 3800 : static_cast<std::uint64_t>(_wtoi64(sizeMb.c_str()));
    auto parts = core::splitWim(wim, first, mb << 20, progressTask(L"split"));
    print(L"\n");
    if (!parts) {
        return reportError(parts.error());
    }
    print(std::format(L"  {} part(s) next to {}\n", *parts, first));
    return 0;
}

int cmdDuplicate(const std::wstring& wim, const std::wstring& index, const std::wstring& name) {
    auto added = core::duplicateEdition(wim, parseIndex(index), name, progressTask(L"copy"));
    print(L"\n");
    if (!added) {
        return reportError(added.error());
    }
    print(std::format(L"  copied as index {}\n", *added));
    return 0;
}

int cmdAppend(const std::wstring& sourcePath, const std::wstring& destination, const std::wstring& indexes, const std::wstring& compress) {
    auto source = core::openSource(sourcePath);
    if (!source) {
        return reportError(source.error());
    }
    const auto compression = parseCompression(compress, core::WimCompression::Lzx);
    if (!compression) {
        return reportError(compression.error());
    }
    std::vector<int> list = parseIndexList(indexes);
    if (list.empty()) {
        for (const auto& image : source->install.images) {
            list.push_back(image.index);
        }
    }
    bool extracted = false;
    const auto scratch = std::filesystem::path(destination).parent_path() / L"append.tmp";
    auto file = core::installImageFile(*source, scratch, extracted, progressTask(L"extract"));
    if (!file) {
        return reportError(file.error());
    }
    auto r = core::exportImages(*file, list, destination, *compression, progressTask(L"append"));
    std::error_code ec;
    if (extracted) {
        std::filesystem::remove_all(scratch, ec);
    }
    print(L"\n");
    if (!r) {
        return reportError(r.error());
    }
    print(std::format(L"  {} edition(s) added to {}\n", list.size(), destination));
    return 0;
}

int cmdCapture(const std::wstring& folder, const std::wstring& wim, const std::wstring& name, const std::wstring& compress) {
    const auto compression = parseCompression(compress, core::WimCompression::Lzx);
    if (!compression) {
        return reportError(compression.error());
    }
    auto added = core::captureImage(folder, wim, core::ImageText{name, std::wstring(), std::nullopt}, *compression,
                                    progressTask(L"capture"));
    print(L"\n");
    if (!added) {
        return reportError(added.error());
    }
    print(std::format(L"  captured as index {} of {}\n", *added, wim));
    return 0;
}

// D-056: Kişiselleştirme helpers (no admin).
int cmdFontInfo(const std::wstring& file) {
    auto info = core::readFontInfo(file);
    if (!info) {
        return reportError(info.error());
    }
    print(std::format(L"  {}\n  file in Windows\\Fonts: {}\n", info->registryName(), core::fontFileName(file)));
    return 0;
}

int cmdPicture(const std::wstring& source, const std::wstring& target, const std::wstring& size, const std::wstring& format) {
    int width = 0;
    int height = 0;
    if (!size.empty()) {
        const auto x = size.find(L'x');
        width = x == std::wstring::npos ? 0 : _wtoi(size.substr(0, x).c_str());
        height = x == std::wstring::npos ? 0 : _wtoi(size.substr(x + 1).c_str());
    }
    const core::PictureFormat f = format == L"png" ? core::PictureFormat::Png
                                  : format == L"bmp" ? core::PictureFormat::Bmp
                                                     : core::PictureFormat::Jpeg;
    auto bytes = core::encodePicture(source, f, width, height);
    if (!bytes) {
        return reportError(bytes.error());
    }
    if (auto saved = writeFileAtomic(target, *bytes); !saved) {
        return reportError(saved.error());
    }
    auto written = core::pictureSize(target);
    print(std::format(L"  {} bytes, {}x{}\n", bytes->size(), written ? written->width : 0, written ? written->height : 0));
    return 0;
}

int cmdWifiList() {
    auto list = core::hostWifiProfiles();
    if (!list) {
        return reportError(list.error());
    }
    for (const auto& p : *list) {
        print(std::format(L"  {:<32} {}\n", p.name, p.portable ? L"key readable" : L"key protected (run elevated)"));
    }
    print(std::format(L"  {} profile(s)\n", list->size()));
    return 0;
}

int cmdWifiXml(const std::wstring& ssid, const std::wstring& password, bool wpa3, bool open, bool hidden) {
    core::WifiNetwork n{ssid, password,
                        open ? core::WifiSecurity::Open : wpa3 ? core::WifiSecurity::Wpa3Personal : core::WifiSecurity::Wpa2Personal,
                        hidden};
    if (const auto problem = core::validateWifi(n); problem != core::WifiProblem::None) {
        return reportError(Error{ErrorCode::InvalidArgument,
                                 problem == core::WifiProblem::Ssid ? L"SSID must be 1-32 bytes" : L"password must be 8-63 ASCII characters",
                                 ssid});
    }
    print(core::wifiProfileXml(n));
    return 0;
}

int cmdSetInfo(const std::wstring& wim, const std::wstring& index, const std::wstring& name,
               const std::wstring& description, const std::wstring& flags) {
    core::ImageText text{name, description, {}};
    if (!flags.empty()) {
        text.flags = flags;
    }
    if (auto r = core::setImageText(wim, parseIndex(index), text); !r) {
        return reportError(r.error());
    }
    print(std::format(L"  index {} of {} is now \"{}\"\n", index, wim, name));
    return 0;
}

int cmdStoreCleanup(const std::wstring& dir, bool resetBase) {
    auto session = openImageSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    const auto task = progressTask(L"cleanup");
    const auto cleaned = core::cleanupComponentStore(**session, resetBase, task);
    print(L"\n");
    if (!cleaned) {
        return reportError(cleaned.error());
    }
    print(L"component store cleaned (image not committed)\n");
    return 0;
}

// A provisioned app out of a mounted image: DISM, and — when DISM refuses it, or with --native —
// WinLove's own removal (Appx.h). Afterwards the image is checked against what either way must
// leave behind: no folder of the app's family, its license gone, nothing staged (exit 3 if not).
// The image is not committed.
int cmdAppxRemove(const std::wstring& dir, const std::wstring& package, bool native) {
    const std::wstring family = core::appxFamilyName(package);
    if (family.empty()) {
        return reportError(Error{ErrorCode::InvalidArgument, L"not a package full name", package});
    }
    // Before any DISM session: it keeps the hives to itself.
    auto staged = core::readStagedAppx(std::filesystem::path(dir), family);
    if (!staged) {
        return reportError(staged.error());
    }
    const core::ComponentRecipe recipe = core::appxRemovalRecipe(package, *staged);
    const auto before = core::probeComponent(dir, recipe);
    print(std::format(L"{}: {} package(s) staged, {} bytes on disk\n", family, staged->size(), before.size));
    {
        auto session = openImageSession(dir);
        if (!session) {
            return reportError(session.error());
        }
        bool byDism = false;
        if (!native) {
            const auto removed = (*session)->removeAppx(package);
            if (removed) {
                byDism = true;
                print(L"removed by DISM\n");
            } else if (removed.error().hresult != core::kAppxRemovalRefused) {
                return reportError(removed.error());
            } else {
                print(std::format(L"DISM refuses it (0x{:08X}): removing it natively\n",
                                  static_cast<std::uint32_t>(removed.error().hresult)));
            }
        }
        if (!byDism) {
            const auto removed = core::removeAppxNative(**session, package, core::TaskContext{g_cancel, {}});
            if (!removed) {
                return reportError(removed.error());
            }
            print(L"removed natively\n");
        }
    } // session closed: the hives can be read again
    const auto after = core::probeComponent(dir, recipe);
    const auto left = core::readStagedAppx(std::filesystem::path(dir), family);
    const bool clean = !after.present && left && left->empty();
    print(std::format(L"after: files {}, staged {}\n", after.present ? L"STILL THERE" : L"gone",
                      !left ? L"unreadable" : left->empty() ? L"none" : L"STILL LISTED"));
    print(L"(image not committed: wlcli unmount <dir> --commit)\n");
    return clean ? 0 : 3;
}

// Setup's own image (sources\boot.wim): LabConfig bypasses and drivers, mounted in `mountDir`,
// committed (BootImage.h). `bypass`: "tpm,secureboot,ram,cpu,storage" or "all"; `legacySetup`: the
// media boots into the previous Setup (24H2+, D-074).
int cmdBootPatch(const std::wstring& bootWim, const std::wstring& mountDir, const std::wstring& bypass,
                 const std::vector<std::wstring>& drivers, bool legacySetup) {
    core::BootPatch patch;
    patch.legacySetup = legacySetup;
    std::wstringstream parts(bypass);
    for (std::wstring part; !bypass.empty() && std::getline(parts, part, L',');) {
        const bool all = part == L"all";
        if (!all && part != L"tpm" && part != L"secureboot" && part != L"ram" && part != L"cpu" && part != L"storage") {
            return reportError(Error{ErrorCode::InvalidArgument, L"--bypass takes tpm,secureboot,ram,cpu,storage or all", part});
        }
        patch.bypassTpm = patch.bypassTpm || all || part == L"tpm";
        patch.bypassSecureBoot = patch.bypassSecureBoot || all || part == L"secureboot";
        patch.bypassRam = patch.bypassRam || all || part == L"ram";
        patch.bypassCpu = patch.bypassCpu || all || part == L"cpu";
        patch.bypassStorage = patch.bypassStorage || all || part == L"storage";
    }
    for (const auto& driver : drivers) {
        patch.drivers.emplace_back(driver);
    }
    if (patch.empty()) {
        return reportError(Error{ErrorCode::InvalidArgument, L"nothing to do: give --bypass=, --driver= and / or --legacy-setup", bootWim});
    }
    auto d = dism();
    if (!d) {
        return reportError(d.error());
    }
    const auto task = progressTask(L"boot");
    const auto report = core::patchBootImage(**d, bootWim, mountDir, patch, task);
    print(L"\n");
    if (!report) {
        return reportError(report.error());
    }
    print(std::format(L"  boot image index {} patched: {} LabConfig value(s), {} driver(s) added{}\n", report->index,
                      patch.labConfigValues().size(), report->driversAdded,
                      legacySetup ? L", boots into the previous Setup" : L""));
    for (const auto& refused : report->driversRefused) {
        print(std::format(L"  driver not added: {}\n", refused));
    }
    return report->driversRefused.empty() ? 0 : 3;
}

// The edition of a mounted image and what it can be changed to; with `target`: changes it
// (dism.exe /Set-Edition — one-way; the image is not committed).
int cmdEdition(const std::wstring& dir, const std::wstring& target, bool asJson) {
    auto session = openImageSession(dir);
    if (!session) {
        return reportError(session.error());
    }
    if (!target.empty()) {
        const auto task = progressTask(L"edition");
        const auto changed = core::setEdition(**session, target, task);
        print(L"\n");
        if (!changed) {
            return reportError(changed.error());
        }
    }
    const auto editions = core::readEditions(**session);
    if (!editions) {
        return reportError(editions.error());
    }
    if (asJson) {
        json targets = stringArray(editions->targets);
        print(utf8::toWide(json{{"current", narrow(editions->current)}, {"targets", targets}}.dump(2)) + L"\n");
        return 0;
    }
    print(std::format(L"current edition: {}\n", editions->current));
    for (const auto& edition : editions->targets) {
        print(std::format(L"  can become: {:<28} {}\n", edition, core::editionDisplayName(edition, 26100)));
    }
    if (!target.empty()) {
        print(L"edition changed (image not committed: wlcli unmount <dir> --commit)\n");
    }
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
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return reportError(Error{ErrorCode::NotFound, L"could not open the answer file", file, 0});
    }
    const auto options = core::parseUnattendXml(*bytes);
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
    const auto bytes = readFileBytes(planFile);
    if (!bytes) {
        return reportError(Error{ErrorCode::NotFound, L"could not open the plan", planFile, 0});
    }
    const auto plan = core::postSetupFromJson(*bytes);
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

// D-045: which writes of a .reg file the mounted image already has (offreg.dll, read only).
// ---- D-068: icons inside PE files ---------------------------------------------------------------
// "#3", "3" or a name.
core::ResourceKey iconKey(const std::wstring& text) {
    std::wstring t = text;
    if (!t.empty() && t.front() == L'#') {
        t.erase(t.begin());
    }
    if (!t.empty() && std::ranges::all_of(t, [](wchar_t c) { return std::iswdigit(c) != 0; }) && t.size() <= 5) {
        const unsigned long v = std::wcstoul(t.c_str(), nullptr, 10);
        if (v <= 0xFFFF) {
            return core::ResourceKey{static_cast<std::uint16_t>(v), {}};
        }
    }
    return core::ResourceKey{0, text};
}

int cmdIcons(const std::wstring& file, bool asJson) {
    auto bytes = readFileBytes(file);
    if (!bytes) {
        return reportError(bytes.error());
    }
    auto pe = core::PeImage::parse(std::move(*bytes));
    if (!pe) {
        return reportError(pe.error());
    }
    const auto groups = core::listIconGroups(pe->resources());
    if (asJson) {
        json list = json::array();
        for (const auto& g : groups) {
            json sizes = json::array();
            for (const auto& img : g.images) {
                sizes.push_back({{"w", img.width}, {"h", img.height}, {"bpp", img.bitCount}, {"png", img.png}, {"bytes", img.data.size()}});
            }
            list.push_back({{"index", g.index}, {"key", narrow(g.key.text())}, {"languages", g.languages}, {"images", sizes}});
        }
        printJson({{"machine", pe->machine()}, {"pe64", pe->is64()}, {"hasCode", pe->hasCode()},
                   {"signed", pe->hasEmbeddedSignature()}, {"sections", pe->sections().size()}, {"groups", list}});
        return 0;
    }
    print(std::format(L"  {} · machine 0x{:04x} · {} section(s) · {} · {}{}\n", file, pe->machine(), pe->sections().size(),
                      pe->hasCode() ? L"has code" : L"resources only", groups.size(),
                      pe->hasEmbeddedSignature() ? L" · embedded signature" : L""));
    for (const auto& s : pe->sections()) {
        print(std::format(L"    section {:<8} va 0x{:08x} vsize {:>9} raw 0x{:08x} rsize {:>9} flags 0x{:08x}\n",
                          std::wstring(s.name.begin(), s.name.end()), s.virtualAddress, s.virtualSize, s.rawPointer,
                          s.rawSize, s.characteristics));
    }
    for (const auto& g : groups) {
        std::wstring sizes;
        for (const auto& img : g.images) {
            sizes += std::format(L" {}{}", img.width, img.png ? L"p" : L"");
        }
        print(std::format(L"  [{:>4}] {:<12}{}\n", g.index, g.key.text(), sizes));
    }
    return 0;
}

int cmdIconExtract(const std::wstring& file, const std::wstring& group, const std::wstring& out) {
    auto bytes = readFileBytes(file);
    if (!bytes) {
        return reportError(bytes.error());
    }
    auto pe = core::PeImage::parse(std::move(*bytes));
    if (!pe) {
        return reportError(pe.error());
    }
    const auto key = iconKey(group);
    for (const auto& g : core::listIconGroups(pe->resources())) {
        if (g.key == key) {
            if (auto ok = writeFileAtomic(out, core::makeIco(g.images)); !ok) {
                return reportError(ok.error());
            }
            print(std::format(L"  {} → {} ({} image(s))\n", key.text(), out, g.images.size()));
            return 0;
        }
    }
    print(L"no such icon group: " + key.text() + L"\n");
    return 2;
}

// "<group>=<file.ico>" arguments → replacements.
Result<std::vector<core::IconReplacement>> iconReplacements(const std::vector<std::wstring>& specs) {
    std::vector<core::IconReplacement> out;
    for (const auto& spec : specs) {
        const auto eq = spec.find(L'=');
        if (eq == std::wstring::npos) {
            return fail(ErrorCode::InvalidArgument, L"expected <group>=<file.ico>", spec);
        }
        auto ico = readFileBytes(spec.substr(eq + 1));
        if (!ico) {
            return std::unexpected(ico.error());
        }
        auto images = core::parseIco(*ico);
        if (!images) {
            return std::unexpected(Error{images.error().code, images.error().message, spec.substr(eq + 1), 0});
        }
        out.push_back({iconKey(spec.substr(0, eq)), std::move(*images)});
    }
    return out;
}

int cmdIconPatch(const std::wstring& file, const std::vector<std::wstring>& specs, const std::wstring& out) {
    auto bytes = readFileBytes(file);
    if (!bytes) {
        return reportError(bytes.error());
    }
    auto replacements = iconReplacements(specs);
    if (!replacements) {
        return reportError(replacements.error());
    }
    const auto started = std::chrono::steady_clock::now();
    auto patched = core::patchIconBytes(*bytes, *replacements);
    if (!patched) {
        return reportError(patched.error());
    }
    if (auto ok = writeFileAtomic(out, *patched); !ok) {
        return reportError(ok.error());
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    print(std::format(L"  wrote {} ({} → {} bytes, {} ms)\n", out, bytes->size(), patched->size(), ms));
    auto check = core::verifyIconFileWithWindows(out);
    if (!check) {
        return reportError(check.error());
    }
    print(std::format(L"  Windows loads {} group(s), {} image(s), {} failed{}\n", check->groups, check->images, check->failed,
                      check->failed ? L": " + check->firstFailure : L""));
    return check->failed == 0 ? 0 : 3;
}

// 7TSP pack (archive or folder) → WinLove pack in <out>; with --source=<folder of .mun files> every
// target found there is patched into <out>\patched\ with all the pack's groups it has, and
// checked by Windows' loader (the run never writes outside <out>).
int cmdIconPack(const std::wstring& source, const std::wstring& out, const std::wstring& from) {
    std::filesystem::path folder = source;
    std::error_code ec;
    const std::filesystem::path outDir = out;
    if (std::filesystem::is_regular_file(folder, ec)) {
        const auto extracted = outDir / L"extracted";
        std::filesystem::remove_all(extracted, ec);
        if (auto ok = core::extractArchive(folder, extracted); !ok) {
            return reportError(ok.error());
        }
        folder = extracted;
    }
    auto pack = core::read7tspPack(folder);
    if (!pack) {
        return reportError(pack.error());
    }
    print(std::format(L"  {} by {} — {} file(s)\n", pack->name, pack->author.empty() ? L"?" : pack->author, pack->files.size()));
    std::vector<std::wstring> skipped;
    const auto packDir = outDir / L"pack";
    auto written = core::convert7tspPack(*pack, packDir, &skipped);
    if (!written) {
        return reportError(written.error());
    }
    print(std::format(L"  {} icon(s) → {}\n", *written, packDir.wstring()));
    for (const auto& s : skipped) {
        print(L"    skipped " + s + L"\n");
    }
    if (from.empty()) {
        return 0;
    }
    int failures = 0;
    for (const auto& file : pack->files) {
        const auto target = std::filesystem::path(from) / file.target;
        auto bytes = readFileBytes(target);
        if (!bytes) {
            print(std::format(L"  {}: not in {}\n", file.target, from));
            continue;
        }
        auto pe = core::PeImage::parse(*bytes);
        if (!pe) {
            print(std::format(L"  {}: {}\n", file.target, pe.error().message));
            continue;
        }
        if (pe->hasCode()) {
            print(std::format(L"  {}: has code — refused (D-068)\n", file.target));
            continue;
        }
        std::vector<core::ResourceKey> existing;
        for (const auto& g : core::listIconGroups(pe->resources())) {
            existing.push_back(g.key);
        }
        std::vector<core::IconReplacement> replacements;
        int missing = 0;
        for (const auto& entry : std::filesystem::directory_iterator(packDir / file.target, ec)) {
            const std::wstring stem = entry.path().stem().wstring();
            const bool digits = !stem.empty() && std::ranges::all_of(stem, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
            const core::ResourceKey key = digits ? core::ResourceKey{static_cast<std::uint16_t>(std::stoul(stem)), {}} : core::ResourceKey{0, stem};
            if (std::ranges::find(existing, key) == existing.end()) {
                ++missing;
                continue;
            }
            auto ico = readFileBytes(entry.path());
            auto images = ico ? core::parseIco(*ico) : Result<std::vector<core::IconImage>>(std::unexpected(ico.error()));
            if (!images) {
                return reportError(images.error());
            }
            replacements.push_back({key, std::move(*images)});
        }
        const auto started = std::chrono::steady_clock::now();
        auto patched = core::patchIconBytes(*bytes, replacements);
        if (!patched) {
            print(std::format(L"  {}: ", file.target));
            reportError(patched.error());
            ++failures;
            continue;
        }
        const auto outFile = outDir / L"patched" / file.target;
        std::filesystem::create_directories(outFile.parent_path(), ec);
        if (auto ok = writeFileAtomic(outFile, *patched); !ok) {
            return reportError(ok.error());
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        auto check = core::verifyIconFileWithWindows(outFile);
        if (!check) {
            return reportError(check.error());
        }
        print(std::format(L"  {}: {} group(s) replaced, {} not in this build, {} → {} bytes, {} ms; Windows loads {} group(s), {} image(s), {} failed\n",
                          file.target, replacements.size(), missing, bytes->size(), patched->size(), ms, check->groups,
                          check->images, check->failed));
        failures += check->failed > 0 ? 1 : 0;
    }
    return failures == 0 ? 0 : 3;
}

int cmdIconVerify(const std::wstring& file) {
    auto check = core::verifyIconFileWithWindows(file);
    if (!check) {
        return reportError(check.error());
    }
    print(std::format(L"  Windows loads {} group(s), {} image(s), {} failed{}\n", check->groups, check->images, check->failed,
                      check->failed ? L": " + check->firstFailure : L""));
    return check->failed == 0 ? 0 : 3;
}

int cmdIconImage(const std::wstring& mountDir, const std::wstring& relative, const std::vector<std::wstring>& specs, bool restore) {
    if (restore) {
        if (auto ok = core::restoreImageIcons(mountDir, relative); !ok) {
            return reportError(ok.error());
        }
        print(L"  restored " + relative + L"\n");
        return 0;
    }
    if (specs.empty()) {
        for (const auto& f : core::patchedIconFiles(mountDir)) {
            print(L"  patched: " + f + L"\n");
        }
        return 0;
    }
    auto replacements = iconReplacements(specs);
    if (!replacements) {
        return reportError(replacements.error());
    }
    auto backup = core::patchImageIcons(mountDir, relative, *replacements);
    if (!backup) {
        return reportError(backup.error());
    }
    print(L"  patched " + relative + L"; original kept as " + *backup + L"\n");
    return 0;
}

int cmdStartApps(const std::wstring& root, bool asJson) {
    const auto apps = core::listStartApps(root);
    if (asJson) {
        json list = json::array();
        for (const auto& a : apps) {
            list.push_back({{"kind", a.kind == core::StartApp::Kind::Packaged ? "packaged" : a.kind == core::StartApp::Kind::DesktopLink ? "link" : "id"},
                            {"id", narrow(a.id)}, {"name", narrow(a.name)}, {"icon", narrow(a.icon)}});
        }
        printJson(list);
        return 0;
    }
    for (const auto& a : apps) {
        print(std::format(L"  {:<4} {:<40} {}\n", a.kind == core::StartApp::Kind::Packaged ? L"app" : a.kind == core::StartApp::Kind::DesktopLink ? L"lnk" : L"id",
                          a.name, a.id));
    }
    print(std::format(L"\n  {} app(s) can be pinned\n", apps.size()));
    return 0;
}

int cmdRegCheck(const std::wstring& file, const std::wstring& mountDir, bool asJson) {
    auto writes = core::readRegFile(file);
    if (!writes) {
        return reportError(writes.error());
    }
    core::OfflineRegistryReader reader(mountDir);
    json out = json::array();
    int held = 0;
    int errors = 0;
    for (const auto& w : *writes) {
        std::wstring status;
        std::wstring current;
        auto holds = reader.holds(w);
        if (!holds) {
            status = L"error";
            current = holds.error().message;
            ++errors;
        } else {
            status = *holds ? L"in-image" : L"differs";
            held += *holds ? 1 : 0;
            if (w.kind == core::RegistryWrite::Kind::Set || w.kind == core::RegistryWrite::Kind::DeleteValue) {
                if (auto value = reader.value(w.key, w.name); value && *value) {
                    core::RegistryWrite seen = w;
                    seen.kind = core::RegistryWrite::Kind::Set;
                    seen.type = (*value)->type;
                    seen.data = (*value)->data;
                    current = core::formatRegValue(seen);
                } else if (value) {
                    current = L"(none)";
                }
            }
        }
        if (asJson) {
            out.push_back({{"target", narrow(core::registryTarget(w))},
                           {"value", narrow(core::formatRegValue(w))},
                           {"status", narrow(status)},
                           {"current", narrow(current)}});
        } else {
            print(std::format(L"  {:<9} {} = {}{}\n", status, core::registryTarget(w), core::formatRegValue(w),
                              current.empty() || (holds && *holds) ? L"" : L"   (image: " + current + L")"));
        }
    }
    if (asJson) {
        printJson(out);
    } else {
        print(std::format(L"\n  {} / {} already in the image{}\n", held, writes->size(),
                          errors ? std::format(L", {} unreadable", errors) : std::wstring()));
    }
    return errors == 0 ? 0 : 1;
}

// D-046: the Microsoft Update Catalog for an image build (no admin, network).
int cmdCatalog(const std::wstring& buildText, const std::wstring& arch, const std::wstring& downloadDir, bool preview,
               const std::wstring& onlyKb, bool asJson) {
    int build = 0;
    int revision = 0;
    if (swscanf_s(buildText.c_str(), L"%d.%d", &build, &revision) < 1 || build < 10000) {
        print(L"error: build must look like 26200 or 26200.8037\n");
        return 1;
    }
    const auto target = core::catalogTarget(build, revision, arch.empty() ? L"x64" : arch);
    auto offers = core::findCatalogUpdates(target, g_cancel);
    if (!offers) {
        return reportError(offers.error());
    }
    auto kindName = [](core::CatalogKind k) { return k == core::CatalogKind::DotNet ? L".NET" : L"LCU"; };
    if (asJson) {
        json out = json::array();
        for (const auto& o : *offers) {
            out.push_back({{"id", narrow(o.entry.id)},
                           {"title", narrow(o.entry.title)},
                           {"kb", narrow(o.entry.kb)},
                           {"kind", narrow(kindName(o.entry.kind))},
                           {"preview", o.entry.preview},
                           {"date", std::format("{:04}-{:02}-{:02}", o.entry.year, o.entry.month, o.entry.day)},
                           {"size", o.entry.size},
                           {"build", std::format("{}.{}", o.entry.build, o.entry.revision)},
                           {"recommended", o.recommended},
                           {"olderThanImage", o.olderThanImage}});
        }
        printJson(out);
    } else {
        print(std::format(L"  Windows {} {} {} ({}.{})\n\n", target.windows, target.release, target.architecture,
                          target.build, target.revision));
        for (const auto& o : *offers) {
            print(std::format(L"  {:<5} {:<10} {:04}-{:02}-{:02} {:>8} MB  {}{}{}\n", kindName(o.entry.kind), o.entry.kb,
                              o.entry.year, o.entry.month, o.entry.day, o.entry.size >> 20, o.entry.title,
                              o.recommended ? L"" : L"  [preview]", o.olderThanImage ? L"  [image is newer]" : L""));
        }
    }
    if (downloadDir.empty()) {
        return 0;
    }
    int failures = 0;
    for (const auto& o : *offers) {
        if (!onlyKb.empty() ? _wcsicmp(onlyKb.c_str(), o.entry.kb.c_str()) != 0
                            : (!o.recommended && !preview) || o.olderThanImage) {
            continue;
        }
        const auto task = progressTask(o.entry.kb.c_str());
        auto got = core::downloadCatalogUpdate(o.entry, downloadDir, task);
        print(L"\n");
        if (!got) {
            reportError(got.error());
            ++failures;
            continue;
        }
        print(std::format(L"  {} ({} bytes downloaded)\n", got->main.wstring(), got->bytes));
        for (const auto& p : got->prerequisites) {
            print(L"    + " + p.wstring() + L"\n");
        }
    }
    return failures == 0 ? 0 : 1;
}

// D-047: USB disks a setup stick can go to (no admin).
int cmdUsbList(bool allowVirtual, bool all, bool asJson) {
    const auto disks = all ? core::listAllDisks() : core::listUsbDisks(allowVirtual);
    if (asJson) {
        json out = json::array();
        for (const auto& d : disks) {
            json letters = stringArray(d.letters);
            out.push_back({{"disk", d.number},
                           {"name", narrow(d.name())},
                           {"serial", narrow(d.serial)},
                           {"size", d.size},
                           {"bus", d.busType},
                           {"removable", d.removableMedia},
                           {"letters", letters},
                           {"system", d.system},
                           {"identity", narrow(d.identity())}});
        }
        printJson(out);
        return 0;
    }
    if (disks.empty()) {
        print(L"  no USB disk\n");
    }
    for (const auto& d : disks) {
        std::wstring letters;
        for (const auto& l : d.letters) {
            letters += l + L" ";
        }
        print(std::format(L"  disk {:<3} {:<32} {:>8} MB  bus {:<2} {}{}\n", d.number, d.name(), d.size >> 20, d.busType,
                          letters, d.system ? L" [system: never written]" : L""));
    }
    return 0;
}

// D-047: writes a bootable setup stick. Erases the disk: --yes is required.
int cmdUsbWrite(const std::wstring& diskText, const std::wstring& folder, const std::wstring& label, bool gpt,
                const std::wstring& unattend, bool allowVirtual, bool yes) {
    const int number = parseIndex(diskText) > 0 || diskText == L"0" ? _wtoi(diskText.c_str()) : -1;
    const auto disks = core::listUsbDisks(allowVirtual);
    const auto disk = std::ranges::find(disks, number, &core::UsbDisk::number);
    if (disk == disks.end()) {
        print(L"error: no such USB disk (see wlcli usb-list)\n");
        return 1;
    }
    if (!yes) {
        print(std::format(L"  this erases disk {} ({}, {} MB). Add --yes to go on.\n", disk->number, disk->name(),
                          disk->size >> 20));
        return 1;
    }
    core::UsbOptions options;
    options.disk = disk->number;
    options.identity = disk->identity();
    options.scheme = gpt ? core::UsbScheme::GptUefi : core::UsbScheme::MbrBiosUefi;
    options.label = label.empty() ? L"WINLOVE" : label;
    options.sourceFolder = folder;
    options.allowVirtual = allowVirtual;
    if (!unattend.empty()) {
        // A wrong path used to put an empty autounattend.xml on the stick.
        auto xml = readFileBytes(unattend);
        if (!xml) {
            return reportError(xml.error());
        }
        if (xml->empty()) {
            return reportError(Error{ErrorCode::InvalidArgument, L"the answer file is empty", unattend});
        }
        options.rootFiles.push_back({L"autounattend.xml", std::move(*xml)});
    }
    const auto task = progressTask(L"usb");
    auto result = core::writeUsb(options, task);
    print(L"\n");
    if (!result) {
        return reportError(result.error());
    }
    print(std::format(L"  {} ready ({} MB{})\n", result->root, result->bytes >> 20,
                      result->swmParts ? std::format(L", install.wim in {} .swm parts", result->swmParts) : std::wstring()));
    return 0;
}

// D-052: third-party drivers of a mounted image; --remove=oemN.inf takes one out (admin).
int cmdDrivers(const std::wstring& mountDir, const std::wstring& remove, bool asJson) {
    auto session = openImageSession(mountDir);
    if (!session) {
        return reportError(session.error());
    }
    if (!remove.empty()) {
        if (auto r = (*session)->removeDriver(remove); !r) {
            return reportError(r.error());
        }
        print(L"  removed " + remove + L"\n");
    }
    auto list = (*session)->drivers();
    if (!list) {
        return reportError(list.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& e : *list) {
            out.push_back({{"publishedName", narrow(e.publishedName)}, {"originalFileName", narrow(e.originalFileName)},
                           {"className", narrow(e.className)}, {"provider", narrow(e.provider)},
                           {"version", narrow(e.version)}, {"date", narrow(e.date)}, {"bootCritical", e.bootCritical},
                           {"signed", e.signed_}});
        }
        printJson(out);
        return 0;
    }
    for (const auto& e : *list) {
        print(std::format(L"  {:<10} {:<28} {:<14} {:<24} {} {}{}\n", e.publishedName, e.originalFileName, e.className,
                          e.provider, e.version, e.date, e.bootCritical ? L"  [boot critical]" : L""));
    }
    print(std::format(L"\n  {} third-party driver(s)\n", list->size()));
    return 0;
}

// D-052: this PC's drivers into a folder (pnputil, admin).
int cmdExportHostDrivers(const std::wstring& folder) {
    auto n = core::exportHostDrivers(folder, core::TaskContext{});
    if (!n) {
        return reportError(n.error());
    }
    print(std::format(L"  {} driver package(s) in {}\n", *n, folder));
    return 0;
}

// D-053: international settings of a mounted image; --set=<json> changes them (admin).
int cmdIntl(const std::wstring& mountDir, const std::wstring& set, bool asJson) {
    auto session = openImageSession(mountDir);
    if (!session) {
        return reportError(session.error());
    }
    if (!set.empty()) {
        // "@file.json": the JSON from a file (PowerShell 5.1 mangles quotes in native arguments).
        std::string json = narrow(set);
        if (set.starts_with(L"@")) {
            auto bytes = readFileBytes(std::filesystem::path(set.substr(1)));
            if (!bytes) {
                return reportError(bytes.error());
            }
            json = std::move(*bytes);
        }
        auto settings = core::intlFromJson(json);
        if (!settings) {
            return reportError(settings.error());
        }
        if (auto r = core::setIntl(**session, *settings, core::TaskContext{}); !r) {
            return reportError(r.error());
        }
        print(L"  set: " + core::intlArguments(*settings) + L"\n");
    }
    auto intl = core::readIntl(**session);
    if (!intl) {
        return reportError(intl.error());
    }
    if (asJson) {
        json langs = stringArray(intl->languages);
        printJson({{"ui", narrow(intl->current.uiLanguage)}, {"system", narrow(intl->current.systemLocale)},
                   {"user", narrow(intl->current.userLocale)}, {"input", narrow(intl->current.inputLocale)},
                   {"timezone", narrow(intl->current.timeZone)}, {"languages", langs}});
        return 0;
    }
    print(std::format(L"  UI language   {}\n  system locale {}\n  user locale   {}\n  keyboard      {}\n  time zone     {}\n",
                      intl->current.uiLanguage, intl->current.systemLocale, intl->current.userLocale,
                      intl->current.inputLocale, intl->current.timeZone));
    for (const auto& l : intl->languages) {
        print(L"  installed     " + l + L"\n");
    }
    return 0;
}

// D-054: default app associations into a mounted image (admin).
int cmdAssociations(const std::wstring& mountDir, const std::wstring& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return reportError(bytes.error());
    }
    auto list = core::parseAssociations(*bytes);
    if (!list) {
        return reportError(list.error());
    }
    auto session = openImageSession(mountDir);
    if (!session) {
        return reportError(session.error());
    }
    if (auto r = core::importAssociations(**session, *bytes, core::tempFolder() / L"WinLove", core::TaskContext{}); !r) {
        return reportError(r.error());
    }
    print(std::format(L"  {} association(s) imported\n", list->size()));
    return 0;
}

// D-054: this PC's default app associations (admin) → a file.
int cmdExportHostAssociations(const std::wstring& file) {
    auto xml = core::exportHostAssociations(core::tempFolder() / L"WinLove");
    if (!xml) {
        return reportError(xml.error());
    }
    if (auto saved = writeFileAtomic(file, *xml); !saved) {
        return reportError(saved.error());
    }
    auto list = core::parseAssociations(*xml);
    print(std::format(L"  {} association(s) -> {}\n", list ? list->size() : 0, file));
    return 0;
}

// D-050: what an app package is and what it needs (no admin); with a mount: provision it (admin).
int cmdAppx(const std::wstring& file, const std::wstring& mountDir, const std::wstring& arch) {
    auto info = core::readAppxPackage(file);
    if (!info) {
        return reportError(info.error());
    }
    std::wstring archs;
    for (const auto& a : info->architectures) {
        archs += a + L" ";
    }
    print(std::format(L"  {}  {}\n  {}  {}\n  {}{}{}\n", info->name, info->version,
                      info->displayName.empty() ? L"(no display name)" : info->displayName,
                      info->publisherDisplay.empty() ? info->publisher : info->publisherDisplay, archs,
                      info->bundle ? L"bundle " : L"", info->framework ? L"framework" : L""));
    for (const auto& dep : info->dependencies) {
        print(L"    needs " + dep.name + L" >= " + dep.minVersion + L"\n");
    }
    auto install = core::planAppxInstall(file, arch.empty() ? L"x64" : arch);
    if (!install) {
        return reportError(install.error());
    }
    for (const auto& dep : install->dependencies) {
        print(L"    found " + dep.wstring() + L"\n");
    }
    for (const auto& m : install->missing) {
        print(L"    MISSING " + m + L"\n");
    }
    print(L"    licence " + (install->license.empty() ? std::wstring(L"none (/SkipLicense)") : install->license.wstring()) + L"\n");
    print(L"    dism.exe " + core::appxArguments(*install) + L"\n");
    if (mountDir.empty()) {
        return 0;
    }
    auto session = openImageSession(mountDir);
    if (!session) {
        return reportError(session.error());
    }
    if (auto r = core::provisionAppx(**session, *install, core::TaskContext{}); !r) {
        return reportError(r.error());
    }
    print(L"  provisioned\n");
    return 0;
}

// D-053: language packs and features under a folder (no admin).
const wchar_t* languageKindName(core::LanguagePackFile::Kind kind) {
    static constexpr const wchar_t* kKinds[] = {L"language pack", L"basic", L"fonts", L"handwriting", L"ocr",
                                                L"text-to-speech", L"speech", L"component", L"other"};
    return kKinds[static_cast<int>(kind)];
}

// D-061: the language files of a build, from uupdump.net's index of Windows Update (no admin).
// --lang=en-us[,de-de] picks languages; --parts=pack,basic,fonts,handwriting,ocr,tts,speech,components
// (default: all but components); --packages-of=<mountdir> limits components to those the image has
// (admin: DISM); --download=<folder> fetches them, SHA-256 checked, under the names DISM wants.
int cmdUupLanguages(const std::wstring& buildText, const std::wstring& arch, const std::wstring& langs, const std::wstring& parts,
                    const std::wstring& packagesOf, const std::wstring& downloadDir, bool asJson) {
    int build = 0;
    int revision = 0;
    if (swscanf_s(buildText.c_str(), L"%d.%d", &build, &revision) < 1 || build < 10000) {
        print(L"error: build must look like 26200 or 26200.8037\n");
        return 1;
    }
    const std::wstring architecture = arch.empty() ? L"x64" : arch;
    auto found = core::findUupBuild(build, revision, architecture, g_cancel);
    if (!found) {
        return reportError(found.error());
    }
    auto files = core::uupFiles(found->uuid, g_cancel);
    if (!files) {
        return reportError(files.error());
    }
    const auto languages = core::uupLanguages(*files, architecture);
    using K = core::LanguagePackFile::Kind;
    if (langs.empty()) {
        if (asJson) {
            json out = json::array();
            for (const auto& l : languages) {
                json items = json::array();
                for (const auto& f : l.files) {
                    items.push_back({{"kind", narrow(languageKindName(f.file.kind))}, {"name", narrow(f.source.name)}, {"size", f.source.size}});
                }
                out.push_back({{"language", narrow(l.language)}, {"files", items}});
            }
            printJson({{"build", narrow(found->title)}, {"uuid", narrow(found->uuid)}, {"languages", out}});
            return 0;
        }
        print(std::format(L"  {} ({})\n\n", found->title, found->uuid));
        for (const auto& l : languages) {
            std::uint64_t main = 0;
            int components = 0;
            std::wstring kinds;
            for (const auto& f : l.files) {
                if (f.file.kind == K::Satellite) {
                    ++components;
                } else {
                    main += f.source.size;
                    kinds += (kinds.empty() ? L"" : L",") + std::wstring(languageKindName(f.file.kind));
                }
            }
            print(std::format(L"  {:<11} {:>5} MB  {}  +{} component file(s)\n", l.language, main >> 20, kinds, components));
        }
        print(std::format(L"\n  {} language(s) with a language pack\n", languages.size()));
        return 0;
    }
    std::vector<std::wstring> installed;
    if (!packagesOf.empty()) {
        auto dism = core::Dism::instance();
        if (!dism) {
            return reportError(dism.error());
        }
        auto session = (*dism)->openSession(packagesOf);
        if (!session) {
            return reportError(session.error());
        }
        auto packages = (*session)->packages();
        if (!packages) {
            return reportError(packages.error());
        }
        for (const auto& p : *packages) {
            if (p.state == core::ServicingState::Installed) {
                installed.push_back(p.name);
            }
        }
    }
    std::wstring wanted = L"," + (parts.empty() ? std::wstring(L"pack,basic,fonts,handwriting,ocr,tts,speech") : parts) + L",";
    for (auto& c : wanted) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    auto partOf = [](K k) -> const wchar_t* {
        switch (k) {
        case K::LanguagePack: return L"pack";
        case K::Basic: return L"basic";
        case K::Fonts: return L"fonts";
        case K::Handwriting: return L"handwriting";
        case K::Ocr: return L"ocr";
        case K::TextToSpeech: return L"tts";
        case K::Speech: return L"speech";
        case K::Satellite: return L"components";
        default: return L"other";
        }
    };
    std::vector<core::UupLanguageFile> chosen;
    std::wstringstream list(langs);
    for (std::wstring tag; std::getline(list, tag, L',');) {
        const auto it = std::ranges::find_if(languages, [&](const core::UupLanguage& l) { return _wcsicmp(l.language.c_str(), tag.c_str()) == 0; });
        if (it == languages.end()) {
            print(L"error: no language pack for " + tag + L" in this build\n");
            return 1;
        }
        for (const auto& f : it->files) {
            if (wanted.find(L"," + std::wstring(partOf(f.file.kind)) + L",") == std::wstring::npos) {
                continue;
            }
            if (f.file.kind == K::Satellite && !packagesOf.empty() && !core::satelliteFits(f.file, installed)) {
                continue;
            }
            chosen.push_back(f);
        }
    }
    std::uint64_t total = 0;
    for (const auto& f : chosen) {
        total += f.source.size;
        print(std::format(L"  {:<15} {:>8} KB  {}\n", languageKindName(f.file.kind), f.source.size >> 10, core::uupSaveName(f)));
    }
    print(std::format(L"\n  {} file(s), {} MB\n", chosen.size(), total >> 20));
    if (downloadDir.empty()) {
        return 0;
    }
    const auto task = progressTask(L"download");
    auto saved = core::downloadUupFiles(chosen, downloadDir, task);
    print(L"\n");
    if (!saved) {
        return reportError(saved.error());
    }
    print(std::format(L"  {} file(s) in {}\n", saved->size(), downloadDir));
    return 0;
}

// D-066: Microsoft Store apps through Microsoft's own services (no admin).
int cmdStoreSearch(const std::wstring& query, bool asJson) {
    auto results = core::searchStore(query, g_cancel);
    if (!results) {
        return reportError(results.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& r : *results) {
            out.push_back({{"id", narrow(r.productId)}, {"name", narrow(r.name)}, {"publisher", narrow(r.publisher)}});
        }
        printJson(out);
        return 0;
    }
    for (const auto& r : *results) {
        print(std::format(L"  {}  {:<40}  {}\n", r.productId, r.name, r.publisher));
    }
    print(std::format(L"\n  {} app(s)\n", results->size()));
    return 0;
}

int cmdStoreGet(const std::wstring& productId, const std::wstring& arch, const std::wstring& downloadDir) {
    auto product = core::storeProduct(productId, g_cancel);
    if (!product) {
        return reportError(product.error());
    }
    print(std::format(L"  {} · {} · {}\n", product->title, product->publisher, product->packageFamilyName));
    auto all = core::storePackages(*product, g_cancel);
    if (!all) {
        return reportError(all.error());
    }
    const auto picked = core::pickStorePackages(*all, product->packageFamilyName, arch.empty() ? L"x64" : arch);
    print(std::format(L"  {} package(s) on Windows Update, {} for {}:\n", all->size(), picked.size(), arch.empty() ? L"x64" : arch));
    for (const auto& p : picked) {
        print(std::format(L"    {:>9} KB  {}{}\n", p.size >> 10, p.fileName(), p.framework ? L"  (framework)" : L""));
    }
    if (picked.empty()) {
        return 1;
    }
    if (downloadDir.empty()) {
        return 0;
    }
    const auto task = progressTask(L"download");
    auto saved = core::downloadStorePackages(picked, downloadDir, task);
    print(L"\n");
    if (!saved) {
        return reportError(saved.error());
    }
    for (const auto& f : *saved) {
        print(L"  " + f.wstring() + L"\n");
    }
    return 0;
}

int cmdLanguages(const std::wstring& folder) {
    const auto files = core::scanLanguageFiles(folder);
    for (const auto& f : files) {
        const std::wstring cbs = core::cbsFileName(f);
        print(std::format(L"  {:<8} {:<6} {:<15} {:>6} MB  {}{}\n", f.language.empty() ? L"-" : f.language, f.architecture,
                          languageKindName(f.kind), f.size >> 20, f.path.filename().wstring(),
                          cbs.empty() || _wcsicmp(cbs.c_str(), f.path.filename().c_str()) == 0 ? L"" : L"  (UUP name)"));
    }
    print(std::format(L"\n  {} language file(s)\n", files.size()));
    return 0;
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
    const auto c = parseCompression(compression, core::WimCompression::Lzx);
    if (!c) {
        return reportError(c.error());
    }
    const auto task = progressTask(L"export");
    if (auto r = core::exportImage(source, parseIndex(index), destination, *c, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    print(std::format(L"\n  exported index {} -> {}\n", index, destination));
    return 0;
}

// Removes editions and rewrites the WIM with the ones that stay (what the Images page does).
int cmdDeleteIndex(const std::wstring& wim, const std::wstring& list) {
    const std::vector<int> indexes = parseIndexList(list);
    if (indexes.empty()) {
        return reportError(Error{ErrorCode::InvalidArgument, L"no edition index given", list});
    }
    std::error_code ec;
    const auto before = std::filesystem::file_size(wim, ec);
    const auto task = progressTask(L"remove");
    if (auto r = core::removeImages(wim, indexes, task); !r) {
        print(L"\n");
        return reportError(r.error());
    }
    const auto after = std::filesystem::file_size(wim, ec);
    print(std::format(L"\n  removed index {} from {}: {} -> {} bytes\n", list, wim, before, after));
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
          L"  wlcli repair <dir>                  (inspect a mount folder and do what it needs: remount / discard / clean)\n"
          L"  wlcli packages|features|capabilities <mountdir>\n"
          L"  wlcli iso <setup-folder> <out.iso> [--label=X] [--boot=both|uefi|bios] [--sha256] [--no-prompt]\n"
          L"  wlcli unattend <answer.xml>         (read an answer file; print it as WinLove writes it, P13)\n"
          L"  wlcli postsetup <plan.json> <mountdir>   (write post-setup scripts and payloads into the image, P14)\n"
          L"  wlcli reg <file.reg> [<mountdir>] [--first-logon]   (parse; with a mount: write into the image's\n"
          L"                                      hives, P11; --first-logon: also re-import after setup)\n"
          L"  wlcli drivers <mountdir> [--remove=oemN.inf] [--json]   (third-party drivers of the image; admin)\n"
          L"  wlcli export-host-drivers <folder>    (this PC's drivers, pnputil /export-driver; admin)\n"
          L"  wlcli intl <mountdir> [--set=<json>] [--json]   (UI language, locales, keyboard, time zone; admin)\n"
          L"  wlcli associations <mountdir> <file.xml>   (default app associations into the image; admin)\n"
          L"  wlcli export-host-associations <file.xml>  (this PC's default app associations; admin)\n"
          L"  wlcli appx-info <package> [--arch=x64]      (manifest, dependencies found next to it; no admin)\n"
          L"  wlcli font-info <font>                     (registry name Windows gives it; no admin)\n"
          L"  wlcli hash <file> [--expect=<sha256>]      (SHA-256; exit 3 when it does not match)\n"
          L"  wlcli recompress <wim|esd> --compress=lzx|xpress|none|esd   (required; every edition, the file replaced)\n"
          L"  wlcli swm-split <wim> <first.swm> [--size-mb=3800]  ·  wlcli swm-merge <first.swm> <out.wim>\n"
          L"  wlcli duplicate <wim> <index> <name>       (a copy of an edition in the same WIM)\n"
          L"  wlcli append <iso|wim|esd|swm> <dest.wim> [--index=1,3] [--compress=lzx]   (editions added)\n"
          L"  wlcli capture <folder> <wim> <name> [--compress=lzx|xpress]   (admin; new or appended edition)\n"
          L"  wlcli picture <src> <dst> [--size=WxH] [--format=jpg|png|bmp]   (WIC: cover-scale + encode)\n"
          L"  wlcli wifi-list                            (this PC's Wi-Fi profiles; keys readable when elevated)\n"
          L"  wlcli wifi-xml --ssid=<name> [--password=<key>] [--wpa3|--open] [--hidden]   (WLAN profile XML)\n"
          L"  wlcli appx-add <mountdir> <package> [--arch=x64]   (provision an .appx / .msix (bundle); admin)\n"
          L"  wlcli languages <folder>                   (language packs and features under a folder)\n"
          L"  wlcli uup-languages <build>[.<rev>] [--arch=x64] [--lang=en-us,...] [--parts=pack,basic,fonts,handwriting,ocr,\n"
          L"                                      tts,speech,components] [--packages-of=<mountdir>] [--download=<folder>] [--json]\n"
          L"                                      (the build's language files from Windows Update, via uupdump.net; D-061)\n"
          L"  wlcli store-search <name> [--json]          (Microsoft Store apps; D-066)\n"
          L"  wlcli store-get <product id> [--arch=x64] [--download=<folder>]   (the app + frameworks from Windows Update)\n"
          L"  wlcli usb-list [--all] [--json] [--allow-virtual]   USB disks a setup stick can go to (never the system\n"
          L"                                      disk; --allow-virtual: file-backed VHD(X) disks too, for the lab)\n"
          L"  wlcli usb-write <disk> <setup folder> --yes [--gpt] [--label=] [--unattend=<xml>] [--allow-virtual]\n"
          L"                                      (admin; ERASES the\n"
          L"                                      disk: FAT32, BIOS + UEFI (--gpt: UEFI only), install.wim > 4 GB -> .swm)\n"
          L"  wlcli catalog <build>[.<revision>] [--arch=x64|arm64] [--download=<folder>] [--preview] [--kb=KB…] [--json]\n"
          L"                                      (newest cumulative + .NET updates from the Microsoft Update\n"
          L"                                      Catalog; --download: fetch the recommended ones, SHA-256 checked)\n"
          L"  wlcli start-apps <mountdir> [--json]       (apps the Start menu can pin; D-069)\n"
          L"  wlcli icons <file> [--json]               (icon groups of a .mun / .dll / .exe; D-068)\n"
          L"  wlcli icon-extract <file> <group> <out.ico>   (group: #3, 3 or a name)\n"
          L"  wlcli icon-patch <file> <group>=<ico>... --out=<file>   (writes a patched copy, checked by Windows)\n"
          L"  wlcli icon-verify <file>                   (Windows loads every icon image of the file)\n"
          L"  wlcli icon-pack <pack.7z|zip|folder> <outdir> [--source=<folder of .mun files>]   (7TSP pack -> WinLove\n"
          L"                                             pack; with --source each target is patched into <outdir>\\patched)\n"
          L"  wlcli icon-image <mountdir> <path under the image> [<group>=<ico>...] [--restore]   (admin; patch in\n"
          L"                                      place with backup + restore script, or put the original back)\n"
          L"  wlcli reg-check <file.reg> <mountdir> [--json]   (which writes the image already has; read only,\n"
          L"                                      offreg.dll — also works on hive files copied into a folder)\n"
          L"  wlcli services <mountdir> [--set=Name=auto|autoDelayed|manual|disabled]   (P10)\n"
          L"  wlcli appx <mountdir>   (provisioned apps + size, as on P07)\n"
          L"  wlcli cbs <mountdir> [text]   (CBS packages from the image's registry, hidden ones too)\n"
          L"  wlcli component <mountdir> <recipe.json> [--remove]   (P07 system component: probe / remove)\n"
          L"  wlcli store-cleanup <mountdir> [--resetbase]   (dism /Cleanup-Image /StartComponentCleanup)\n"
          L"  wlcli edition <mountdir> [--set=<EditionId>] [--json]   (current + target editions; --set: dism /Set-Edition)\n"
          L"  wlcli appx-remove <mountdir> <PackageFullName> [--native]   (DISM; natively when DISM refuses the app)\n"
          L"  wlcli boot-patch <boot.wim> <mountdir> [--bypass=tpm,secureboot,ram,cpu,storage|all] [--driver=<inf>]...\n"
          L"                                      [--legacy-setup]   (Setup's image: LabConfig, drivers, previous Setup; mounts, commits)\n"
          L"  wlcli optional-features <mountdir>   (features + capabilities with names, as on P04)\n"
          L"  wlcli apply <changeset.json> <mountdir> [--commit] [--source=<sources\\sxs>]\n"
          L"                                      [--also=2,3 --wim=<file>] [--setup=<setup folder>]   (with --commit: then the same on further editions)\n"
          L"\n  Change sets (no admin):\n"
          L"  wlcli plan <changeset.json>              Show the ordered apply plan\n"
          L"  wlcli extract-all <iso> <dir>             Copy the whole ISO into a folder (resumable)\n"
          L"  wlcli export <wim|esd> <index> <dst.wim> [--compress=lzx|xpress|none|esd]   (lzx when left out)\n"
          L"  wlcli delete-index <wim> <index>[,<index>...]   Remove editions; the WIM is rewritten with the rest\n"
          L"  wlcli optimize <wim>                      Rewrite a WIM without what commits left behind\n"
          L"  wlcli verify <iso|wim|folder>             Read every stream and check its SHA-1 (exit 3: damaged)\n"
          L"  wlcli wim-file <iso|wim|folder> <index> <path>   Is the file in the edition? (file list, no mount; exit 1: no)\n"
          L"  wlcli set-info <wim> <index> <name> [<description>] [--flags=<EditionId>]\n"
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
    std::wstring flags;
    std::wstring bypass;
    std::vector<std::wstring> drivers;
    bool native = false;
    bool legacySetup = false;
    std::wstring boot;
    bool sha = false;
    bool noPrompt = false;
    bool firstLogon = false;
    std::wstring outPath;
    bool restoreIcons = false;
    bool remove = false;
    bool resetBase = false;
    std::wstring arch;
    std::wstring downloadDir;
    bool preview = false;
    std::wstring onlyKb;
    bool allowVirtual = false;
    std::wstring removeName;
    std::wstring alsoEditions;
    std::wstring wimPath;
    bool yes = false;
    bool listAll = false;
    bool gpt = false;
    std::wstring unattendFile;
    std::wstring pictureSize;
    std::wstring pictureFormat;
    std::wstring ssid;
    std::wstring wifiPassword;
    bool wpa3 = false;
    bool openNetwork = false;
    bool hidden = false;
    std::wstring expectHash;
    std::wstring sizeMb;
    std::wstring indexList;
    std::wstring langList;
    std::wstring partList;
    std::wstring packagesOf;
    std::wstring setupFolder;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        if (a == L"--json") {
            asJson = true;
        } else if (a.starts_with(L"--compress=")) {
            compress = std::wstring(a.substr(11));
        } else if (a == L"--native") {
            native = true;
        } else if (a == L"--legacy-setup") {
            legacySetup = true;
        } else if (a.starts_with(L"--bypass=")) {
            bypass = std::wstring(a.substr(9));
        } else if (a.starts_with(L"--driver=")) {
            drivers.emplace_back(a.substr(9));
        } else if (a.starts_with(L"--flags=")) {
            flags = std::wstring(a.substr(8));
        } else if (a.starts_with(L"--set=")) {
            serviceSet = std::wstring(a.substr(6));
        } else if (a.starts_with(L"--arch=")) {
            arch = std::wstring(a.substr(7));
        } else if (a.starts_with(L"--download=")) {
            downloadDir = std::wstring(a.substr(11));
        } else if (a.starts_with(L"--lang=")) {
            langList = std::wstring(a.substr(7));
        } else if (a.starts_with(L"--parts=")) {
            partList = std::wstring(a.substr(8));
        } else if (a.starts_with(L"--setup=")) {
            setupFolder = std::wstring(a.substr(8));
        } else if (a.starts_with(L"--packages-of=")) {
            packagesOf = std::wstring(a.substr(14));
        } else if (a.starts_with(L"--kb=")) {
            onlyKb = std::wstring(a.substr(5));
        } else if (a.starts_with(L"--also=")) {
            alsoEditions = std::wstring(a.substr(7));
        } else if (a.starts_with(L"--wim=")) {
            wimPath = std::wstring(a.substr(6));
        } else if (a.starts_with(L"--remove=")) {
            removeName = std::wstring(a.substr(9));
        } else if (a == L"--allow-virtual") {
            allowVirtual = true;
        } else if (a == L"--all") {
            listAll = true;
        } else if (a == L"--yes") {
            yes = true;
        } else if (a == L"--gpt") {
            gpt = true;
        } else if (a.starts_with(L"--size=")) {
            pictureSize = std::wstring(a.substr(7));
        } else if (a.starts_with(L"--format=")) {
            pictureFormat = std::wstring(a.substr(9));
        } else if (a.starts_with(L"--ssid=")) {
            ssid = std::wstring(a.substr(7));
        } else if (a.starts_with(L"--password=")) {
            wifiPassword = std::wstring(a.substr(11));
        } else if (a == L"--wpa3") {
            wpa3 = true;
        } else if (a == L"--open") {
            openNetwork = true;
        } else if (a.starts_with(L"--expect=")) {
            expectHash = std::wstring(a.substr(9));
        } else if (a.starts_with(L"--size-mb=")) {
            sizeMb = std::wstring(a.substr(10));
        } else if (a.starts_with(L"--index=")) {
            indexList = std::wstring(a.substr(8));
        } else if (a == L"--hidden") {
            hidden = true;
        } else if (a.starts_with(L"--unattend=")) {
            unattendFile = std::wstring(a.substr(11));
        } else if (a == L"--preview") {
            preview = true;
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
        } else if (a.starts_with(L"--out=")) {
            outPath = std::wstring(a.substr(6));
        } else if (a == L"--restore") {
            restoreIcons = true;
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
    if (command == L"verify" && args.size() == 2) {
        return cmdVerify(args[1]);
    }
    if (command == L"wim-file" && args.size() == 4) {
        return cmdWimFile(args[1], args[2], args[3]);
    }
    if (command == L"set-info" && (args.size() == 4 || args.size() == 5)) {
        return cmdSetInfo(args[1], args[2], args[3], args.size() == 5 ? args[4] : L"", flags);
    }
    if (command == L"optimize" && args.size() == 2) {
        return cmdOptimize(args[1]);
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
        return cmdApply(args[1], args[2], commit == 1, source, alsoEditions, wimPath, setupFolder);
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
    if (command == L"appx-remove" && args.size() == 3) {
        return cmdAppxRemove(args[1], args[2], native);
    }
    if (command == L"boot-patch" && args.size() == 3) {
        return cmdBootPatch(args[1], args[2], bypass, drivers, legacySetup);
    }
    if (command == L"edition" && args.size() == 2) {
        return cmdEdition(args[1], serviceSet, asJson);
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
    if (command == L"drivers" && args.size() == 2) {
        return cmdDrivers(args[1], removeName, asJson);
    }
    if (command == L"export-host-drivers" && args.size() == 2) {
        return cmdExportHostDrivers(args[1]);
    }
    if (command == L"intl" && args.size() == 2) {
        return cmdIntl(args[1], serviceSet, asJson);
    }
    if (command == L"associations" && args.size() == 3) {
        return cmdAssociations(args[1], args[2]);
    }
    if (command == L"export-host-associations" && args.size() == 2) {
        return cmdExportHostAssociations(args[1]);
    }
    if (command == L"hash" && args.size() == 2) {
        return cmdHash(args[1], expectHash);
    }
    if (command == L"recompress" && args.size() == 2) {
        return cmdRecompress(args[1], compress);
    }
    if (command == L"swm-merge" && args.size() == 3) {
        return cmdSwmMerge(args[1], args[2]);
    }
    if (command == L"swm-split" && args.size() == 3) {
        return cmdSwmSplit(args[1], args[2], sizeMb);
    }
    if (command == L"duplicate" && args.size() == 4) {
        return cmdDuplicate(args[1], args[2], args[3]);
    }
    if (command == L"append" && args.size() == 3) {
        return cmdAppend(args[1], args[2], indexList, compress);
    }
    if (command == L"capture" && args.size() == 4) {
        return cmdCapture(args[1], args[2], args[3], compress);
    }
    if (command == L"font-info" && args.size() == 2) {
        return cmdFontInfo(args[1]);
    }
    if (command == L"picture" && args.size() == 3) {
        return cmdPicture(args[1], args[2], pictureSize, pictureFormat);
    }
    if (command == L"wifi-list") {
        return cmdWifiList();
    }
    if (command == L"wifi-xml" && !ssid.empty()) {
        return cmdWifiXml(ssid, wifiPassword, wpa3, openNetwork, hidden);
    }
    if (command == L"appx-info" && args.size() == 2) {
        return cmdAppx(args[1], L"", arch);
    }
    if (command == L"appx-add" && args.size() == 3) {
        return cmdAppx(args[2], args[1], arch);
    }
    if (command == L"languages" && args.size() == 2) {
        return cmdLanguages(args[1]);
    }
    if (command == L"usb-list") {
        return cmdUsbList(allowVirtual, listAll, asJson);
    }
    if (command == L"usb-write" && args.size() == 3) {
        return cmdUsbWrite(args[1], args[2], label, gpt, unattendFile, allowVirtual, yes);
    }
    if (command == L"store-search" && args.size() == 2) {
        return cmdStoreSearch(args[1], asJson);
    }
    if (command == L"store-get" && args.size() == 2) {
        return cmdStoreGet(args[1], arch, downloadDir);
    }
    if (command == L"uup-languages" && args.size() == 2) {
        return cmdUupLanguages(args[1], arch, langList, partList, packagesOf, downloadDir, asJson);
    }
    if (command == L"catalog" && args.size() == 2) {
        return cmdCatalog(args[1], arch, downloadDir, preview, onlyKb, asJson);
    }
    if (command == L"start-apps" && args.size() == 2) {
        return cmdStartApps(args[1], asJson);
    }
    if (command == L"icons" && args.size() == 2) {
        return cmdIcons(args[1], asJson);
    }
    if (command == L"icon-extract" && args.size() == 4) {
        return cmdIconExtract(args[1], args[2], args[3]);
    }
    if (command == L"icon-pack" && args.size() == 3) {
        return cmdIconPack(args[1], args[2], source);
    }
    if (command == L"icon-verify" && args.size() == 2) {
        return cmdIconVerify(args[1]);
    }
    if (command == L"icon-patch" && args.size() >= 3 && !outPath.empty()) {
        return cmdIconPatch(args[1], std::vector<std::wstring>(args.begin() + 2, args.end()), outPath);
    }
    if (command == L"icon-image" && args.size() >= 3) {
        return cmdIconImage(args[1], args[2], std::vector<std::wstring>(args.begin() + 3, args.end()), restoreIcons);
    }
    if (command == L"reg-check" && args.size() == 3) {
        return cmdRegCheck(args[1], args[2], asJson);
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
