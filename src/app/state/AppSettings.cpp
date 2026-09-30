#include "app/state/AppSettings.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <fstream>
#include <sstream>

namespace wl::app {

std::filesystem::path AppSettings::defaultFile() {
    return log::defaultDirectory().parent_path() / L"settings.json";
}

std::filesystem::path AppSettings::defaultWorkRoot() {
    return log::defaultDirectory().parent_path(); // %LOCALAPPDATA%\WinLove
}

AppSettings AppSettings::load(const std::filesystem::path& file) {
    AppSettings settings;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return settings;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto doc = nlohmann::json::parse(buffer.str(), nullptr, /*allow_exceptions=*/false);
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
    settings.language = text("language") == "en" ? Language::English : Language::Turkish;
    if (const auto it = doc.find("reduceMotion"); it != doc.end() && it->is_boolean()) {
        settings.reduceMotion = it->get<bool>();
    }
    return settings;
}

void AppSettings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    static constexpr const char* kThemes[] = {"dark", "light", "hc", "system"};
    out << nlohmann::json{{"version", 1},
                          {"theme", kThemes[static_cast<std::size_t>(theme)]},
                          {"reduceMotion", reduceMotion},
                          {"language", language == Language::English ? "en" : "tr"},
                          {"workRoot", utf8::fromWide(workRoot.wstring())},
                          {"mountFolder", utf8::fromWide(mountFolder.wstring())},
                          {"isoFolder", utf8::fromWide(isoFolder.wstring())}}
               .dump(2);
}

} // namespace wl::app
