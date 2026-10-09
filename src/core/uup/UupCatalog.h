#pragma once
// D-093: Windows media straight from Microsoft's update servers. A UUP set ("Unified Update
// Platform": the files Windows Update itself installs from) is listed by the UUP dump API
// (api.uupdump.net — it asks Windows Update and keeps the answers); the files come from Microsoft's
// CDN (*.dl.delivery.mp.microsoft.com) and each one is checked against its SHA-256.
//   listid.php?search=…&sortByDate=1   → builds (uuid, title, build, arch, created)
//   listlangs.php?id=…                 → languages of a build
//   listeditions.php?id=…&lang=…       → editions of a build in a language
//   get.php?id=…&lang=…&edition=…      → files (name, size, sha1, sha256, url); noLinks=1 → no url
// get.php with links answers one set per 10 s per address (USER_RATE_LIMITED): the links are asked
// for right before a download only (they expire), and a refusal is waited out once.
// Parsing and classification are pure and unit-tested; nothing here needs admin.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::uup {

enum class BuildKind : std::uint8_t {
    Release,   // "Windows 11, version 26H2 (26300.9550)": a full Windows, generally available
    Insider,   // "Windows 11 Insider Preview 27965.1000 (rs_prerelease)"
    Server,    // "Windows Server …"
    Update,    // a cumulative / preview / .NET update alone: not a Windows to install
};

struct Build {
    std::wstring id;    // UUP dump's uuid of the update
    std::wstring title; // "Windows 11, version 26H2 (26300.9550)"
    std::wstring build; // "26300.9550"
    std::wstring arch;  // amd64 | arm64 | x86
    std::int64_t created = 0; // unix seconds
    BuildKind kind = BuildKind::Update;
};

struct Language {
    std::wstring code; // "tr-tr"
    std::wstring name; // "Turkish"
};

struct Edition {
    std::wstring code; // "PROFESSIONAL"
    std::wstring name; // "Windows Pro"
};

enum class FileKind : std::uint8_t {
    Metadata,   // professional_tr-tr.esd: setup media, WinRE and the edition's file list
    PackageEsd, // Microsoft-Windows-*-Package.ESD: the files the edition's list points into
    Cab,        // a feature on demand, a language feature, a small update (.cab)
    Msu,        // a cumulative / checkpoint update (.msu)
    Edge,       // Edge.wim
    App,        // a Store app package (.msix / .appx / bundles, their licence and metadata)
    Other,
};

struct File {
    std::wstring name;
    std::uint64_t size = 0;
    std::wstring sha256; // lower-case hex; empty when the API had none (then sha1)
    std::wstring sha1;
    std::wstring url;    // empty for a list asked without links
    FileKind kind = FileKind::Other;
};

struct FileSet {
    std::wstring updateName; // "Windows 11, version 26H2 (26300.9550)"
    std::wstring build;      // "26300.9550"
    std::wstring arch;
    bool hasUpdates = false;  // cumulative updates to integrate are part of the set
    bool appxPresent = false; // Store apps are available (as language "neutral", edition "app")
    std::vector<File> files;

    [[nodiscard]] std::uint64_t totalSize() const noexcept;
};

// ---- pure (unit-tested) ----

[[nodiscard]] BuildKind buildKind(std::wstring_view title);
[[nodiscard]] FileKind fileKind(std::wstring_view name);
// The answer's error ("USER_RATE_LIMITED", "UNSUPPORTED_LANG" …) as an Error, or the response.
[[nodiscard]] Result<std::vector<Build>> parseBuilds(std::string_view json);
[[nodiscard]] Result<std::vector<Language>> parseLanguages(std::string_view json);
[[nodiscard]] Result<std::vector<Edition>> parseEditions(std::string_view json);
[[nodiscard]] Result<FileSet> parseFiles(std::string_view json);

// ---- network (engine thread) ----

// Newest first. `search`: a build number, "26H2", "Insider" … ; empty = everything recent.
[[nodiscard]] Result<std::vector<Build>> listBuilds(std::wstring_view search, const CancelToken& cancel = {});
[[nodiscard]] Result<std::vector<Language>> listLanguages(std::wstring_view id, const CancelToken& cancel = {});
[[nodiscard]] Result<std::vector<Edition>> listEditions(std::wstring_view id, std::wstring_view language,
                                                        const CancelToken& cancel = {});
// The files of `editions` (codes; the set of each, merged) in `language`. `links`: with download
// addresses (rate-limited, expire within hours) or without (sizes and hashes only, for a summary).
// Language "neutral" with edition "app" lists the Store apps.
[[nodiscard]] Result<FileSet> listFiles(std::wstring_view id, std::wstring_view language,
                                        std::span<const std::wstring> editions, bool links,
                                        const CancelToken& cancel = {});

} // namespace wl::core::uup
