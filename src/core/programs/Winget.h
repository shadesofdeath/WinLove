#pragma once
// winget's package repository as WinLove reads it (D-078) — no winget needed on this PC:
//  - the index: https://cdn.winget.microsoft.com/cache/source2.msix, signed by Microsoft, holding
//    Public\index.db (SQLite, read with Windows' own winsqlite3.dll): id, name, moniker, latest
//    version and tags of every package (~15 000);
//  - a package's versions: <cache>/packages/<id>/<first 8 hex of its hash>/versionData.mszyml
//    (MSZIP, Windows' Compression API), whose SHA-256 is that hash in the index;
//  - a version's merged manifest: <cache>/<relative path>, its SHA-256 given by the version list;
//  - its icon: the manifest's IconUrl, checked against IconSha256.
// So everything shown is checked back to the signed index. All of it is cached under one folder.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3; // winsqlite3.h

namespace wl::core {

struct WingetPackage {
    std::wstring id;      // "VideoLAN.VLC"
    std::wstring name;    // "VLC media player"
    std::wstring moniker; // "vlc" (may be empty)
    std::wstring version; // the latest

    [[nodiscard]] bool operator==(const WingetPackage&) const = default;
};

// What a package's manifest says about its latest version (texts in the requested locale when it has them).
struct WingetDetails {
    std::wstring id;
    std::wstring version;
    std::wstring name;
    std::wstring publisher;
    std::wstring author;
    std::wstring shortDescription;
    std::wstring description;
    std::wstring license;
    std::wstring licenseUrl;
    std::wstring homepage; // PackageUrl, else PublisherUrl
    std::wstring releaseNotesUrl;
    std::vector<std::wstring> tags;
    std::wstring iconUrl;
    std::wstring iconSha256; // lowercase hex; empty when the manifest gives none
    std::vector<std::wstring> installerTypes; // "wix", "inno", "msix" … each once
    std::vector<std::wstring> scopes;         // "machine", "user"
    std::vector<std::wstring> architectures;  // "x64", "arm64", "neutral" …
};

class WingetIndex {
public:
    // A ready index.db (refreshWingetIndex() keeps one), opened read-only.
    [[nodiscard]] static Result<WingetIndex> open(const std::filesystem::path& indexDb);
    WingetIndex(WingetIndex&& other) noexcept;
    WingetIndex& operator=(WingetIndex&& other) noexcept;
    WingetIndex(const WingetIndex&) = delete;
    WingetIndex& operator=(const WingetIndex&) = delete;
    ~WingetIndex();

    [[nodiscard]] std::size_t count() const;
    // Best first: the id / moniker / name itself, then names and ids starting with the text, then
    // containing it, then packages tagged with it. Case-insensitive (ASCII); empty text: nothing.
    [[nodiscard]] std::vector<WingetPackage> search(std::wstring_view text, std::size_t limit) const;
    [[nodiscard]] std::optional<WingetPackage> find(std::wstring_view id) const; // exact id, any case
    // Packages tagged with any of `tags` (winget's tags are lower case), by name; at most `limit`.
    [[nodiscard]] std::vector<WingetPackage> tagged(std::span<const std::wstring> tags, std::size_t limit) const;
    // The whole repository by name, at most `limit`.
    [[nodiscard]] std::vector<WingetPackage> all(std::size_t limit) const;
    [[nodiscard]] std::vector<std::wstring> tags(std::wstring_view id) const;
    // D-095: the packages an installed program is (InstalledPrograms): by its product code — an MSI
    // GUID or an Uninstall key's name, any case — and by winget's normalized name, each with whether
    // its normalized publisher is `normPublisher` too.
    [[nodiscard]] std::vector<WingetPackage> byProductCode(std::wstring_view code) const;
    [[nodiscard]] std::vector<std::pair<WingetPackage, bool>> byNormalizedName(std::span<const std::wstring> normNames,
                                                                               std::wstring_view normPublisher) const;
    // The SHA-256 (lowercase hex) of the package's version list; empty when the id is unknown.
    [[nodiscard]] std::wstring versionDataHash(std::wstring_view id) const;
    // When Microsoft built this index (its metadata); epoch when it does not say.
    [[nodiscard]] std::chrono::system_clock::time_point builtAt() const;

private:
    WingetIndex() = default;
    sqlite3* m_db = nullptr;
};

// Keeps <folder>\index.db current: source2.msix is downloaded when the copy is older than
// `maxAge` or missing, its signature checked (valid, signed by Microsoft Corporation) and
// index.db taken out of it. Without a network a copy that exists is returned as it is.
[[nodiscard]] Result<std::filesystem::path> refreshWingetIndex(const std::filesystem::path& folder, std::chrono::hours maxAge,
                                                               const TaskContext& task);

// The latest version's details (cached under `cache`; `locale` "tr-TR": its localized texts if any).
[[nodiscard]] Result<WingetDetails> fetchWingetDetails(const WingetIndex& index, std::wstring_view id,
                                                       const std::filesystem::path& cache, std::wstring_view locale,
                                                       const CancelToken& cancel = {});
// The same from what the index said (find() and versionDataHash()), without the index: for a
// thread of its own while the index stays with the one that searches it.
[[nodiscard]] Result<WingetDetails> fetchWingetDetails(const WingetPackage& package, std::wstring_view versionDataHash,
                                                       const std::filesystem::path& cache, std::wstring_view locale,
                                                       const CancelToken& cancel = {});
// The package's icon file (cached), checked against its SHA-256 when the manifest gives one.
[[nodiscard]] Result<std::filesystem::path> fetchWingetIcon(const WingetDetails& details, const std::filesystem::path& cache,
                                                            const CancelToken& cancel = {});

// ---- the pure parts (tests) -----------------------------------------------------------------
struct WingetVersion {
    std::wstring version;
    std::wstring relativePath; // "manifests/v/VideoLAN/VLC/3.0.24/0fdc"
    std::wstring sha256;       // lowercase hex
};
[[nodiscard]] Result<std::string> decompressMszip(std::span<const std::byte> data);
[[nodiscard]] Result<std::vector<WingetVersion>> parseVersionData(std::string_view yaml);
[[nodiscard]] Result<WingetDetails> parseWingetManifest(std::string_view yaml, std::wstring_view locale);
// What may go on a command line as a winget id: letters, digits and . - _ +
[[nodiscard]] bool validWingetId(std::wstring_view id) noexcept;
[[nodiscard]] std::wstring sha256Hex(std::span<const std::byte> data);

} // namespace wl::core
