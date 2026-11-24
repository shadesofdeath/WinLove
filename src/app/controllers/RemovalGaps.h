#pragma once
// D-153 (2026-11-24, the owner's pick): a removed app leaves a gap — something of Windows still points
// at it. Queuing the removal also queues the fix, in the same undo step; taking the removal out of the
// queue takes the fix out too (as long as it is still what was queued for it). The fixes are ordinary
// queue edits the pages show (Ayarlar, Uygulamalar › Varsayılanlar): the user can change them there.
//
//   Snipping Tool (Microsoft.ScreenSketch) → Print Screen copies the screen to the clipboard
//                                            (Windows 11 opens the tool with it; without it, nothing)
//   Photos (Microsoft.Windows.Photos)      → the classic Photo Viewer is registered and opens
//                                            JPEG / PNG / GIF / BMP for new users
//   Notepad (Microsoft.WindowsNotepad)     → .txt opens with the classic Notepad (when the image has
//                                            Windows\System32\notepad.exe)
//
// Only slots the queue leaves empty are filled: what the user chose for the same setting stays.
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/generated/StringKeys.g.h"
#include "app/state/AppState.h"
#include "core/image/dism/DefaultApps.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

class RemovalGaps final : public QueueCompanion {
public:
    // `mountDir`: the mounted image's folder (empty: none) — a fix that needs a file of the image
    // looks there.
    RemovalGaps(const ImageSettingsCatalog& settings, std::function<std::filesystem::path()> mountDir);

    // The identity names ("Microsoft.ScreenSketch") of the queued app removals that have a fix.
    [[nodiscard]] std::vector<std::wstring> watched(const core::ops::ChangeSet& changes) const override;
    void reconcile(core::ops::ChangeSet& changes, const std::vector<std::wstring>& before) const override;

    // What removing this app (a RemoveAppx target or its identity name) fixes — the component card's
    // note. Empty: no gap.
    [[nodiscard]] std::vector<Str> fixesOf(std::wstring_view app) const;
    // "Microsoft.ScreenSketch_11.2.0.0_x64__8wekyb3d8bbwe" → "Microsoft.ScreenSketch".
    [[nodiscard]] static std::wstring identityOf(std::wstring_view target);

private:
    struct Fix {
        std::wstring app;   // identity name
        Str what;           // the card's words
        std::vector<core::ops::Operation> ops;              // settings' operations (empty slots only)
        std::vector<core::AppAssociation> associations;     // merged into SetDefaultApps
        std::filesystem::path needs;                        // a file of the image it needs (relative), or empty
    };
    [[nodiscard]] const Fix* fixOf(std::wstring_view identity) const;
    [[nodiscard]] bool available(const Fix& fix) const;
    static void addFix(core::ops::ChangeSet& changes, const Fix& fix);
    static void removeFix(core::ops::ChangeSet& changes, const Fix& fix);

    std::vector<Fix> m_fixes;
    std::function<std::filesystem::path()> m_mountDir;
};

} // namespace wl::app
