#include "core/esd/EsdCatalog.h"

#include "base/Encoding.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/io/Cabinet.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"

#include <json.hpp>
#include <pugixml.hpp>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <thread>

namespace wl::core::esd {

namespace {

constexpr wchar_t kMetadataService[] =
    L"https://fe3.delivery.mp.microsoft.com/UpdateMetadataService/updates/search/v1/bydeviceinfo";
constexpr wchar_t kWindows11Link[] = L"https://go.microsoft.com/fwlink/?LinkId=2156292"; // 24H2 (June 2025): fallback
constexpr wchar_t kWindows10Link[] = L"https://go.microsoft.com/fwlink/?LinkId=841361";  // 22H2 (December 2023)
// What the Media Creation Tool of 25H2+ asks for (its SetupMgr.dll, "DownloadProductsCabFromDCAT").
constexpr char kCabQuery[] =
    R"({"Products":"PN=Windows.Products.Cab.amd64&V=26100.0.0.0&Branch=ge_release","DeviceAttributes":"DUScan=1;OSVersion=10.0.26100.1"})";

std::wstring child(const pugi::xml_node& node, const char* name) {
    return utf8::toWide(node.child_value(name));
}

std::wstring trim(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && iswspace(s[i])) {
        ++i;
    }
    return s.substr(i);
}

std::wstring sha256Hex(std::span<const std::uint8_t> data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return {};
    }
    std::array<UCHAR, 32> digest{};
    const NTSTATUS st = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()),
                                   digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(alg, 0);
    return BCRYPT_SUCCESS(st) ? hexLower(digest) : std::wstring();
}

HttpOptions options(std::wstring_view userAgent, std::size_t maxBytes = 0) {
    HttpOptions o;
    o.userAgent = userAgent.empty() ? std::wstring(L"WinLove") : std::wstring(userAgent);
    o.noCookies = true;
    o.maxBytes = maxBytes;
    return o;
}

Result<Catalog> catalogFromCab(const std::string& cab, Product product, std::wstring source) {
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(cab.data()), cab.size());
    auto xml = cabinetFile(bytes, "products.xml");
    if (!xml) {
        return std::unexpected(xml.error());
    }
    auto catalog = parseProductsXml(*xml, product);
    if (catalog) {
        catalog->source = std::move(source);
    }
    return catalog;
}

Result<std::string> getCab(std::wstring_view url, std::wstring_view userAgent, const CancelToken& cancel) {
    auto r = httpGet(url, options(userAgent, 16u << 20), cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"HTTP {}", r->status), std::wstring(url));
    }
    return std::move(r->body);
}

// Windows 11's catalog the way the Media Creation Tool of 25H2+ gets it.
Result<Catalog> fromMetadataService(std::wstring_view userAgent, const CancelToken& cancel) {
    auto r = httpPost(kMetadataService, kCabQuery, L"application/json", options(userAgent, 1u << 20), cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"Windows Update's metadata service answered HTTP {}", r->status),
                    kMetadataService);
    }
    auto where = parseCabSearch(r->body);
    if (!where) {
        return std::unexpected(where.error());
    }
    if (!trustedEsdUrl(where->url)) {
        return fail(ErrorCode::AccessDenied, L"the catalog's address is not a Microsoft server", where->url);
    }
    auto cab = getCab(where->url, userAgent, cancel);
    if (!cab) {
        return std::unexpected(cab.error());
    }
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(cab->data()), cab->size());
    if ((where->size && cab->size() != where->size) || sha256Hex(bytes) != where->sha256) {
        return fail(ErrorCode::IoError, L"products.cab does not match the SHA-256 Windows Update published", where->url);
    }
    log::info("esd", std::format(L"products.cab from Windows Update: {} bytes, SHA-256 {}", cab->size(), where->sha256));
    return catalogFromCab(*cab, Product::Windows11, kMetadataService);
}

} // namespace

std::wstring Catalog::build() const {
    std::wstring best;
    for (const auto& e : entries) {
        if (best.empty() || std::pair(majorBuild(e.build), revision(e.build)) > std::pair(majorBuild(best), revision(best))) {
            best = e.build;
        }
    }
    return best;
}

std::uint64_t Pick::bytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& f : files) {
        total += f.size;
    }
    return total;
}

std::pair<std::wstring, std::wstring> buildFromFileName(std::wstring_view fileName) {
    // "26300.9711.261011-1601.26h2_ge_release_svc_refresh_…" / "19045.3803.231204-0204.22h2_release_…"
    const auto a = fileName.find(L'.');
    if (a == std::wstring_view::npos) {
        return {};
    }
    const auto b = fileName.find(L'.', a + 1);
    if (b == std::wstring_view::npos) {
        return {};
    }
    const auto c = fileName.find(L'.', b + 1);
    const std::wstring_view major = fileName.substr(0, a);
    const std::wstring_view rev = fileName.substr(a + 1, b - a - 1);
    auto digits = [](std::wstring_view s) { return !s.empty() && std::ranges::all_of(s, [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }); };
    if (!digits(major) || !digits(rev)) {
        return {};
    }
    std::wstring stamp(c == std::wstring_view::npos ? std::wstring_view() : fileName.substr(b + 1, c - b - 1));
    return {std::wstring(major) + L"." + std::wstring(rev), stamp};
}

std::wstring catalogArchitecture(std::wstring_view uupArch) {
    const std::wstring a = text::lower(uupArch);
    return a == L"amd64" || a == L"x64" ? L"x64" : a == L"arm64" ? L"arm64" : a == L"x86" ? L"x86" : a;
}

int majorBuild(std::wstring_view build) {
    int n = 0;
    bool any = false;
    for (const wchar_t c : build) {
        if (c < L'0' || c > L'9') {
            break;
        }
        n = n * 10 + (c - L'0');
        any = true;
    }
    return any ? n : 0;
}

int revision(std::wstring_view build) {
    const auto dot = build.find(L'.');
    return dot == std::wstring_view::npos ? 0 : majorBuild(build.substr(dot + 1));
}

Result<Catalog> parseProductsXml(std::string_view xml, Product product) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return fail(ErrorCode::ParseError, L"products.xml is not XML");
    }
    const auto catalogNode = doc.child("MCT").child("Catalogs").child("Catalog");
    if (!catalogNode) {
        return fail(ErrorCode::ParseError, L"products.xml has no MCT/Catalogs/Catalog");
    }
    Catalog catalog;
    catalog.product = product;
    catalog.version = utf8::toWide(catalogNode.attribute("version").value());
    for (const auto& file : catalogNode.child("PublishedMedia").child("Files").children("File")) {
        Entry e;
        e.fileName = trim(child(file, "FileName"));
        e.language = text::lower(trim(child(file, "LanguageCode")));
        e.languageName = trim(child(file, "Language"));
        e.edition = trim(child(file, "Edition"));
        e.architecture = catalogArchitecture(trim(child(file, "Architecture")));
        try {
            e.size = std::stoull(trim(child(file, "Size")));
        } catch (...) {
            e.size = 0;
        }
        e.sha256 = text::lower(trim(child(file, "Sha256")));
        e.sha1 = text::lower(trim(child(file, "Sha1")));
        e.url = trim(child(file, "FilePath"));
        e.retailOnly = text::iequals(trim(child(file, "IsRetailOnly")), L"True");
        auto [build, stamp] = buildFromFileName(e.fileName);
        e.build = std::move(build);
        e.stamp = std::move(stamp);
        const bool hashed = e.sha256.size() == 64 || e.sha1.size() == 40;
        // An entry without what a download needs is of no use (and never offered).
        if (e.fileName.empty() || !text::iendsWith(e.fileName, L".esd") || e.fileName.find_first_of(L"\\/:") != std::wstring::npos ||
            e.size == 0 || !hashed || e.url.empty() || e.language.empty() || e.edition.empty() || e.build.empty()) {
            continue;
        }
        catalog.entries.push_back(std::move(e));
    }
    if (catalog.entries.empty()) {
        return fail(ErrorCode::ParseError, L"products.xml lists no ESD");
    }
    return catalog;
}

Result<CabLocation> parseCabSearch(std::string_view text) {
    const auto root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_array()) {
        return fail(ErrorCode::ParseError, L"Windows Update's metadata service did not answer with a list");
    }
    for (const auto& entry : root) {
        const auto files = entry.find("FileLocations");
        if (files == entry.end() || !files->is_array()) {
            continue;
        }
        for (const auto& f : *files) {
            const std::string name = f.value("FileName", std::string());
            if (!text::iequals(utf8::toWide(name), L"products.cab")) {
                continue;
            }
            CabLocation where;
            where.url = utf8::toWide(f.value("Url", std::string()));
            where.size = f.contains("Size") && f["Size"].is_number_unsigned() ? f["Size"].get<std::uint64_t>() : 0;
            const auto digest = base64Decode(f.value("Digest", std::string()));
            if (digest.size() == 32) {
                where.sha256 = hexLower(digest);
            }
            if (where.url.empty() || where.sha256.empty()) {
                return fail(ErrorCode::ParseError, L"products.cab without an address or a SHA-256");
            }
            return where;
        }
    }
    return fail(ErrorCode::NotFound, L"Windows Update's metadata service lists no products.cab");
}

bool imageEdition(std::wstring_view edition) {
    // The 26H2 consumer ESD (lab 2026-11-11) holds Home, Home Single Language, Education and Pro; the
    // business one Enterprise. The catalog maps the rest (N, Pro for Workstations, Pro Education,
    // the old Starter / Ultimate names) onto the same file: Setup reaches them with a key from those.
    for (const wchar_t* e : {L"Core", L"CoreSingleLanguage", L"Education", L"Professional", L"Enterprise"}) {
        if (text::iequals(edition, e)) {
            return true;
        }
    }
    return false;
}

Pick pickFiles(const Catalog& catalog, std::wstring_view language, std::wstring_view architecture,
               std::span<const std::wstring> editions) {
    Pick pick;
    const std::wstring lang = text::lower(language);
    const std::wstring arch = catalogArchitecture(architecture);
    for (const auto& wanted : editions) {
        if (!imageEdition(wanted)) {
            pick.missing.push_back(wanted); // listed, but not an image of its own in the ESD
            continue;
        }
        const Entry* found = nullptr;
        for (const auto& e : catalog.entries) {
            if (e.language == lang && e.architecture == arch && text::iequals(e.edition, wanted)) {
                found = &e;
                break;
            }
        }
        if (!found) {
            pick.missing.push_back(wanted);
            continue;
        }
        if (std::ranges::none_of(pick.files, [&](const Entry& f) { return text::iequals(f.fileName, found->fileName); })) {
            pick.files.push_back(*found);
        }
    }
    return pick;
}

std::optional<Product> productOf(const uup::Build& build) {
    if (build.title.find(L"Windows 11") != std::wstring::npos) {
        return Product::Windows11;
    }
    if (build.title.find(L"Windows 10") != std::wstring::npos) {
        return Product::Windows10;
    }
    return std::nullopt;
}

Choice chooseSource(const Catalog* catalog, bool failed, const uup::Build& build, bool newestOfRelease,
                    std::wstring_view language, std::span<const std::wstring> editions) {
    Choice c;
    if (build.kind != uup::BuildKind::Release || !productOf(build)) {
        c.reason = Reason::NotRelease;
        return c;
    }
    if (!catalog) {
        c.reason = failed ? Reason::Failed : Reason::Loading;
        return c;
    }
    const int major = majorBuild(build.build);
    const std::wstring arch = catalogArchitecture(build.arch);
    const std::wstring lang = text::lower(language);
    bool release = false, inArch = false, inLanguage = false;
    for (const auto& e : catalog->entries) {
        if (majorBuild(e.build) != major) {
            continue;
        }
        release = true;
        if (e.architecture != arch) {
            continue;
        }
        inArch = true;
        if (e.language == lang) {
            inLanguage = true;
            if (c.esdBuild.empty() || revision(e.build) > revision(c.esdBuild)) {
                c.esdBuild = e.build;
                c.esdStamp = e.stamp;
            }
        }
    }
    if (!release) {
        c.reason = Reason::NoRelease;
        c.esdBuild = catalog->build(); // what it has instead
        return c;
    }
    if (!inArch) {
        c.reason = Reason::NoArch;
        return c;
    }
    if (!inLanguage) {
        c.reason = Reason::NoLanguage;
        return c;
    }
    c.pick = pickFiles(*catalog, lang, arch, editions);
    if (!c.pick.missing.empty()) {
        c.reason = Reason::NoEdition;
        c.missingEdition = c.pick.missing.front();
        c.pick.files.clear();
        return c;
    }
    if (c.pick.files.empty()) {
        c.reason = Reason::NoEdition; // nothing picked yet
        return c;
    }
    c.esdAllowed = true;
    if (c.esdBuild == build.build) {
        c.reason = Reason::Exact;
        c.source = Source::Esd;
    } else if (newestOfRelease) {
        c.reason = Reason::Newest;
        c.source = Source::Esd;
    } else {
        c.reason = Reason::OtherBuild;
    }
    return c;
}

Source effectiveSource(const Choice& choice, std::optional<Source> userChoice) noexcept {
    if (userChoice == Source::Esd) {
        return choice.esdAllowed ? Source::Esd : Source::Uup;
    }
    if (userChoice == Source::Uup) {
        return Source::Uup;
    }
    return choice.esdAllowed ? choice.source : Source::Uup;
}

LocalState localState(std::optional<std::uint64_t> fileSize, std::optional<std::uint64_t> partSize, std::uint64_t expected) noexcept {
    if (fileSize) {
        return *fileSize == expected ? LocalState::Complete : LocalState::WrongSize;
    }
    if (partSize) {
        return *partSize <= expected ? LocalState::Partial : LocalState::WrongSize;
    }
    return LocalState::Missing;
}

bool trustedEsdUrl(std::wstring_view url) {
    const std::wstring u = text::lower(url);
    std::wstring_view rest;
    if (u.starts_with(L"https://")) {
        rest = std::wstring_view(u).substr(8);
    } else if (u.starts_with(L"http://")) {
        rest = std::wstring_view(u).substr(7);
    } else {
        return false;
    }
    const std::wstring_view authority = rest.substr(0, rest.find_first_of(L"/?#"));
    if (authority.find(L'@') != std::wstring_view::npos) {
        return false; // "http://microsoft.com@evil.example/"
    }
    const std::wstring_view host = authority.substr(0, authority.find(L':'));
    return host.size() > 14 && host.ends_with(L".microsoft.com");
}

Result<bool> verifyFile(const std::filesystem::path& file, const Entry& entry, const TaskContext& task) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size != entry.size) {
        return false;
    }
    const TaskContext hashing{task.cancel, [&task](double f, std::wstring_view) { task.report(f, L"verify"); }};
    if (!entry.sha256.empty()) {
        auto h = sha256File(file, hashing);
        if (!h) {
            return std::unexpected(h.error());
        }
        return *h == entry.sha256;
    }
    auto h = sha1File(file, hashing);
    if (!h) {
        return std::unexpected(h.error());
    }
    return *h == entry.sha1;
}

Result<Catalog> fetchCatalog(Product product, std::wstring_view userAgent, const CancelToken& cancel) {
    if (product == Product::Windows11) {
        auto current = fromMetadataService(userAgent, cancel);
        if (current || current.error().code == ErrorCode::Cancelled) {
            return current;
        }
        log::warn("esd", L"Windows 11 catalog from Windows Update: " + describe(current.error()) + L"; the older link");
    }
    const wchar_t* link = product == Product::Windows11 ? kWindows11Link : kWindows10Link;
    auto cab = getCab(link, userAgent, cancel);
    if (!cab) {
        return std::unexpected(cab.error());
    }
    return catalogFromCab(*cab, product, link);
}

Result<std::vector<std::filesystem::path>> downloadFiles(std::span<const Entry> files, const std::filesystem::path& folder,
                                                         std::wstring_view userAgent, const TaskContext& task) {
    std::uint64_t total = 0;
    for (const auto& f : files) {
        total += f.size;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const HttpOptions http = options(userAgent);
    std::vector<std::filesystem::path> saved;
    std::uint64_t done = 0;
    for (const auto& entry : files) {
        if (entry.fileName.empty() || entry.fileName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
            return fail(ErrorCode::InvalidArgument, L"unexpected file name in the catalog", entry.fileName);
        }
        if (!trustedEsdUrl(entry.url)) {
            return fail(ErrorCode::AccessDenied, L"the download address is not a Microsoft server", entry.url);
        }
        const auto target = folder / entry.fileName;
        const auto part = std::filesystem::path(target.wstring() + L".part");
        auto sizeOf = [&](const std::filesystem::path& p) -> std::optional<std::uint64_t> {
            std::error_code e;
            const auto s = std::filesystem::file_size(p, e);
            return e ? std::nullopt : std::optional<std::uint64_t>(s);
        };
        const LocalState state = localState(sizeOf(target), sizeOf(part), entry.size);
        log::info("esd", std::format(L"{}: {} bytes, local state {}", entry.fileName, entry.size, static_cast<int>(state)));
        if (state == LocalState::Complete) {
            auto ok = verifyFile(target, entry, task);
            if (!ok) {
                return std::unexpected(ok.error());
            }
            if (*ok) {
                log::info("esd", L"already here and verified: " + target.wstring());
                done += entry.size;
                saved.push_back(target);
                continue;
            }
            log::warn("esd", L"the file there does not match its hash; downloading it again: " + target.wstring());
            std::filesystem::remove(target, ec);
        } else if (state == LocalState::WrongSize) {
            std::filesystem::remove(target, ec);
            std::filesystem::remove(part, ec);
        }
        int mismatches = 0;
        int resumed = 0;
        int stalled = 0;
        for (;;) {
            if (auto go = task.cancel.check(L"download"); !go) {
                return std::unexpected(go.error());
            }
            std::optional<std::uint64_t> from;
            std::uint64_t reached = 0;
            auto got = httpDownload(entry.url, target, entry.size, TaskContext{task.cancel, {}},
                                    [&](std::uint64_t have, std::uint64_t) {
                                        if (!from) {
                                            from = have;
                                        }
                                        reached = have;
                                        task.report(total ? static_cast<double>(done + have) / static_cast<double>(total) : 0.0,
                                                    L"download");
                                    },
                                    [](std::wstring_view finalUrl) { return trustedEsdUrl(finalUrl); }, &http);
            if (!got) {
                if (got.error().code == ErrorCode::Cancelled || task.cancel.cancelled()) {
                    return fail(ErrorCode::Cancelled, L"download cancelled", entry.fileName);
                }
                // A dropped connection (Microsoft's CDN closes some): the .part goes on while each try brings
                // bytes. Tries that bring none wait longer each time (2, 4, 8, 16, 30 s: a minute of a
                // network gone away, lab 2026-11-11); then the error — a second run continues the .part.
                stalled = from && reached > *from ? 0 : stalled + 1;
                if (stalled <= 5 && ++resumed <= 200 && got.error().code != ErrorCode::AccessDenied) {
                    log::warn("esd", std::format(L"{}: {}; continuing", entry.fileName, got.error().message));
                    const int wait = stalled == 0 ? 1 : std::min(30, 1 << stalled);
                    for (int s = 0; s < wait * 10 && !task.cancel.cancelled(); ++s) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
                return std::unexpected(got.error());
            }
            auto ok = verifyFile(target, entry, task);
            if (!ok) {
                return std::unexpected(ok.error());
            }
            if (*ok) {
                log::info("esd", std::format(L"downloaded and verified ({}): {}", entry.sha256.empty() ? L"SHA-1" : L"SHA-256",
                                             target.wstring()));
                break;
            }
            std::filesystem::remove(target, ec);
            if (++mismatches > 1) {
                return fail(ErrorCode::IoError, L"the downloaded file does not match its published hash", entry.fileName);
            }
            log::warn("esd", L"hash mismatch, downloading again: " + entry.fileName);
        }
        done += entry.size;
        saved.push_back(target);
    }
    task.report(1.0, L"download");
    return saved;
}

} // namespace wl::core::esd
