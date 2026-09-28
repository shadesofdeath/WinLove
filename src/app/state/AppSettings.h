#pragma once
// User settings persisted in %LOCALAPPDATA%\WinLove\settings.json (edited on P16).
// For now: the work root — where ISOs are copied before mounting and where images are mounted.
// Default: %LOCALAPPDATA%\WinLove (next to logs and settings; nothing in the root of C:).
#include <filesystem>

namespace wl::app {

struct AppSettings {
    std::filesystem::path workRoot = defaultWorkRoot();

    [[nodiscard]] std::filesystem::path mountDirectory() const { return workRoot / L"mount"; }
    // Extracted setup media for a source, e.g. ...\WinLove\work\Win11_25H2_Turkish_x64_v2
    [[nodiscard]] std::filesystem::path workDirectoryFor(const std::filesystem::path& source) const {
        return workRoot / L"work" / source.stem();
    }

    [[nodiscard]] static std::filesystem::path defaultFile();
    [[nodiscard]] static std::filesystem::path defaultWorkRoot();
    // Mount folder of builds before 2026-09-28 (C:\WinLove\mount): still checked at startup so
    // an image mounted there is restored and can be unmounted.
    [[nodiscard]] static std::filesystem::path legacyMountDirectory() { return L"C:\\WinLove\\mount"; }
    [[nodiscard]] static AppSettings load(const std::filesystem::path& file); // missing/corrupt → defaults
    void save(const std::filesystem::path& file) const;
};

} // namespace wl::app
