#pragma once
// D-155 Sağ tık menüsü: what the image's Explorer puts in its right-click menus, read from the image
// itself, and the two edits WinLove makes there.
//
// Read (machine-wide, SOFTWARE\Classes of the image) for the shell's common places — files (*),
// files and folders (AllFilesystemObjects), folders (Directory, Folder), a folder's background
// (Directory\Background), drives (Drive), the desktop (DesktopBackground):
//   Verb     — <place>\shell\<verb>: its name (MUIVerb or the default value, "@dll,-id" resolved in
//              the image's language), its command; Extended (Shift only), already hidden
//              (ProgrammaticAccessOnly / LegacyDisable);
//   Handler  — <place>\shellex\ContextMenuHandlers\<name>: a COM extension by its CLSID, named by
//              the CLSID's default value; one row per CLSID (it may sit in several places);
//   Packaged — a provisioned app's Windows 11 menu item (AppxManifest.xml: desktop4/5
//              FileExplorerContextMenus, ItemType → Verb Id + Clsid): "Open in Terminal" and the like.
//
// Hide: a verb gets ProgrammaticAccessOnly (still runs when a program asks for it); a handler or a
// packaged item is blocked by its CLSID (HKLM\…\Shell Extensions\Blocked — everywhere it is used).
// Add: our own verb in the chosen places, "WinLove.<id>" keys with the name, an optional icon, Shift
// only (Extended) and the command (%1 the clicked item, %V the folder of a background click).
// Windows 11 lists classic items under "Show more options" unless the classic menu is on (Ayarlar).
//
// In the queue: OpKind::SetContextMenu, target "hide\<entry id>" or "add\<id>", value JSON
// {"name", "command"?, "places"?, "icon"?, "extended"?, "writes": [{key, name, value}]} — the writes
// resolved when queued, as for policies (D-154).
#include "base/Result.h"
#include "core/image/RegistryEdit.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct ContextMenuEntry {
    enum class Kind : std::uint8_t { Verb, Handler, Packaged };
    Kind kind = Kind::Verb;
    std::wstring id;          // Verb: "<place>\shell\<verb>"; Handler / Packaged: "{CLSID}" (upper case)
    std::wstring name;        // what the menu says (resolved), or the key's name
    std::vector<std::wstring> places; // "*", "Directory\Background" … (Handler: every place it is in)
    std::wstring keyName;     // the verb or handler key's name
    std::wstring clsid;       // Handler / Packaged
    std::wstring command;     // Verb: the command line
    std::wstring source;      // Handler: its DLL; Packaged: the package's identity name
    bool extended = false;    // shown with Shift only
    bool hidden = false;      // the image already hides it
};

// The places WinLove reads and writes, in the order the page shows them.
[[nodiscard]] const std::vector<std::wstring>& contextMenuPlaces();

[[nodiscard]] Result<std::vector<ContextMenuEntry>> readContextMenu(const std::filesystem::path& mountDir);
// The packaged (Windows 11) items alone, from <mount>\Program Files\WindowsApps (none hidden; tests).
[[nodiscard]] std::vector<ContextMenuEntry> readPackagedContextMenu(const std::filesystem::path& mountDir);

// The writes that hide an entry (empty for one already hidden), and those that show one the image
// hides (its ProgrammaticAccessOnly / LegacyDisable value or its Blocked value deleted).
[[nodiscard]] std::vector<RegistryWrite> contextMenuHideWrites(const ContextMenuEntry& entry);
[[nodiscard]] std::vector<RegistryWrite> contextMenuShowWrites(const ContextMenuEntry& entry);

struct ContextMenuItem {
    std::wstring id;       // "WinLove.<id>": letters, digits, '-', '.' (validated)
    std::wstring name;     // what the menu says
    std::wstring command;  // the command line
    std::wstring icon;     // optional: "C:\…\x.exe,0" / a .ico path in the installed Windows
    std::vector<std::wstring> places; // of contextMenuPlaces()
    bool extended = false;
    [[nodiscard]] bool operator==(const ContextMenuItem&) const = default;
};
// A key-safe id from a name ("Open with VS Code" → "open-with-vs-code"); never empty.
[[nodiscard]] std::wstring contextMenuItemId(std::wstring_view name);
// Errors: no name / command / place, an unknown place, a bad id.
[[nodiscard]] Result<std::vector<RegistryWrite>> contextMenuAddWrites(const ContextMenuItem& item);

struct ContextMenuOperation {
    std::wstring name;
    std::optional<ContextMenuItem> item; // "add\…" operations
    std::vector<RegistryWrite> writes;
};
[[nodiscard]] std::wstring contextMenuOperationJson(const ContextMenuOperation& op);
[[nodiscard]] Result<ContextMenuOperation> parseContextMenuOperation(std::wstring_view json);

} // namespace wl::core
