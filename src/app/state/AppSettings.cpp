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
    if (const auto root = doc.value("workRoot", std::string{}); !root.empty()) {
        settings.workRoot = utf8::toWide(root);
    }
    if (const auto iso = doc.value("isoFolder", std::string{}); !iso.empty()) {
        settings.isoFolder = utf8::toWide(iso);
    }
    return settings;
}

void AppSettings::save(const std::filesystem::path& file) const {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << nlohmann::json{{"version", 1},
                          {"workRoot", utf8::fromWide(workRoot.wstring())},
                          {"isoFolder", utf8::fromWide(isoFolder.wstring())}}
               .dump(2);
}

} // namespace wl::app
