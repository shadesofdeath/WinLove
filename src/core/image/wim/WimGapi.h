#pragma once
// WimgApiBackend (docs/ENGINE.md §1): export / delete editions through wimgapi.dll, loaded at
// runtime from System32 like dismapi.dll (the header ships only with the ADK — D-017).
// No admin needed: these write WIM files, they do not mount.
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace wl::core {

// Copies edition `index` of `source` (WIM or ESD) into `destination`: creates the file if missing,
// otherwise appends as a new index. ESD → WIM = export with Lzx (or Xpress) compression.
[[nodiscard]] Result<void> exportImage(const std::filesystem::path& source, int index,
                                       const std::filesystem::path& destination, WimCompression compression,
                                       const TaskContext& task);

// Rewrites `wim` without the streams no image refers to any more. A commit only appends: the old
// versions of changed files (the registry hives, above all) stay in the file, and 7-Zip lists them
// under "[DELETED]". Every image is exported in order, with the file's own compression, into a new
// file that replaces the old one once it is complete. Not for ESD / split / bootable WIMs (those
// are returned untouched).
[[nodiscard]] Result<void> optimizeWim(const std::filesystem::path& wim, const TaskContext& task);

// Removes the editions `indexes` from a WIM on disk and gives their space back: the editions that
// stay are exported, in order, into a new file that replaces the old one once it is complete, so a
// cancel or a failure leaves the original as it was. What stays is renumbered 1..n
// (indexAfterRemoval). A bootable WIM (boot.wim) loses the entries in place instead and keeps its
// size: an export would drop its boot index. Not for ESD / split images; one edition must stay.
[[nodiscard]] Result<void> removeImages(const std::filesystem::path& wim, std::span<const int> indexes,
                                        const TaskContext& task);

// The texts of an edition: what DISM and Setup's edition list show. Written into the image's XML
// (NAME / DISPLAYNAME, DESCRIPTION / DISPLAYDESCRIPTION, FLAGS); the file grows by the difference,
// nothing else in it changes. Control characters and outer spaces are dropped.
struct ImageText {
    std::wstring name;                 // 1 to 255 characters
    std::wstring description;          // may be empty
    std::optional<std::wstring> flags; // the edition id Setup matches keys against; untouched when not given
};
[[nodiscard]] Result<void> setImageText(const std::filesystem::path& wim, int index, const ImageText& text);

// Splits `source` into parts of at most `partSize` bytes — install.swm, install2.swm, … — what
// Setup reads from a FAT32 stick when install.wim is larger than 4 GB (D-047). `firstPart` names
// the first part; the rest go next to it. Returns the number of parts.
[[nodiscard]] Result<int> splitWim(const std::filesystem::path& source, const std::filesystem::path& firstPart,
                                   std::uint64_t partSize, const TaskContext& task);

// The index an edition has once the editions `removed` are gone; empty when it is one of them.
[[nodiscard]] std::optional<int> indexAfterRemoval(int index, std::span<const int> removed);

} // namespace wl::core
