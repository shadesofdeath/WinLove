#pragma once
// User settings persisted in %LOCALAPPDATA%\WinLove\settings.json (edited on P16).
// For now: the work root — where ISOs are copied before mounting and where images are mounted.
#include <filesystem>

namespace wl::app {

struct AppSettings {
    std::filesystem::path workRoot = L"C:\\WinLove";

    [[nodiscard]] std::filesystem::path mountDirectory() const { return workRoot / L"mount"; }
    // Extracted setup media for a source, e.g. C:\WinLove\work\Win11_25H2_Turkish_x64_v2
    [[nodiscard]] std::filesystem::path workDirectoryFor(const std::filesystem::path& source) const {
        return workRoot / L"work" / source.stem();
    }

    [[nodiscard]] static std::filesystem::path defaultFile();
    [[nodiscard]] static AppSettings load(const std::filesystem::path& file); // missing/corrupt → defaults
    void save(const std::filesystem::path& file) const;
};

} // namespace wl::app
