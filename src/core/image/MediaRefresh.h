#pragma once
// D-080: the setup media's own files brought up to date, as Microsoft's media refresh does (steps
// 26–28): the Setup dynamic update is expanded over sources\, then setup.exe / setuphost.exe and the
// boot manager are taken from the updated Setup image (BootPatch::setupFilesTo) — a Setup or boot
// manager older than the image it boots makes the installation fail. Nothing in the setup folder
// changes: the result is a list of files the ISO / USB writer puts in place of the folder's.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct MediaFile {
    std::wstring path;          // in the media, from its root: L"sources\\setup.exe"
    std::filesystem::path file; // the new content
    bool isNew = false;         // the setup folder has no such file yet (the dynamic update brings it)
};

// Expands every file of the Setup dynamic update (.cab) into `folder` (expand.exe -F:*).
[[nodiscard]] Result<std::vector<std::filesystem::path>> expandSetupDynamicUpdate(const std::filesystem::path& cab,
                                                                                  const std::filesystem::path& folder,
                                                                                  const TaskContext& task);

// What replaces the setup folder's files: the expanded dynamic update under sources\ (`setupDu`,
// may be empty; its language folders only where the media has that language), then the files
// saved from the updated Setup image (`saved`, may be empty): its whole sources\ folder
// (saved\sources\…, new folders too), bootmgfw.efi for every bootmgfw / bootx64 / bootia32 /
// bootaa64.efi of the media, bootmgr.efi for every bootmgr.efi, boot.stl as efi\microsoft\boot\boot.stl.
// A later entry for the same path wins.
[[nodiscard]] std::vector<MediaFile> mediaRefreshFiles(const std::filesystem::path& setupFolder,
                                                       const std::filesystem::path& setupDu,
                                                       const std::filesystem::path& saved);

} // namespace wl::core
