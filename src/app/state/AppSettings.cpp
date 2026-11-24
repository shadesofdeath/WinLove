#include "app/state/AppSettings.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <format>

#include <windows.h>

namespace wl::app {

namespace {

// Report 2026-11-24 ("cannot create destination (0x00000000)" opening a UUP ISO; a shorter file
// name helped): the manifest says longPathAware, but Windows lets a path past 259 characters
// through only when LongPathsEnabled is on, and it is off by default. Setup media keeps files 147
// characters below its root (sources\replacementmanifests\microsoft-windows-appx-deployment-
// server\…), the copy writes each as "<name>.partial" first, and UUP / official ISO names run to
// ~75 characters: under C:\Users\<name>\AppData\Local\WinLove\work that is ~270.
constexpr std::size_t kMaxPath = 259;
constexpr std::size_t kMediaDepth = 1 + 147 + 8 + 16; // separator, deepest file, ".partial", room

bool longPathsEnabled() noexcept {
    using Fn = BOOLEAN(NTAPI*)();
    static const bool enabled = [] {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto fn = ntdll ? reinterpret_cast<Fn>(GetProcAddress(ntdll, "RtlAreLongPathsEnabled")) : nullptr;
        return fn != nullptr && fn() != FALSE;
    }();
    return enabled;
}

} // namespace

std::filesystem::path AppSettings::workFolderFor(const std::filesystem::path& root, const std::filesystem::path& source) {
    const std::wstring stem = source.stem().wstring();
    const std::size_t rootLength = root.wstring().size() + 1;
    if (rootLength + stem.size() + kMediaDepth <= kMaxPath) {
        return root / stem;
    }
    // FNV-1a of the whole name, any case: two long names cut alike stay apart.
    std::uint32_t hash = 2166136261u;
    for (const wchar_t c : text::lower(stem)) {
        hash = (hash ^ static_cast<std::uint32_t>(c)) * 16777619u;
    }
    const std::wstring tag = std::format(L"~{:08x}", hash);
    const std::size_t used = rootLength + tag.size() + kMediaDepth;
    std::wstring cut = stem.substr(0, std::min(stem.size(), used < kMaxPath ? kMaxPath - used : 0));
    while (!cut.empty() && (cut.back() == L'.' || cut.back() == L' ' || cut.back() == L'-' || cut.back() == L'_')) {
        cut.pop_back(); // no folder name ends in "." or " "; the rest reads better without
    }
    return root / (cut + tag);
}

std::filesystem::path AppSettings::workDirectoryFor(const std::filesystem::path& source) const {
    const auto root = workRoot / L"work";
    const auto whole = root / source.stem();
    std::error_code ec;
    if (longPathsEnabled() && std::filesystem::is_directory(whole, ec)) {
        return whole; // made under the whole name where long paths work: not extracted again
    }
    return workFolderFor(root, source);
}

std::uint32_t accentFromText(std::string_view text) noexcept {
    static constexpr std::pair<std::string_view, ui::Accent> kNames[] = {{"blue", ui::Accent::Blue},
                                                                        {"copper", ui::Accent::Copper},
                                                                        {"sea", ui::Accent::Sea},
                                                                        {"pomegranate", ui::Accent::Pomegranate},
                                                                        {"olive", ui::Accent::Olive}};
    for (const auto& [name, preset] : kNames) {
        if (text == name) {
            return ui::presetColor(preset);
        }
    }
    if (!text.empty() && text.size() <= 16) {
        std::wstring wide(text.begin(), text.end()); // ASCII is all a colour can be
        if (const auto rgb = ui::parseHexColor(wide)) {
            return *rgb;
        }
    }
    return ui::kDefaultAccent;
}

std::filesystem::path AppSettings::defaultFile() {
    return log::defaultDirectory().parent_path() / L"settings.json";
}

std::filesystem::path AppSettings::defaultWorkRoot() {
    return log::defaultDirectory().parent_path(); // %LOCALAPPDATA%\WinLove
}

bool AppSettings::isWorkCopy(const std::filesystem::path& folder) const {
    const std::wstring path = folder.lexically_normal().wstring();
    if (!folder.is_absolute() || path.find(L"..") != std::wstring::npos) {
        return false;
    }
    for (const auto& root : {workRoot / L"work", legacyMountDirectory().parent_path() / L"work"}) {
        const std::wstring base = root.lexically_normal().wstring();
        // Strictly inside: the work folder itself is not a copy of anything.
        if (path.size() > base.size() + 1 && _wcsnicmp(path.c_str(), base.c_str(), base.size()) == 0 &&
            (path[base.size()] == L'\\' || path[base.size()] == L'/')) {
            return true;
        }
    }
    return false;
}

AppSettings AppSettings::load(const std::filesystem::path& file) {
    AppSettings settings;
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return settings;
    }
    const auto doc = nlohmann::json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        log::warn("app", L"settings.json is corrupt; using defaults");
        return settings;
    }
    // Wrongly typed fields ("workRoot": 5) are skipped instead of throwing out of startup.
    auto text = [&](const char* key) -> std::string {
        const auto it = doc.find(key);
        return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    if (const auto root = text("workRoot"); !root.empty()) {
        settings.workRoot = utf8::toWide(root);
    }
    if (const auto iso = text("isoFolder"); !iso.empty()) {
        settings.isoFolder = utf8::toWide(iso);
    }
    if (const auto mount = text("mountFolder"); !mount.empty()) {
        settings.mountFolder = utf8::toWide(mount);
    }
    const std::string theme = text("theme");
    settings.theme = theme == "light"    ? ThemeChoice::Light
                     : theme == "black" || theme == "hc" ? ThemeChoice::Black
                     : theme == "system" ? ThemeChoice::System
                                         : ThemeChoice::Dark;
    settings.accent = accentFromText(text("accent"));
    settings.language = text("language") == "en" ? Language::English : Language::Turkish;
    if (const auto it = doc.find("reduceMotion"); it != doc.end() && it->is_boolean()) {
        settings.reduceMotion = it->get<bool>();
    }
    if (const auto it = doc.find("pauseBeforeSave"); it != doc.end() && it->is_boolean()) {
        settings.pauseBeforeSave = it->get<bool>();
    }
    if (const auto it = doc.find("isoAfterApply"); it != doc.end() && it->is_boolean()) {
        settings.isoAfterApply = it->get<bool>();
    }
    const std::string dism = text("dismEngine");
    settings.dismEngine = dism == "host" ? DismEngineChoice::Host : dism == "adk" ? DismEngineChoice::Adk : DismEngineChoice::Auto;
    if (const auto it = doc.find("esdThreads"); it != doc.end() && it->is_number_integer()) {
        settings.esdThreads = std::clamp(it->get<int>(), 0, 256);
    }
    if (const auto it = doc.find("presetInIso"); it != doc.end() && it->is_boolean()) {
        settings.presetInIso = it->get<bool>();
    }
    if (const auto it = doc.find("updateGuard"); it != doc.end() && it->is_boolean()) {
        settings.updateGuard = it->get<bool>();
    }
    if (const auto it = doc.find("checkUpdates"); it != doc.end() && it->is_boolean()) {
        settings.checkUpdates = it->get<bool>();
    }
    if (const auto it = doc.find("updateCheckedAt"); it != doc.end() && it->is_number_integer()) {
        settings.updateCheckedAt = std::max<std::int64_t>(it->get<std::int64_t>(), 0);
    }
    settings.updateLatestTag = utf8::toWide(text("updateLatestTag"));
    settings.updateNotifiedTag = utf8::toWide(text("updateNotifiedTag"));
    const std::string font = text("font");
    settings.font = font == "geist" ? UiFont::Geist : font == "inter" ? UiFont::Inter : UiFont::SegoeVariable;
    // D-107: settings written before the WinLoveUI design ("design" < 2) take its accent and type
    // family once; what is chosen afterwards stays.
    if (const auto it = doc.find("design"); it == doc.end() || !it->is_number_integer() || it->get<int>() < 2) {
        settings.accent = ui::kDefaultAccent;
        settings.font = UiFont::SegoeVariable;
    }
    const std::string density = text("density");
    settings.density = density == "large"         ? Density::Large
                       : density == "comfortable" ? Density::Comfortable
                                                  : Density::Compact;
    if (const auto it = doc.find("closedNavGroups"); it != doc.end() && it->is_array()) {
        for (const auto& group : *it) {
            if (group.is_number_integer() && group.get<int>() >= 0 && group.get<int>() < 16) {
                settings.closedNavGroups.push_back(group.get<int>());
            }
        }
    }
    if (const auto it = doc.find("windowsDownload"); it != doc.end() && it->is_object()) {
        auto& w = settings.windowsDownload;
        // By type, like the fields above: value() throws on "updates": 1, out of every startup.
        const auto flag = [&](const char* key, bool& out) {
            if (const auto f = it->find(key); f != it->end() && f->is_boolean()) {
                out = f->get<bool>();
            }
        };
        flag("updates", w.updates);
        flag("edge", w.edge);
        flag("apps", w.apps);
        flag("netFx3", w.netFx3);
        flag("resetBase", w.resetBase);
        flag("esd", w.esd);
        if (const auto ex = it->find("excludedApps"); ex != it->end() && ex->is_array()) {
            for (const auto& id : *ex) {
                if (id.is_string() && !id.get<std::string>().empty()) {
                    w.excludedApps.push_back(utf8::toWide(id.get<std::string>()));
                }
            }
        }
    }
    if (const auto it = doc.find("guards"); it != doc.end() && it->is_array()) {
        std::vector<std::wstring> guards;
        for (const auto& id : *it) {
            if (id.is_string() && !id.get<std::string>().empty()) {
                guards.push_back(utf8::toWide(id.get<std::string>()));
            }
        }
        settings.guards = std::move(guards);
    }
    if (const auto it = doc.find("guardsSeen"); it != doc.end() && it->is_array()) {
        std::vector<std::wstring> seen;
        for (const auto& id : *it) {
            if (id.is_string() && !id.get<std::string>().empty()) {
                seen.push_back(utf8::toWide(id.get<std::string>()));
            }
        }
        settings.guardsSeen = std::move(seen);
    }
    return settings;
}

void AppSettings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    static constexpr const char* kThemes[] = {"dark", "light", "black", "system"};
    static constexpr const char* kDensities[] = {"compact", "comfortable", "large"};
    static constexpr const char* kFonts[] = {"inter", "geist", "segoe"};
    // Through a temporary file: a crash mid-write must not reset every setting to its default.
    nlohmann::json doc{{"version", 1},
                       {"design", 2}, // D-107
                       {"theme", kThemes[static_cast<std::size_t>(theme)]},
                       {"accent", utf8::fromWide(ui::hexColor(accent))}, // D-143: "#RRGGBB"
                       {"reduceMotion", reduceMotion},
                       {"density", kDensities[static_cast<std::size_t>(density)]},
                       {"font", kFonts[static_cast<std::size_t>(font)]},
                       {"closedNavGroups", closedNavGroups},
                       {"language", language == Language::English ? "en" : "tr"},
                       {"workRoot", utf8::fromWide(workRoot.wstring())},
                       {"mountFolder", utf8::fromWide(mountFolder.wstring())},
                       {"isoFolder", utf8::fromWide(isoFolder.wstring())},
                       {"pauseBeforeSave", pauseBeforeSave},
                       {"isoAfterApply", isoAfterApply},
                       {"dismEngine", dismEngine == DismEngineChoice::Host  ? "host"
                                      : dismEngine == DismEngineChoice::Adk ? "adk"
                                                                            : "auto"},
                       {"esdThreads", esdThreads},
                       {"presetInIso", presetInIso},
                       {"updateGuard", updateGuard},
                       {"checkUpdates", checkUpdates},
                       {"updateCheckedAt", updateCheckedAt},
                       {"updateLatestTag", utf8::fromWide(updateLatestTag)},
                       {"updateNotifiedTag", utf8::fromWide(updateNotifiedTag)}};
    {
        const auto& w = windowsDownload;
        auto& out = doc["windowsDownload"] = nlohmann::json{{"updates", w.updates}, {"edge", w.edge}, {"apps", w.apps},
                                                            {"netFx3", w.netFx3}, {"resetBase", w.resetBase}, {"esd", w.esd}};
        auto& list = out["excludedApps"] = nlohmann::json::array();
        for (const auto& id : w.excludedApps) {
            list.push_back(utf8::fromWide(id));
        }
    }
    if (guards) {
        auto& list = doc["guards"] = nlohmann::json::array();
        for (const auto& id : *guards) {
            list.push_back(utf8::fromWide(id));
        }
    }
    if (guardsSeen) {
        auto& list = doc["guardsSeen"] = nlohmann::json::array();
        for (const auto& id : *guardsSeen) {
            list.push_back(utf8::fromWide(id));
        }
    }
    const std::string json = doc.dump(2);
    if (auto written = writeFileAtomic(file, json); !written) {
        log::warn("app", L"settings not saved: " + describe(written.error()));
    }
}

} // namespace wl::app
