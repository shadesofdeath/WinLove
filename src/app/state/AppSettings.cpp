#include "app/state/AppSettings.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

namespace wl::app {

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
                     : theme == "hc"     ? ThemeChoice::HighContrast
                     : theme == "system" ? ThemeChoice::System
                                         : ThemeChoice::Dark;
    const std::string accent = text("accent");
    settings.accent = accent == "sea"           ? ui::Accent::Sea
                      : accent == "pomegranate" ? ui::Accent::Pomegranate
                      : accent == "sky"         ? ui::Accent::Sky
                      : accent == "olive"       ? ui::Accent::Olive
                                                : ui::Accent::Copper;
    settings.language = text("language") == "en" ? Language::English : Language::Turkish;
    if (const auto it = doc.find("reduceMotion"); it != doc.end() && it->is_boolean()) {
        settings.reduceMotion = it->get<bool>();
    }
    const std::string font = text("font");
    settings.font = font == "geist" ? UiFont::Geist : font == "segoe" ? UiFont::SegoeVariable : UiFont::Inter;
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
    if (const auto it = doc.find("guards"); it != doc.end() && it->is_array()) {
        std::vector<std::wstring> guards;
        for (const auto& id : *it) {
            if (id.is_string() && !id.get<std::string>().empty()) {
                guards.push_back(utf8::toWide(id.get<std::string>()));
            }
        }
        settings.guards = std::move(guards);
    }
    return settings;
}

void AppSettings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    static constexpr const char* kThemes[] = {"dark", "light", "hc", "system"};
    static constexpr const char* kAccents[] = {"copper", "sea", "pomegranate", "sky", "olive"};
    static constexpr const char* kDensities[] = {"compact", "comfortable", "large"};
    static constexpr const char* kFonts[] = {"inter", "geist", "segoe"};
    // Through a temporary file: a crash mid-write must not reset every setting to its default.
    nlohmann::json doc{{"version", 1},
                       {"theme", kThemes[static_cast<std::size_t>(theme)]},
                       {"accent", kAccents[static_cast<std::size_t>(accent)]},
                       {"reduceMotion", reduceMotion},
                       {"density", kDensities[static_cast<std::size_t>(density)]},
                       {"font", kFonts[static_cast<std::size_t>(font)]},
                       {"closedNavGroups", closedNavGroups},
                       {"language", language == Language::English ? "en" : "tr"},
                       {"workRoot", utf8::fromWide(workRoot.wstring())},
                       {"mountFolder", utf8::fromWide(mountFolder.wstring())},
                       {"isoFolder", utf8::fromWide(isoFolder.wstring())}};
    if (guards) {
        auto& list = doc["guards"] = nlohmann::json::array();
        for (const auto& id : *guards) {
            list.push_back(utf8::fromWide(id));
        }
    }
    const std::string json = doc.dump(2);
    if (auto written = writeFileAtomic(file, json); !written) {
        log::warn("app", L"settings not saved: " + describe(written.error()));
    }
}

} // namespace wl::app
