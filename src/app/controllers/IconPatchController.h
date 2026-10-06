#pragma once
// D-068 Simgeler › Sistem simgeleri: the icon files of the mounted image (Windows\SystemResources\
// *.mun — where Windows 10 1903+ keeps the icons of imageres, shell32, DDORes …), their icon groups,
// and the replacements the user picked. One PatchIcons operation per file in the queue
// (core/image/icons/IconPatch.h): {"groups": {"#3": source, …}} or {"restore": true}.
//
// Icon packs (patch kind): a folder with one subfolder per file — "imageres.dll" (also "imageres"
// or "imageres.dll.mun") holding "<id>.ico|png" ("3.ico", "#3.png") or "<NAME>.ico" for named
// groups — and / or an iconpack.json: {"name", "author", "files": {"imageres.dll": {"3": "a.ico"}}}.
// exportPack writes the queued replacements in that layout.
//
// 7TSP packs (core/image/icons/ResFile.h) — a folder, or an archive (.7z / .zip) importPack opens —
// are turned into that layout under the pack root (%LOCALAPPDATA%\WinLove\IconPacks\<pack name>),
// which the queued operations then point at. Groups the image's file does not have, and files
// WinLove does not patch (a program's own .dll, a .mui), are reported as unmatched, never queued.
#include "app/state/AppSettings.h"
#include "app/state/AppState.h"
#include "core/image/icons/IconPatch.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wl::app {

class IconPatchController {
public:
    IconPatchController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~IconPatchController();
    IconPatchController(const IconPatchController&) = delete;
    IconPatchController& operator=(const IconPatchController&) = delete;

    struct File {
        std::wstring relative; // "Windows\SystemResources\imageres.dll.mun"
        std::wstring name;     // "imageres.dll"
        std::uint64_t bytes = 0;
    };
    struct Group {
        core::ResourceKey key;
        int index = 0;       // PrivateExtractIcons index (≥ 0) — the preview
        int images = 0;
        int largest = 0;     // px
    };

    // The mounted image's icon files, by name (read when first asked for, per mount).
    [[nodiscard]] const std::vector<File>& files();
    // A file's groups; nullptr while they are being read (onLoaded fires when they are there).
    [[nodiscard]] const std::vector<Group>* groups(const std::wstring& relative);
    [[nodiscard]] std::filesystem::path pathInImage(const std::wstring& relative) const;
    // Patched by an earlier Uygula (its original is in Windows\WinLove\IconBackup).
    [[nodiscard]] bool patchedInImage(const std::wstring& relative);
    std::function<void()> onLoaded;
    // Reads a file's groups now, on this thread (render demos; the page reads in the background).
    void preload(const std::wstring& relative);

    // ---- the queue ----
    [[nodiscard]] std::optional<std::filesystem::path> replacement(const std::wstring& relative, const core::ResourceKey& key) const;
    // Refused (InvalidArgument) when `source` is not an icon or a picture Windows can read.
    Result<void> replace(const std::wstring& relative, const core::ResourceKey& key, const std::filesystem::path& source);
    void revert(const std::wstring& relative, const core::ResourceKey& key);
    [[nodiscard]] bool restoreQueued(const std::wstring& relative) const;
    void setRestore(const std::wstring& relative, bool restore); // the image's original back (Uygula)
    void revertFile(const std::wstring& relative);               // the file's operation out of the queue
    void resetAll();
    [[nodiscard]] int changedIn(const std::wstring& relative) const; // replaced groups (restore: 1)
    [[nodiscard]] int changedCount() const;

    // ---- packs and files of this PC ----
    struct PackResult {
        std::wstring name;
        int matched = 0;
        std::vector<std::wstring> unmatched; // entries naming a file / group the image does not have
    };
    Result<PackResult> applyPack(const std::filesystem::path& folder);
    // A folder (as applyPack) or an archive of either kind of pack.
    Result<PackResult> importPack(const std::filesystem::path& source);
    // Where converted / extracted packs are kept (tests point it elsewhere).
    void setPackRoot(std::filesystem::path root) { m_packRoot = std::move(root); }
    // The queued replacements as a pack in `folder` (created); how many icons were written.
    Result<int> exportPack(const std::filesystem::path& folder) const;
    // A group of the image's file as a .ico of this PC.
    Result<void> exportOriginal(const std::wstring& relative, const core::ResourceKey& key, const std::filesystem::path& out) const;

    // Pure (unit-tested).
    [[nodiscard]] static core::ops::Operation operationFor(const std::wstring& relative, const core::IconPatchRequest& request);
    [[nodiscard]] static bool isIconSource(const std::filesystem::path& file);
    // "imageres.dll" / "imageres" / "imageres.dll.mun" (any case) → the file of `files` it names.
    [[nodiscard]] static const File* fileNamed(const std::vector<File>& files, std::wstring_view name);
    // "3", "#3", "3.ico" stem … → key; anything not a number is a name.
    [[nodiscard]] static core::ResourceKey keyFromPackName(std::wstring_view stem);

private:
    [[nodiscard]] std::optional<core::IconPatchRequest> request(const std::wstring& relative) const;
    void store(const std::wstring& relative, const core::IconPatchRequest& request);
    void forgetMount();
    Result<PackResult> apply7tsp(const std::filesystem::path& folder);

    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::size_t m_subscription = 0;
    std::filesystem::path m_mountDir; // what the caches below belong to
    std::optional<std::vector<File>> m_files;
    std::optional<std::set<std::wstring>> m_patched;
    std::map<std::wstring, std::vector<Group>> m_groups;
    std::set<std::wstring> m_loading;
    std::filesystem::path m_packRoot = AppSettings::defaultWorkRoot() / L"IconPacks";
};

} // namespace wl::app
