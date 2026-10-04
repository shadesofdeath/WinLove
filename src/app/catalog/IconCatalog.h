#pragma once
// D-065: the Windows icons an icon pack can replace in an image, and how a pack's files are matched
// to them. Nothing in Windows' own files is patched (imageres.dll / shell32.dll resource editing
// breaks the component store and the next cumulative update puts the originals back): the icons
// are copied to ProgramData\WinLove\Icons and the shell is pointed at them —
//   desktop items   HKLM\SOFTWARE\Classes\CLSID\{…}\DefaultIcon (every user) and the per-user
//                   Explorer\CLSID\{…}\DefaultIcon a theme writes (default profile, again at first
//                   logon); "ThemeChangesDesktopIcons" = 0 keeps a theme from putting its own back
//   shell icons     HKLM\…\Explorer\Shell Icons "<index>" (folders, drives, the shortcut arrow)
//   system drive    HKLM\…\Explorer\DriveIcons\C\DefaultIcon
// A pack is a folder of .ico files named after the slots (aliases below, any case, spaces / dashes
// ignored), or with an iconpack.json: {"name": "…", "author": "…", "icons": {"this-pc": "pc.ico", …}}.
#include "app/Localization.h"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct IconSlot {
    enum class Group : std::uint8_t { Desktop, Explorer };
    std::string_view id;           // "this-pc"
    Group group;
    Str name;
    std::wstring_view defaultFile; // where Windows takes it from (preview), relative to System32
    int defaultIndex;              // PrivateExtractIcons index: < 0 resource id, >= 0 position
    std::wstring_view clsid;       // desktop items: "{20D04FE0-…}"
    std::wstring_view clsidValue;  // "" = (Default); recycle bin: "Empty" / "Full"
    std::wstring_view shellIcon;   // Shell Icons value name ("3"); "" = none
    bool systemDrive = false;      // DriveIcons\C
    std::span<const std::wstring_view> aliases;
};

[[nodiscard]] std::span<const IconSlot> iconSlots() noexcept;
[[nodiscard]] const IconSlot* findIconSlot(std::string_view id) noexcept;

// "This PC.ico" → "thispc": what a file name is compared by.
[[nodiscard]] std::wstring iconAliasKey(std::wstring_view stem);

struct IconPack {
    std::wstring name;   // the manifest's, else the folder's name
    std::wstring author;
    std::map<std::string, std::filesystem::path> icons; // slot id → .ico
    std::vector<std::wstring> unmatched;                 // .ico files no slot took
};
// Reads a pack folder (not recursive beyond one level of subfolders). Errors: not a folder, or a
// manifest that is not valid JSON.
[[nodiscard]] Result<IconPack> readIconPack(const std::filesystem::path& folder);

// Is this a usable icon file: .ico, ICO header, at most 4 MiB.
[[nodiscard]] bool isIconFile(const std::filesystem::path& file);

} // namespace wl::app
