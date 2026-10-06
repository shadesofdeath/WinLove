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

// The editions in a new order — Setup lists them in file order (an AIO, D-077). `order` names
// every index once ({2,1,3}: the second edition first). Rewritten like removeImages: the original
// is replaced only once the new file is complete. Not for ESD / split / boot images.
[[nodiscard]] Result<void> reorderImages(const std::filesystem::path& wim, std::span<const int> order,
                                         const TaskContext& task);
// Every index 1..count exactly once.
[[nodiscard]] bool isPermutation(std::span<const int> order, int count);

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

// ---- Images page tools (2026-10-01, D-058) -----------------------------------------------------

// Editions `indexes` of `source` — a WIM, an ESD or the first part of a split WIM (its other
// parts next to it) — appended to `destination` (created when missing) with `compression`. The
// same edition twice is allowed: a copy is a variant to be customised on its own.
[[nodiscard]] Result<void> exportImages(const std::filesystem::path& source, std::span<const int> indexes,
                                        const std::filesystem::path& destination, WimCompression compression,
                                        const TaskContext& task);

// Every edition of `wim` rewritten with `target` compression (Lzms = a solid ESD). The result has
// the same name with .wim / .esd as the compression says; the original goes once the new file is
// complete. A bootable WIM keeps its boot index. Returns the new path (the same when only the
// compression changed). Not for split WIMs (mergeSplitWim first); same compression = refused.
[[nodiscard]] Result<std::filesystem::path> recompressWim(const std::filesystem::path& wim, WimCompression target,
                                                          const TaskContext& task);

// install.swm + install2.swm … (the parts next to `firstPart`) → one WIM, LZX.
[[nodiscard]] Result<void> mergeSplitWim(const std::filesystem::path& firstPart, const std::filesystem::path& destination,
                                         const TaskContext& task);

// A copy of edition `index` appended to the same WIM, named `name` (the description stays). The
// file grows by the copy's metadata only: every file is shared. Returns the new index.
[[nodiscard]] Result<int> duplicateEdition(const std::filesystem::path& wim, int index, const std::wstring& name,
                                           const TaskContext& task);

// `folder` (a folder, or a drive's root) captured as a new edition of `wim` (created when missing,
// otherwise appended), named by `text`. Needs an elevated process (backup / security privileges);
// a running system's own drive needs a shadow copy and is not supported. Returns the new index.
[[nodiscard]] Result<int> captureImage(const std::filesystem::path& folder, const std::filesystem::path& wim,
                                       const ImageText& text, WimCompression compression, const TaskContext& task);

// Edition `index` of `wim` (a WIM or an ESD) written out as files under `folder` (created when
// missing), without security descriptors: for a UUP language pack, an ESD that holds the expanded
// package DISM then adds from the folder (D-061). Applying a whole Windows is not what this is for.
[[nodiscard]] Result<void> applyImage(const std::filesystem::path& wim, int index, const std::filesystem::path& folder,
                                      const TaskContext& task);

// The edition a WIM boots (WIMSetBootImage); 0 = none. boot.wim / WinRE.wim need it.
[[nodiscard]] Result<void> setBootImage(const std::filesystem::path& wim, int index);

// The index an edition has once the editions `removed` are gone; empty when it is one of them.
[[nodiscard]] std::optional<int> indexAfterRemoval(int index, std::span<const int> removed);

} // namespace wl::core
