#pragma once
// 7TSP icon packs (the "7tsp GUI" format most Windows icon themes ship in): an archive or folder
// with Pack.ini ("[Base Pack]" Pack= / Base by=) and Resources\<target>.res — one compiled
// resource file (.res, what rc.exe writes) per Windows file it changes: "imageres.dll.mun.res",
// "shell32.dll.mun.res", "explorer.exe.mui.res" … The resources replace the target's own by type /
// name; the packs WinLove reads carry icons (RT_GROUP_ICON + RT_ICON), so each group becomes an
// .ico in WinLove's pack layout (IconPatchController: <file>\<id>.ico) and goes through the same
// checked patch path as any other replacement (IconPatch.h). Other resource types are ignored.
#include "base/Result.h"
#include "core/image/icons/PeResources.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// A .res file → the same tree a PE's resource section gives (types → names → languages). The
// leading empty entry rc.exe writes is skipped; every header and size is bounds-checked.
[[nodiscard]] Result<ResourceTree> parseResFile(std::string_view bytes);
// The tree as a .res file (what rc.exe writes; parseResFile reads it back the same).
[[nodiscard]] std::string writeResFile(const ResourceTree& tree);

struct SevenTspPack {
    std::wstring name;   // Pack.ini "Pack=", else the folder's name
    std::wstring author; // "Base by=" / "theme creator="
    struct File {
        std::wstring target; // "imageres.dll.mun" (the .res name without ".res")
        std::filesystem::path res;
    };
    std::vector<File> files;
};

// A folder that looks like a 7TSP pack: Pack.ini or a Resources folder with .res files, at its
// root or one folder down (archives often wrap everything in a folder named after the pack).
// NotFound when it is not one.
[[nodiscard]] Result<SevenTspPack> read7tspPack(const std::filesystem::path& folder);
[[nodiscard]] bool is7tspPack(const std::filesystem::path& folder);

// Each icon group of each .res as <out>\<target>\<key>.ico (key "3" or the group's name) plus an
// iconpack.json with the name and author. `skipped` gets "<target>: <reason>" for files without
// icons or that could not be read. Returns how many icons were written.
[[nodiscard]] Result<int> convert7tspPack(const SevenTspPack& pack, const std::filesystem::path& out,
                                          std::vector<std::wstring>* skipped = nullptr);

// A file name for a group key: "3" or the name when every character is safe in a file name (a
// name that is not stays out of the pack: nullopt-like empty string).
[[nodiscard]] std::wstring packFileStem(const ResourceKey& key);

// .7z / .zip / .rar … → `out` (created). Windows' own tar.exe (libarchive: 7z and zip on Windows
// 11) first, then 7-Zip when it is installed. Entries cannot leave `out` (both tools refuse
// absolute paths and "..").
[[nodiscard]] Result<void> extractArchive(const std::filesystem::path& archive, const std::filesystem::path& out);

} // namespace wl::core
