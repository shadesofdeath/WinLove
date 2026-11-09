#pragma once
// User settings persisted in %LOCALAPPDATA%\WinLove\settings.json (edited on P16):
// appearance (theme, reduced motion), interface language, and the work environment — where ISOs
// are copied before mounting and where images are mounted.
// Default work root: %LOCALAPPDATA%\WinLove (next to logs and settings; nothing in the root of C:).
#include "app/Localization.h"
#include "ui/text/TextStyles.h"
#include "ui/theme/Palette.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

// D-114: "Saf siyah" took the high contrast choice's place (Windows' own high contrast still wins).
enum class ThemeChoice : std::uint8_t { Dark, Light, Black, System };
// How large the interface is drawn, on top of the monitor's DPI (P16 "yoğunluk"): compact is the
// design's 24px rows; the others scale everything, for big screens or tired eyes.
enum class Density : std::uint8_t { Compact, Comfortable, Large };
// D-123: Ayarlar › DISM motoru — Auto: this PC's, the Windows ADK's when a part of it is missing.
enum class DismEngineChoice : std::uint8_t { Auto, Host, Adk };
// D-092: the interface's type family (and its monospace companion).
enum class UiFont : std::uint8_t { Inter, Geist, SegoeVariable };
[[nodiscard]] constexpr ui::FontFamilies fontFamilies(UiFont font) noexcept {
    switch (font) {
    case UiFont::Geist: return {L"Geist", L"Segoe UI", L"Geist Mono", L"Cascadia Mono"};
    case UiFont::SegoeVariable: return {L"Segoe UI Variable Text", L"Segoe UI", L"Cascadia Mono", L"Consolas"};
    case UiFont::Inter: break;
    }
    return {L"Inter", L"Segoe UI", L"JetBrains Mono", L"Cascadia Mono"};
}
[[nodiscard]] constexpr float densityScale(Density d) noexcept {
    return d == Density::Large ? 1.25f : d == Density::Comfortable ? 1.125f : 1.0f;
}

// D-093: what "Windows indir" makes of a download — kept between runs, like uup-converter's
// ConvertConfig.ini and CustomAppsList.txt.
struct WindowsDownloadPrefs {
    bool updates = true;
    bool edge = true;
    bool apps = true;
    bool netFx3 = false;
    bool resetBase = false;
    bool esd = false;
    std::vector<std::wstring> excludedApps; // Store app ids left out

    [[nodiscard]] bool operator==(const WindowsDownloadPrefs&) const = default;
};

struct AppSettings {
    ThemeChoice theme = ThemeChoice::Dark;
    ui::Accent accent = ui::Accent::Blue;
    bool reduceMotion = false; // true: always; false: follow the Windows "show animations" setting
    Density density = Density::Compact;
    UiFont font = UiFont::SegoeVariable; // D-107: the WinLoveUI package's family
    std::vector<int> closedNavGroups; // navigation groups folded away (PageInfo navGroup)
    Language language = Language::Turkish;
    std::filesystem::path workRoot = defaultWorkRoot();
    std::filesystem::path mountFolder; // empty: <workRoot>\mount
    std::filesystem::path isoFolder;   // last "ISO Oluştur" output folder (empty: Desktop)
    // D-082: the compatibility guards that are on (compat.json ids). Never chosen: the catalog's defaults.
    std::optional<std::vector<std::wstring>> guards;
    // D-120: the guards the catalog had when `guards` was saved. A guard added later is not in it and
    // takes its catalog default (absent with `guards`: the guards before D-120).
    std::optional<std::vector<std::wstring>> guardsSeen;
    WindowsDownloadPrefs windowsDownload;
    bool pauseBeforeSave = false; // D-106: Uygula stops before each save for changes by hand in the mount folder
    // 2026-11-09 (D-123, the reference tool's Tool settings): which DISM WinLove loads (read once, at start),
    // how many logical processors an ESD / LZMS export may use (0: as Windows decides), and whether
    // ISO Oluştur writes the last applied preset into the ISO's root.
    DismEngineChoice dismEngine = DismEngineChoice::Auto;
    int esdThreads = 0;
    bool presetInIso = true;
    // D-125: Apply ends with the update guard (core/ops/UpdateGuard.h). Off until chosen: it
    // changes the installed Windows after updates, so the user switches it on knowingly.
    bool updateGuard = false;

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
