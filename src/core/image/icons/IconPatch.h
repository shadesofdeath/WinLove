#pragma once
// D-068: changing icons inside Windows' own files — the "patch" mode of the Simgeler page.
//
// Safety, in order (each step must hold or nothing is written):
//   1. Which files: under Windows\ only, never the component store (WinSxS, servicing) or boot
//      files, and only resource containers — a PE without code (the .mun files of
//      Windows\SystemResources, resource-only DLLs). A program's own file (code) is refused:
//      its catalog hash breaks and Smart App Control / WDAC may then refuse to run it.
//   2. The first patch copies the original to Windows\WinLove\IconBackup\<path under Windows>;
//      later patches build on the file as it is (icons changed by an earlier Uygula stay) and never
//      touch the backup, so "restore" always gives Microsoft's file back.
//   3. The new bytes are checked by our parser (every resource that was not replaced is
//      byte-identical, replaced groups hold exactly the new images) and then by Windows itself:
//      LoadLibraryEx(AS_IMAGE_RESOURCE) + CreateIconFromResourceEx for every image of every group.
//   4. The file is written beside the target under a temporary name with the original's owner
//      (TrustedInstaller), group and DACL, then renamed over it. The rename replaces only this
//      name: the WinSxS hard link keeps the original, the component store stays consistent
//      (DISM /ScanHealth clean, later updates install; an update or sfc may put the original back).
//   5. Windows\WinLove\IconBackup\restore-icons.cmd puts every original back — on the installed
//      system or from WinRE / setup media (run it from <drive>:\Windows\WinLove\IconBackup).
#include "base/Result.h"
#include "core/image/icons/IconGroups.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct IconReplacement {
    ResourceKey group;
    std::vector<IconImage> images;
};

// Original bytes + replacements → the new file, checked by our parser (see 3.). The PE must
// have resources and the groups must exist.
[[nodiscard]] Result<std::string> patchIconBytes(const std::string& original, const std::vector<IconReplacement>& replacements);

// Windows' loader on `file`: how many icon groups it found and how many images failed to load.
struct IconLoadCheck {
    int groups = 0;
    int images = 0;
    int failed = 0;
    std::wstring firstFailure;
};
[[nodiscard]] Result<IconLoadCheck> verifyIconFileWithWindows(const std::filesystem::path& file);

// Rule 1 for a path under the image root ("Windows\SystemResources\imageres.dll.mun") and its
// parsed PE. InvalidArgument with the reason when refused.
[[nodiscard]] Result<void> checkIconPatchTarget(std::wstring_view relative, const PeImage& pe);
[[nodiscard]] Result<void> checkIconPatchPath(std::wstring_view relative);

// Image-root path of the backup copy ("Windows\WinLove\IconBackup\SystemResources\imageres.dll.mun").
[[nodiscard]] std::wstring iconBackupPath(std::wstring_view relative);

// Steps 1–5 on a mounted image (elevated: SeBackup / SeRestore). Each replacement's .ico comes
// from this PC. Returns the backup path.
[[nodiscard]] Result<std::wstring> patchImageIcons(const std::filesystem::path& mountDir, std::wstring_view relative,
                                                   const std::vector<IconReplacement>& replacements);
// The backup back in place (same write path as a patch); NotFound when there is no backup.
[[nodiscard]] Result<void> restoreImageIcons(const std::filesystem::path& mountDir, std::wstring_view relative);
// Every file with a backup ("Windows\SystemResources\imageres.dll.mun"), for the page and the script.
[[nodiscard]] std::vector<std::wstring> patchedIconFiles(const std::filesystem::path& mountDir);

// ---- the queue: one PatchIcons operation per file -----------------------------------------------
// target = the file's path from the image root, value = JSON:
//   {"groups": {"#3": "C:\\Icons\\folder.ico", "ICO_MYCOMPUTER": "D:\\pc.png"}}  or  {"restore": true}
// Sources are files of this PC (.ico, or any picture: loadIconSource).
struct IconPatchRequest {
    bool restore = false;
    std::vector<std::pair<ResourceKey, std::filesystem::path>> groups;
};
[[nodiscard]] std::wstring iconPatchValue(const IconPatchRequest& request);
[[nodiscard]] Result<IconPatchRequest> iconPatchRequest(std::wstring_view value);
// "#3" → id 3; anything else is a name.
[[nodiscard]] ResourceKey resourceKeyFromText(std::wstring_view text);
// The operation: sources loaded, then patchImageIcons / restoreImageIcons.
[[nodiscard]] Result<void> applyIconPatch(const std::filesystem::path& mountDir, std::wstring_view relative,
                                          const IconPatchRequest& request);

} // namespace wl::core
