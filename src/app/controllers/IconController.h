#pragma once
// D-065 Simgeler: which Windows icons the image gets from an icon pack (IconCatalog.h). A slot with
// an icon is a CopyFile into ProgramData\WinLove\Icons\<slot>.ico plus the registry values that
// point the shell at it; resetting a slot takes them out of the queue. The shortcut arrow can be
// removed: a transparent icon is written into the work folder and assigned to that slot.
#include "app/catalog/IconCatalog.h"
#include "app/state/AppState.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace wl::app {

class IconController {
public:
    explicit IconController(AppState& state) : m_state(state) {}

    // The .ico on this PC queued for `slot`, if any.
    [[nodiscard]] std::optional<std::filesystem::path> assigned(const IconSlot& slot) const;
    // Refused when the file is not an icon (.ico with an icon header, at most 4 MiB).
    Result<void> assign(const IconSlot& slot, const std::filesystem::path& icon);
    void reset(const IconSlot& slot);
    void resetAll();

    struct PackResult {
        std::wstring name;
        int matched = 0;
        std::vector<std::wstring> unmatched;
    };
    // Every slot the pack has an icon for is assigned; the others stay as they are.
    Result<PackResult> applyPack(const std::filesystem::path& folder);

    [[nodiscard]] bool shortcutArrowRemoved() const;
    Result<void> setShortcutArrowRemoved(bool removed);

    // What the image shows now, for the preview: the DLL (the mounted image's own System32 when it
    // is there, else this PC's) and the PrivateExtractIcons index.
    [[nodiscard]] std::pair<std::filesystem::path, int> defaultIcon(const IconSlot& slot) const;
    [[nodiscard]] int changedCount() const; // slots with an icon (nav badge)

    // Pure (unit-tested): the operations of a slot with `icon`, and the place in the image.
    [[nodiscard]] static std::vector<core::ops::Operation> operationsFor(const IconSlot& slot, const std::filesystem::path& icon);
    [[nodiscard]] static std::wstring imageFile(const IconSlot& slot); // "ProgramData\WinLove\Icons\this-pc.ico"
    [[nodiscard]] static core::ops::Operation themeKeepsIcons();       // ThemeChangesDesktopIcons = 0 (first logon)

private:
    [[nodiscard]] std::filesystem::path blankIcon() const; // <work>\icons\blank.ico

    AppState& m_state;
};

} // namespace wl::app
