#pragma once
// User settings persisted in %LOCALAPPDATA%\WinLove\settings.json (edited on P16):
// appearance (theme, reduced motion), interface language, and the work environment — where ISOs
// are copied before mounting and where images are mounted.
// Default work root: %LOCALAPPDATA%\WinLove (next to logs and settings; nothing in the root of C:).
#include "app/Localization.h"
#include "ui/theme/Palette.h"

#include <cstdint>
#include <filesystem>

namespace wl::app {

enum class ThemeChoice : std::uint8_t { Dark, Light, HighContrast, System };

struct AppSettings {
    ThemeChoice theme = ThemeChoice::Dark;
    ui::Accent accent = ui::Accent::Copper;
    bool reduceMotion = false; // true: always; false: follow the Windows "show animations" setting
    Language language = Language::Turkish;
    std::filesystem::path workRoot = defaultWorkRoot();
    std::filesystem::path mountFolder; // empty: <workRoot>\mount
    std::filesystem::path isoFolder;   // last "ISO Oluştur" output folder (empty: Desktop)

    [[nodiscard]] bool operator==(const AppSettings&) const = default;
    [[nodiscard]] std::filesystem::path mountDirectory() const {
        return mountFolder.empty() ? workRoot / L"mount" : mountFolder;
    }
    // Extracted setup media for a source, e.g. ...\WinLove\work\Win11_25H2_Turkish_x64_v2
    [[nodiscard]] std::filesystem::path workDirectoryFor(const std::filesystem::path& source) const {
        return workRoot / L"work" / source.stem();
    }

    // True for a folder WinLove itself extracted setup media into: something under <workRoot>\work
    // (or under the work folder of builds before 2026-09-28, C:\WinLove\work). Only such a folder
    // may be deleted by the app; anything else is the user's own.
    [[nodiscard]] bool isWorkCopy(const std::filesystem::path& folder) const;

    [[nodiscard]] static std::filesystem::path defaultFile();
    [[nodiscard]] static std::filesystem::path defaultWorkRoot();
    // Mount folder of builds before 2026-09-28 (C:\WinLove\mount): still checked at startup so
    // an image mounted there is restored and can be unmounted.
    [[nodiscard]] static std::filesystem::path legacyMountDirectory() { return L"C:\\WinLove\\mount"; }
    [[nodiscard]] static AppSettings load(const std::filesystem::path& file); // missing/corrupt → defaults
    void save(const std::filesystem::path& file) const;
};

} // namespace wl::app
