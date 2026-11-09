#pragma once
// D-112: what the reference tool reads from an image for the dynamic rows of its component tree
// (research README §6.6), read the same way from the same places:
//   drivers     DRIVERS + SYSTEM hives DriverDatabase\DriverPackages (package → INF, class GUID);
//               DriverStore\FileRepository\<package>\<inf> for provider, DriverVer and the device
//               names of its [Models]; the class name from SYSTEM Control\Class\{GUID} (MUI resolved
//               through this PC's files), else the INF's Class.
//   languages   locale folders: Windows\System32\<tag>, SysWOW64\<tag>, Common Files\microsoft
//               shared\ink\<tag> (what the reference tool deleted for cy-GB).
//   keyboards   SYSTEM Control\Keyboard Layouts\<KLID>: Layout Display Name (MUI) / Layout Text,
//               Layout File.
//   fonts       SOFTWARE Windows NT\CurrentVersion\Fonts values, grouped by family.
//   system apps SOFTWARE Appx\AppxAllUserStore\InboxApplications\<full name> (Path → its
//               Windows\SystemApps folder); the manifest's DisplayName when it is plain text.
// And how each goes (the reference tool's methods, README §8) as a ComponentRecipe for RemoveComponent.
// Reading needs read access to the image's hive files (a mounted image: an elevated process).
#include "base/Result.h"
#include "core/image/SystemComponents.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct InventoryDriver {
    std::wstring package;   // DriverPackages key = FileRepository folder ("mdm3com.inf_amd64_60545ab840205b89")
    std::wstring inf;       // "mdm3com.inf"
    std::wstring classGuid; // upper case
    std::wstring className; // the image's class description ("Modemler"), else the INF's Class
    std::wstring provider;
    std::wstring version;
    std::wstring date;
    std::vector<std::wstring> devices;
    std::uint64_t size = 0; // its FileRepository folder
    bool classInf = false;  // c_*.inf: defines the class itself (never removed)
};

struct InventoryLanguage {
    std::wstring tag;                 // "cy-GB"
    std::wstring name;                // this PC's name for it ("Galce (Birleşik Krallık)")
    std::vector<std::wstring> paths;  // relative to the image root, the folders that exist
    std::uint64_t size = 0;
};

struct InventoryKeyboard {
    std::wstring klid;      // "0000041F"
    std::wstring name;      // "Türkçe Q"
    std::wstring file;      // "KBDTUQ.DLL"
    bool sharedFile = false; // another layout loads the same file: the file stays
};

struct InventoryFont {
    std::wstring family;              // "Gabriola"
    std::vector<std::wstring> values; // Fonts value names ("Gabriola (TrueType)")
    std::vector<std::wstring> files;  // Windows\Fonts file names, the ones not shared with another family
    std::uint64_t size = 0;
};

struct InventorySystemApp {
    std::wstring fullName;    // InboxApplications key
    std::wstring family;      // "Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy"
    std::wstring name;        // identity name ("Microsoft.Windows.PeopleExperienceHost")
    std::wstring displayName; // the manifest's, when it is plain text; else empty
    std::wstring folder;      // relative: "Windows\SystemApps\Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy"
    std::uint64_t size = 0;
};

struct ImageInventory {
    std::vector<InventoryDriver> drivers;
    std::vector<InventoryLanguage> languages;
    std::vector<InventoryKeyboard> keyboards;
    std::vector<InventoryFont> fonts;
    std::vector<InventorySystemApp> systemApps;
    std::vector<std::pair<std::wstring, std::wstring>> fontValues; // every Fonts value: name → file, as read
};

[[nodiscard]] Result<ImageInventory> readImageInventory(const std::filesystem::path& mountDir);

// "Arial Bold Italic (TrueType)" → "Arial"; "Cambria & Cambria Math (TrueType)" → "Cambria";
// "Courier 10,12,15" → "Courier".
[[nodiscard]] std::wstring fontFamily(std::wstring_view valueName);
// D-119: a family Windows' own interface and its web engines fall back to (Segoe UI and its icon
// fonts, Marlett, Arial, Times New Roman, Courier New, Tahoma, Microsoft Sans Serif, Consolas,
// Lucida Console, Symbol, Wingdings, Webdings and the classic .fon families). With these gone the
// setup screens' web engine stopped at once and Setup looped on "Why did my PC restart?" (VM,
// 2026-11-08): no removal takes them, whatever a recipe or a preset says.
[[nodiscard]] bool isCoreFontFamily(std::wstring_view family);
// "cy-GB", "sr-Latn-RS", "en-US": a locale name Windows knows.
[[nodiscard]] bool isLocaleTag(std::wstring_view name);

// The reference tool's removal (README §8) as recipes for a RemoveComponent operation.
[[nodiscard]] ComponentRecipe driverRecipe(const InventoryDriver& driver);
[[nodiscard]] ComponentRecipe languageRecipe(const InventoryLanguage& language);
[[nodiscard]] ComponentRecipe keyboardRecipe(const InventoryKeyboard& keyboard);
[[nodiscard]] ComponentRecipe fontRecipe(const InventoryFont& font);
[[nodiscard]] ComponentRecipe systemAppRecipe(const InventorySystemApp& app);

} // namespace wl::core
