#pragma once
// D-149: Microsoft's official ESD catalog — what its Media Creation Tool downloads. products.cab
// holds products.xml: every language × edition × architecture of one Windows release, each entry
// a single solid ESD (setup media + Windows PE + Windows Setup + the editions) with its size, hash
// and address on Microsoft's CDN (dl.delivery.mp.microsoft.com).
//   Windows 11: the Media Creation Tool of 25H2+ asks Windows Update's metadata service for it —
//     POST https://fe3.delivery.mp.microsoft.com/UpdateMetadataService/updates/search/v1/bydeviceinfo
//     {"Products":"PN=Windows.Products.Cab.amd64&V=26100.0.0.0&Branch=ge_release",
//      "DeviceAttributes":"DUScan=1;OSVersion=10.0.26100.1"}
//     → [{"UpdateIds":[…],"FileLocations":[{"FileName":"products.cab","Size":…,"Digest":<base64
//       SHA-256>,"Url":…}]}]; the cab is Authenticode-signed by Microsoft. Its catalog (2.1) gives
//     each ESD a SHA-256. The tool's older link (go.microsoft.com/fwlink/?LinkId=2156292) still
//     answers with the 24H2 catalog of June 2025: the fallback only.
//   Windows 10 22H2: go.microsoft.com/fwlink/?LinkId=841361 → download.microsoft.com …/products.cab
//     (catalog 1.4.1, SHA-1 per ESD; 19045.3803, December 2023 — the last one).
// Requests go to Microsoft only, with a plain "WinLove/<version>" User-Agent and nothing else.
// Parsing, selection and the source choice are pure and unit-tested; nothing here needs admin.
#include "base/Result.h"
#include "core/tasks/Task.h"
#include "core/uup/UupCatalog.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::esd {

enum class Product : std::uint8_t { Windows11, Windows10 };

struct Entry {
    std::wstring fileName;     // 26300.9711.261011-1601.26h2_ge_release_svc_refresh_CLIENTCONSUMER_RET_x64FRE_tr-tr.esd
    std::wstring language;     // "tr-tr" (lower-case)
    std::wstring languageName; // "Turkish (Turkey)"
    std::wstring edition;      // "Professional" (an edition the ESD installs, or an upgrade name that maps onto it)
    std::wstring architecture; // "x64" | "arm64" | "x86" (lower-case)
    std::uint64_t size = 0;
    std::wstring sha256;       // lower-case hex; empty in the older catalogs…
    std::wstring sha1;         // …which have this
    std::wstring url;
    bool retailOnly = false;
    std::wstring build;        // "26300.9711" from the file name
    std::wstring stamp;        // "261011-1601": when Microsoft built it (yyMMdd-HHmm)
};

struct Catalog {
    Product product = Product::Windows11;
    std::wstring version;      // the catalog format ("2.1")
    std::wstring source;       // where it came from (the address, for the log)
    std::vector<Entry> entries;
    // The release's build, "26300.9711" (the newest one of the entries).
    [[nodiscard]] std::wstring build() const;
};

// Where Windows Update's metadata service says products.cab is.
struct CabLocation {
    std::wstring url;
    std::uint64_t size = 0;
    std::wstring sha256; // lower-case hex (the answer's Digest is base64)
};

// ---- pure (unit-tested) ----

[[nodiscard]] Result<Catalog> parseProductsXml(std::string_view xml, Product product);
[[nodiscard]] Result<CabLocation> parseCabSearch(std::string_view json);
// "26300.9711.261011-1601.26h2_ge_release…" → {"26300.9711", "261011-1601"}; empty when not that shape.
[[nodiscard]] std::pair<std::wstring, std::wstring> buildFromFileName(std::wstring_view fileName);
// UUP's "amd64" / "arm64" / "x86" → the catalog's "x64" / "arm64" / "x86".
[[nodiscard]] std::wstring catalogArchitecture(std::wstring_view uupArch);
// "26300.9711" → 26300; 0 when it is not a build.
[[nodiscard]] int majorBuild(std::wstring_view build);
[[nodiscard]] int revision(std::wstring_view build);

// An edition the official ESDs carry as an image of its own (Core, CoreSingleLanguage, Education,
// Professional, Enterprise); the catalog also lists names it maps onto them (ProfessionalN,
// ProfessionalWorkstation …) — those are not offered (the ESD has no such image to export).
[[nodiscard]] bool imageEdition(std::wstring_view edition);
// The ESD files a selection needs: one per distinct file of the editions (Pro and Home share the
// consumer ESD; Enterprise has the business one). `missing`: editions (as asked) the catalog has
// no file for in that language and architecture.
struct Pick {
    std::vector<Entry> files;
    std::vector<std::wstring> missing;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
};
[[nodiscard]] Pick pickFiles(const Catalog& catalog, std::wstring_view language, std::wstring_view architecture,
                             std::span<const std::wstring> editions);

// ---- the source of one download ----

enum class Source : std::uint8_t { Uup, Esd };

enum class Reason : std::uint8_t {
    Exact,       // the catalog has this very build → ESD
    Newest,      // the newest build of its release in the list, the catalog has the release → ESD
    OtherBuild,  // an older build was picked; the catalog has another one of the release → UUP, ESD may be chosen
    Loading,     // the catalog is still on its way → UUP
    Waiting,     // the catalog is here, the selection's editions are not yet (the page's own state) → UUP
    Failed,      // the catalog could not be had → UUP
    NotRelease,  // an Insider / Server build: no official ESD → UUP
    NoRelease,   // the catalog is of another release → UUP
    NoLanguage,  // …not in this language
    NoArch,      // …not for this architecture
    NoEdition,   // …not with a picked edition
};

struct Choice {
    Source source = Source::Uup; // what the download uses unless the user switches
    bool esdAllowed = false;     // ESD may be chosen
    Reason reason = Reason::Loading;
    std::wstring esdBuild;       // the catalog's build ("26300.9711"; NoRelease: the release it has instead)
    std::wstring esdStamp;       // its date stamp ("261011-1601")
    std::wstring missingEdition; // NoEdition: the first edition it lacks
    Pick pick;                   // the files when allowed
};

// The catalog of the build's product (nullptr while loading / when it failed: `failed`), whether
// the build is the newest of its release and architecture in the list, the language and editions
// (UUP codes; matched case-insensitively against the catalog's edition names).
[[nodiscard]] Choice chooseSource(const Catalog* catalog, bool failed, const uup::Build& build, bool newestOfRelease,
                                  std::wstring_view language, std::span<const std::wstring> editions);
// What actually goes: the user's switch when it is allowed, else the automatic choice.
[[nodiscard]] Source effectiveSource(const Choice& choice, std::optional<Source> userChoice) noexcept;
// Which catalog a build's title belongs to: Windows 11 / 10; nullopt for anything else.
[[nodiscard]] std::optional<Product> productOf(const uup::Build& build);

// ---- local files ----

// What is on the disk for an entry: nothing, a partial download ("<name>.part", continued with a
// Range request), the whole file (verified next), or a file of the wrong size (downloaded again).
enum class LocalState : std::uint8_t { Missing, Partial, Complete, WrongSize };
[[nodiscard]] LocalState localState(std::optional<std::uint64_t> fileSize, std::optional<std::uint64_t> partSize,
                                    std::uint64_t expected) noexcept;
// The file is the entry's: its SHA-256 (or SHA-1 in the older catalogs). Progress: stage "verify".
[[nodiscard]] Result<bool> verifyFile(const std::filesystem::path& file, const Entry& entry, const TaskContext& task);
// Only Microsoft's servers (http/https, a host under microsoft.com).
[[nodiscard]] bool trustedEsdUrl(std::wstring_view url);

// ---- network (a worker thread) ----

// `userAgent`: "WinLove/1.3.0".
[[nodiscard]] Result<Catalog> fetchCatalog(Product product, std::wstring_view userAgent, const CancelToken& cancel = {});
// The files into `folder` (a second run continues: a verified file is kept, a .part goes on).
// Progress: fraction of all bytes, stage "download" / "verify".
[[nodiscard]] Result<std::vector<std::filesystem::path>> downloadFiles(std::span<const Entry> files,
                                                                       const std::filesystem::path& folder,
                                                                       std::wstring_view userAgent, const TaskContext& task);

} // namespace wl::core::esd
