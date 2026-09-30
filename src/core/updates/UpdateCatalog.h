#pragma once
// D-046: the newest updates for an image, from the Microsoft Update Catalog
// (www.catalog.update.microsoft.com) — what NTLite's "download updates" does.
//   1. Search.aspx?q=… returns an HTML table (id, title, products, classification, date, size).
//   2. The entries are classified by title ("2026-09 Cumulative Update for Windows 11, version
//      25H2 for x64-based Systems (KB5129195) (26200.9457)") and matched to the image's Windows
//      version and architecture; the newest cumulative update (LCU) and .NET cumulative update win.
//   3. DownloadDialog.aspx (POST updateIDs) lists the entry's files with URL and SHA-256. Since
//      24H2 an LCU lists the checkpoint MSU it builds on as well: both are downloaded into the same
//      folder, where DISM finds the checkpoint on its own (only the LCU itself is queued).
//   4. Every file is downloaded (resumable) and checked against the catalog's SHA-256.
// The page parsing is pure and unit-tested with saved pages; nothing here needs admin.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

enum class CatalogKind : std::uint8_t { Cumulative, DotNet, SafeOs, Setup, Other };

struct CatalogEntry {
    std::wstring id;             // update GUID
    std::wstring title;
    std::wstring products;
    std::wstring classification;
    int year = 0, month = 0, day = 0; // "Last Updated"
    std::uint64_t size = 0;
    // From the title:
    CatalogKind kind = CatalogKind::Other;
    bool preview = false;
    std::wstring kb;             // "KB5129195"
    int build = 0, revision = 0; // "(26200.9457)" — 0 when the title has none (Windows 10, .NET)

    [[nodiscard]] int dateKey() const noexcept { return year * 10000 + month * 100 + day; }
};

struct CatalogFile {
    std::wstring url;
    std::wstring fileName;
    std::vector<std::uint8_t> sha256; // 32 bytes, or empty when the catalog gave none
};

// What the updates are for: Windows 10 / 11, release ("25H2", "22H2"), architecture.
struct CatalogTarget {
    int windows = 11;
    std::wstring release;
    std::wstring architecture = L"x64"; // x64 | arm64 | x86
    int build = 0, revision = 0;        // the image's own build, e.g. 26200.8037
};
// From the image's build (26200 → 11 25H2); empty release for a build the catalog has no name for.
[[nodiscard]] CatalogTarget catalogTarget(int build, int revision, std::wstring_view architecture);

// Pure parsing and matching (unit-tested).
[[nodiscard]] std::vector<CatalogEntry> parseCatalogSearch(std::string_view html);
[[nodiscard]] std::vector<CatalogFile> parseCatalogDownload(std::string_view script);
void classifyCatalogEntry(CatalogEntry& entry);
[[nodiscard]] bool matchesTarget(const CatalogEntry& entry, const CatalogTarget& target);
// The search terms that find the target's cumulative and .NET updates.
[[nodiscard]] std::vector<std::wstring> catalogQueries(const CatalogTarget& target);

struct CatalogOffer {
    CatalogEntry entry;
    bool recommended = false; // the newest non-preview update of its kind
    bool olderThanImage = false; // the image already has this revision or a newer one
};
// The newest cumulative update and .NET update (and the previews newer than them) for the target,
// from matching entries of any number of searches (duplicates by id are merged).
[[nodiscard]] std::vector<CatalogOffer> pickCatalogOffers(const std::vector<CatalogEntry>& entries,
                                                         const CatalogTarget& target);

// Network: search (every query of catalogQueries) → offers.
[[nodiscard]] Result<std::vector<CatalogOffer>> findCatalogUpdates(const CatalogTarget& target, const CancelToken& cancel);
[[nodiscard]] Result<std::vector<CatalogFile>> catalogFiles(std::wstring_view updateId, const CancelToken& cancel);

// Only Microsoft download hosts are accepted (the URL comes from a web page).
[[nodiscard]] bool trustedDownloadUrl(std::wstring_view url);

struct DownloadedUpdate {
    std::filesystem::path main;               // the entry's own package (queued)
    std::vector<std::filesystem::path> prerequisites; // e.g. the 24H2 checkpoint (same folder)
    std::uint64_t bytes = 0;                  // size of the files fetched now (0: all were there already)
};
// Downloads the files of `entry` into `folder` (a file already there with the right SHA-256 is
// kept) and verifies each. Progress covers all files by size.
[[nodiscard]] Result<DownloadedUpdate> downloadCatalogUpdate(const CatalogEntry& entry,
                                                             const std::filesystem::path& folder,
                                                             const TaskContext& task);

} // namespace wl::core
