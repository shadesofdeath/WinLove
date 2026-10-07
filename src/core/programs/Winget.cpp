#include "core/programs/Winget.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/net/Http.h"
#include "core/programs/Yaml.h"

#include <windows.h>

#include <appxpackaging.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <shlwapi.h>
#include <softpub.h>
#include <wintrust.h>
#include <winsqlite/winsqlite3.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>

namespace wl::core {

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::wstring_view kCache = L"https://cdn.winget.microsoft.com/cache/";
constexpr const wchar_t* kIndexPackage = L"source2.msix";
constexpr const wchar_t* kIndexFile = L"index.db";

std::wstring column(sqlite3_stmt* statement, int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    return text ? utf8::toWide(text) : std::wstring();
}

// One prepared statement, finalized when it goes.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &m_statement, nullptr) != SQLITE_OK) {
            m_statement = nullptr;
            log::warn("winget", L"index query not prepared: " + utf8::toWide(sqlite3_errmsg(db)));
        }
    }
    ~Statement() {
        if (m_statement) {
            sqlite3_finalize(m_statement);
        }
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return m_statement != nullptr; }
    void bind(int index, const std::string& text) {
        sqlite3_bind_text(m_statement, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
    }
    void bind(int index, std::int64_t value) { sqlite3_bind_int64(m_statement, index, value); }
    [[nodiscard]] bool step() { return sqlite3_step(m_statement) == SQLITE_ROW; }
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return m_statement; }

private:
    sqlite3_stmt* m_statement = nullptr;
};

WingetPackage packageFrom(sqlite3_stmt* row) {
    return WingetPackage{column(row, 0), column(row, 1), column(row, 2), column(row, 3)};
}

// "a_b%" → "a\_b\%" for LIKE ... ESCAPE '\'.
std::string likeEscaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::wstring hexOf(std::span<const unsigned char> bytes) {
    static constexpr wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

// The signer of a file whose Authenticode signature Windows accepts; empty when it does not.
std::wstring trustedSigner(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof(info);
    info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    std::wstring signer;
    if (status == ERROR_SUCCESS) {
        if (CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData)) {
            if (CRYPT_PROVIDER_SGNR* sgnr = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)) {
                if (CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(sgnr, 0); cert && cert->pCert) {
                    wchar_t name[256] = {};
                    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
                    signer = name;
                }
            }
        }
    } else {
        log::warn("winget", std::format(L"signature not accepted ({:#010x}): {}", static_cast<unsigned long>(status), file.wstring()));
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return signer;
}

// COM for this thread while one call needs it (the reader / engine threads may not have it).
class ComScope {
public:
    ComScope() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT m_hr;
};

// Public\index.db out of the package: the packaging API checks every block against the block map.
Result<void> extractIndex(const std::filesystem::path& package, const std::filesystem::path& target) {
    ComScope com;
    ComPtr<IAppxFactory> factory;
    HRESULT hr = CoCreateInstance(__uuidof(AppxFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IStream> in;
    if (SUCCEEDED(hr)) {
        hr = SHCreateStreamOnFileEx(package.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &in);
    }
    ComPtr<IAppxPackageReader> reader;
    if (SUCCEEDED(hr)) {
        hr = factory->CreatePackageReader(in.Get(), &reader);
    }
    ComPtr<IAppxFile> file;
    if (SUCCEEDED(hr)) {
        hr = reader->GetPayloadFile(L"Public\\index.db", &file);
    }
    ComPtr<IStream> stream;
    if (SUCCEEDED(hr)) {
        hr = file->GetStream(&stream);
    }
    if (FAILED(hr)) {
        return fail(ErrorCode::ParseError, L"the winget index package cannot be read", package.wstring(), static_cast<std::int32_t>(hr));
    }
    const auto temporary = std::filesystem::path(target).concat(L".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        char buffer[1 << 16];
        ULONG got = 0;
        while (SUCCEEDED(hr = stream->Read(buffer, sizeof(buffer), &got)) && got > 0) {
            out.write(buffer, got);
        }
        if (FAILED(hr) || !out) {
            return fail(ErrorCode::IoError, L"the winget index could not be written", temporary.wstring(), static_cast<std::int32_t>(hr));
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"the winget index could not be replaced", target.wstring());
    }
    return {};
}

std::wstring text(const nlohmann::json& node, const char* key) {
    if (!node.is_object()) {
        return {};
    }
    const auto it = node.find(key);
    return it != node.end() && it->is_string() ? utf8::toWide(it->get<std::string>()) : std::wstring();
}

std::vector<std::wstring> texts(const nlohmann::json& node, const char* key) {
    std::vector<std::wstring> out;
    if (!node.is_object()) {
        return out;
    }
    const auto it = node.find(key);
    if (it != node.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string()) {
                out.push_back(utf8::toWide(item.get<std::string>()));
            }
        }
    }
    return out;
}

void addOnce(std::vector<std::wstring>& list, std::wstring value) {
    if (!value.empty() && std::ranges::none_of(list, [&](const std::wstring& v) { return text::iequals(v, value); })) {
        list.push_back(std::move(value));
    }
}

// A cached file, used only while its SHA-256 still matches; otherwise fetched again and checked.
Result<std::string> fetchChecked(const std::wstring& url, const std::filesystem::path& cached, std::wstring_view sha256,
                                 const CancelToken& cancel) {
    if (auto bytes = readFileBytes(cached); bytes && (sha256.empty() || sha256Hex(bytesOf(*bytes)) == sha256)) {
        return std::move(*bytes);
    }
    auto response = httpGet(url, cancel);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (response->status != 200) {
        return fail(ErrorCode::NotFound, std::format(L"winget repository answered {}", response->status), url);
    }
    if (!sha256.empty() && sha256Hex(bytesOf(response->body)) != sha256) {
        return fail(ErrorCode::ParseError, L"a winget file does not match its SHA-256", url);
    }
    std::error_code ec;
    std::filesystem::create_directories(cached.parent_path(), ec);
    (void)writeFileAtomic(cached, response->body);
    return std::move(response->body);
}

// A path segment of a cache file name: only what a file name may hold.
std::wstring safeName(std::wstring_view name) {
    std::wstring out;
    for (const wchar_t c : name) {
        out.push_back(std::wstring_view(L"<>:\"/\\|?*").find(c) == std::wstring_view::npos && c >= 32 ? c : L'_');
    }
    return out;
}

} // namespace

// ---- WingetIndex ------------------------------------------------------------------------------

Result<WingetIndex> WingetIndex::open(const std::filesystem::path& indexDb) {
    WingetIndex index;
    const std::string path = utf8::fromWide(indexDb.wstring());
    if (sqlite3_open_v2(path.c_str(), &index.m_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::wstring why = index.m_db ? utf8::toWide(sqlite3_errmsg(index.m_db)) : std::wstring();
        return fail(ErrorCode::IoError, L"the winget index cannot be opened", indexDb.wstring() + L": " + why);
    }
    if (index.count() == 0) {
        return fail(ErrorCode::ParseError, L"not a winget index", indexDb.wstring());
    }
    return index;
}

WingetIndex::WingetIndex(WingetIndex&& other) noexcept : m_db(std::exchange(other.m_db, nullptr)) {}

WingetIndex& WingetIndex::operator=(WingetIndex&& other) noexcept {
    if (this != &other) {
        if (m_db) {
            sqlite3_close(m_db);
        }
        m_db = std::exchange(other.m_db, nullptr);
    }
    return *this;
}

WingetIndex::~WingetIndex() {
    if (m_db) {
        sqlite3_close(m_db);
    }
}

std::size_t WingetIndex::count() const {
    Statement q(m_db, "SELECT count(*) FROM packages");
    return q && q.step() ? static_cast<std::size_t>(sqlite3_column_int64(q.get(), 0)) : 0;
}

std::vector<WingetPackage> WingetIndex::search(std::wstring_view query, std::size_t limit) const {
    std::vector<WingetPackage> out;
    const std::string exact = lowerAscii(utf8::fromWide(std::wstring(text::trim(query))));
    if (exact.empty() || limit == 0) {
        return out;
    }
    const std::string escaped = likeEscaped(exact);
    Statement q(m_db,
                "SELECT id, name, moniker, latest_version, "
                "CASE WHEN lower(id) = ?1 OR lower(moniker) = ?1 THEN 0 "
                "     WHEN lower(name) = ?1 THEN 1 "
                "     WHEN lower(name) LIKE ?2 ESCAPE '\\' THEN 2 "
                "     WHEN lower(id) LIKE ?2 ESCAPE '\\' THEN 3 "
                "     WHEN lower(name) LIKE ?3 ESCAPE '\\' THEN 4 "
                "     WHEN lower(id) LIKE ?3 ESCAPE '\\' OR lower(moniker) LIKE ?3 ESCAPE '\\' THEN 5 "
                "     ELSE 6 END AS rank "
                "FROM packages "
                "WHERE lower(id) LIKE ?3 ESCAPE '\\' OR lower(name) LIKE ?3 ESCAPE '\\' OR lower(moniker) LIKE ?3 ESCAPE '\\' "
                "   OR rowid IN (SELECT m.package FROM tags2_map m JOIN tags2 t ON t.rowid = m.tag WHERE t.tag = ?1) "
                "ORDER BY rank, length(name), name LIMIT ?4");
    if (!q) {
        return out;
    }
    q.bind(1, exact);
    q.bind(2, escaped + "%");
    q.bind(3, "%" + escaped + "%");
    q.bind(4, static_cast<std::int64_t>(limit));
    while (q.step()) {
        out.push_back(packageFrom(q.get()));
    }
    return out;
}

std::optional<WingetPackage> WingetIndex::find(std::wstring_view id) const {
    Statement q(m_db, "SELECT id, name, moniker, latest_version FROM packages WHERE lower(id) = ?1 LIMIT 1");
    if (!q) {
        return std::nullopt;
    }
    q.bind(1, lowerAscii(utf8::fromWide(std::wstring(id))));
    if (!q.step()) {
        return std::nullopt;
    }
    return packageFrom(q.get());
}

std::vector<WingetPackage> WingetIndex::tagged(std::span<const std::wstring> tags, std::size_t limit) const {
    std::vector<WingetPackage> out;
    if (tags.empty() || limit == 0) {
        return out;
    }
    std::string placeholders;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        placeholders += i == 0 ? "?" : ",?";
    }
    const std::string sql = "SELECT DISTINCT p.id, p.name, p.moniker, p.latest_version FROM packages p "
                            "JOIN tags2_map m ON m.package = p.rowid JOIN tags2 t ON t.rowid = m.tag "
                            "WHERE t.tag IN (" + placeholders + ") ORDER BY lower(p.name), p.id LIMIT ?";
    Statement q(m_db, sql.c_str());
    if (!q) {
        return out;
    }
    int at = 1;
    for (const auto& tag : tags) {
        q.bind(at++, lowerAscii(utf8::fromWide(tag)));
    }
    q.bind(at, static_cast<std::int64_t>(limit));
    while (q.step()) {
        out.push_back(packageFrom(q.get()));
    }
    return out;
}

std::vector<WingetPackage> WingetIndex::all(std::size_t limit) const {
    std::vector<WingetPackage> out;
    Statement q(m_db, "SELECT id, name, moniker, latest_version FROM packages ORDER BY lower(name), id LIMIT ?1");
    if (!q) {
        return out;
    }
    q.bind(1, static_cast<std::int64_t>(limit));
    while (q.step()) {
        out.push_back(packageFrom(q.get()));
    }
    return out;
}

std::vector<std::wstring> WingetIndex::tags(std::wstring_view id) const {
    std::vector<std::wstring> out;
    Statement q(m_db, "SELECT t.tag FROM packages p JOIN tags2_map m ON m.package = p.rowid JOIN tags2 t ON t.rowid = m.tag "
                      "WHERE lower(p.id) = ?1 ORDER BY t.tag");
    if (!q) {
        return out;
    }
    q.bind(1, lowerAscii(utf8::fromWide(std::wstring(id))));
    while (q.step()) {
        out.push_back(column(q.get(), 0));
    }
    return out;
}

std::wstring WingetIndex::versionDataHash(std::wstring_view id) const {
    Statement q(m_db, "SELECT hash FROM packages WHERE lower(id) = ?1 LIMIT 1");
    if (!q) {
        return {};
    }
    q.bind(1, lowerAscii(utf8::fromWide(std::wstring(id))));
    if (!q.step()) {
        return {};
    }
    const auto* blob = static_cast<const unsigned char*>(sqlite3_column_blob(q.get(), 0));
    const int size = sqlite3_column_bytes(q.get(), 0);
    return blob && size > 0 ? hexOf({blob, static_cast<std::size_t>(size)}) : std::wstring();
}

std::chrono::system_clock::time_point WingetIndex::builtAt() const {
    Statement q(m_db, "SELECT value FROM metadata WHERE name = 'lastwritetime'");
    if (!q || !q.step()) {
        return {};
    }
    const std::wstring seconds = column(q.get(), 0);
    return std::chrono::system_clock::time_point(std::chrono::seconds(std::wcstoll(seconds.c_str(), nullptr, 10)));
}

// ---- downloads ---------------------------------------------------------------------------------

Result<std::filesystem::path> refreshWingetIndex(const std::filesystem::path& folder, std::chrono::hours maxAge,
                                                 const TaskContext& task) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto index = folder / kIndexFile;
    const auto package = folder / kIndexPackage;
    const bool have = std::filesystem::is_regular_file(index, ec);
    if (have) {
        const auto written = std::filesystem::last_write_time(index, ec);
        if (!ec && std::filesystem::file_time_type::clock::now() - written < maxAge) {
            return index;
        }
    }
    auto downloaded = httpDownload(std::wstring(kCache) + kIndexPackage, package, 0, task);
    if (!downloaded) {
        if (have && downloaded.error().code != ErrorCode::Cancelled) {
            log::warn("winget", L"index not refreshed, the copy stays: " + describe(downloaded.error()));
            return index;
        }
        return std::unexpected(downloaded.error());
    }
    const std::wstring signer = trustedSigner(package);
    if (signer != L"Microsoft Corporation") {
        std::filesystem::remove(package, ec);
        return fail(ErrorCode::AccessDenied, L"the winget index is not signed by Microsoft",
                    signer.empty() ? std::wstring(L"no valid signature") : signer);
    }
    if (auto extracted = extractIndex(package, index); !extracted) {
        return std::unexpected(extracted.error());
    }
    std::filesystem::last_write_time(index, std::filesystem::file_time_type::clock::now(), ec);
    log::info("winget", std::format(L"index refreshed ({} bytes, signed by {})", *downloaded, signer));
    return index;
}

Result<WingetDetails> fetchWingetDetails(const WingetIndex& index, std::wstring_view id, const std::filesystem::path& cache,
                                         std::wstring_view locale, const CancelToken& cancel) {
    const auto package = index.find(id);
    if (!package) {
        return fail(ErrorCode::NotFound, L"no such winget package", std::wstring(id));
    }
    return fetchWingetDetails(*package, index.versionDataHash(id), cache, locale, cancel);
}

Result<WingetDetails> fetchWingetDetails(const WingetPackage& found, std::wstring_view versionDataHash,
                                         const std::filesystem::path& cache, std::wstring_view locale, const CancelToken& cancel) {
    const WingetPackage* package = &found;
    const std::wstring hash(versionDataHash);
    if (hash.size() < 8) {
        return fail(ErrorCode::NotFound, L"the winget package has no version list", found.id);
    }
    const std::wstring folder = safeName(package->id);
    // The version list: its SHA-256 is the hash the signed index gives.
    auto listed = fetchChecked(std::format(L"{}packages/{}/{}/versionData.mszyml", kCache, package->id, hash.substr(0, 8)),
                               cache / L"packages" / folder / (hash + L".mszyml"), hash, cancel);
    if (!listed) {
        return std::unexpected(listed.error());
    }
    auto yaml = decompressMszip(bytesOf(*listed));
    if (!yaml) {
        return std::unexpected(yaml.error());
    }
    auto versions = parseVersionData(*yaml);
    if (!versions) {
        return std::unexpected(versions.error());
    }
    if (versions->empty()) {
        return fail(ErrorCode::NotFound, L"the winget package lists no version", package->id);
    }
    const auto latest = std::ranges::find(*versions, package->version, &WingetVersion::version);
    const WingetVersion& version = latest != versions->end() ? *latest : versions->front();
    auto manifest = fetchChecked(std::wstring(kCache) + version.relativePath,
                                 cache / L"manifests" / folder / (safeName(version.version) + L"-" + version.sha256.substr(0, 8) + L".yaml"),
                                 version.sha256, cancel);
    if (!manifest) {
        return std::unexpected(manifest.error());
    }
    auto details = parseWingetManifest(*manifest, locale);
    if (!details) {
        return std::unexpected(details.error());
    }
    if (details->id.empty()) {
        details->id = package->id;
    }
    if (details->version.empty()) {
        details->version = version.version;
    }
    if (details->name.empty()) {
        details->name = package->name;
    }
    return details;
}

Result<std::filesystem::path> fetchWingetIcon(const WingetDetails& details, const std::filesystem::path& cache,
                                              const CancelToken& cancel) {
    if (details.iconUrl.empty() || !text::istartsWith(details.iconUrl, L"https://")) {
        return fail(ErrorCode::NotFound, L"the package has no icon", details.id);
    }
    std::wstring extension = std::filesystem::path(details.iconUrl).extension().wstring();
    if (extension.empty() || extension.size() > 5) {
        extension = L".ico";
    }
    const std::wstring name = details.iconSha256.empty() ? safeName(details.id) : details.iconSha256;
    const auto file = cache / L"icons" / (name + text::lower(extension));
    if (auto bytes = fetchChecked(details.iconUrl, file, details.iconSha256, cancel); !bytes) {
        return std::unexpected(bytes.error());
    }
    return file;
}

// ---- pure parts --------------------------------------------------------------------------------

Result<std::string> decompressMszip(std::span<const std::byte> data) {
    DECOMPRESSOR_HANDLE handle = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &handle)) {
        return fail(ErrorCode::Unsupported, L"MSZIP decompression is not available", {}, static_cast<std::int32_t>(GetLastError()));
    }
    SIZE_T needed = 0;
    Decompress(handle, data.data(), data.size(), nullptr, 0, &needed);
    std::string out(needed, '\0');
    const bool ok = needed > 0 && Decompress(handle, data.data(), data.size(), out.data(), out.size(), &needed);
    CloseDecompressor(handle);
    if (!ok) {
        return fail(ErrorCode::ParseError, L"not MSZIP data", {}, static_cast<std::int32_t>(GetLastError()));
    }
    out.resize(needed);
    return out;
}

Result<std::vector<WingetVersion>> parseVersionData(std::string_view yaml) {
    auto doc = parseYaml(yaml);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    std::vector<WingetVersion> out;
    const auto list = doc->find("vD");
    if (list == doc->end() || !list->is_array()) {
        return fail(ErrorCode::ParseError, L"not a winget version list");
    }
    for (const auto& entry : *list) {
        WingetVersion v{text(entry, "v"), text(entry, "rP"), text::lower(text(entry, "s256H"))};
        if (!v.version.empty() && !v.relativePath.empty() && v.relativePath.find(L"..") == std::wstring::npos) {
            out.push_back(std::move(v));
        }
    }
    return out;
}

Result<WingetDetails> parseWingetManifest(std::string_view yaml, std::wstring_view locale) {
    auto parsed = parseYaml(yaml);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    const nlohmann::json& doc = *parsed;
    WingetDetails d;
    d.id = text(doc, "PackageIdentifier");
    d.version = text(doc, "PackageVersion");
    d.name = text(doc, "PackageName");
    d.publisher = text(doc, "Publisher");
    d.author = text(doc, "Author");
    d.shortDescription = text(doc, "ShortDescription");
    d.description = text(doc, "Description");
    d.license = text(doc, "License");
    d.licenseUrl = text(doc, "LicenseUrl");
    d.homepage = text(doc, "PackageUrl");
    if (d.homepage.empty()) {
        d.homepage = text(doc, "PublisherUrl");
    }
    d.releaseNotesUrl = text(doc, "ReleaseNotesUrl");
    d.tags = texts(doc, "Tags");
    // The localized texts, when the manifest has the locale (or its language) — "tr-TR", then "tr".
    if (!locale.empty()) {
        const std::wstring language = std::wstring(locale.substr(0, locale.find(L'-')));
        const nlohmann::json* best = nullptr;
        if (const auto list = doc.find("Localization"); list != doc.end() && list->is_array()) {
            for (const auto& entry : *list) {
                const std::wstring tag = text(entry, "PackageLocale");
                if (text::iequals(tag, locale)) {
                    best = &entry;
                    break;
                }
                if (!best && text::iequals(tag.substr(0, tag.find(L'-')), language)) {
                    best = &entry;
                }
            }
        }
        if (best) {
            auto take = [&](std::wstring& field, const char* key) {
                if (std::wstring value = text(*best, key); !value.empty()) {
                    field = std::move(value);
                }
            };
            take(d.shortDescription, "ShortDescription");
            take(d.description, "Description");
            take(d.license, "License");
            if (auto localized = texts(*best, "Tags"); !localized.empty()) {
                d.tags = std::move(localized);
            }
        }
    }
    // The icon: a default-theme one, .ico before .png.
    if (const auto icons = doc.find("Icons"); icons != doc.end() && icons->is_array()) {
        int bestScore = -1;
        for (const auto& icon : *icons) {
            const std::wstring url = text(icon, "IconUrl");
            if (url.empty()) {
                continue;
            }
            const std::wstring theme = text(icon, "IconTheme");
            const std::wstring type = text(icon, "IconFileType");
            const int score = (theme.empty() || text::iequals(theme, L"default") ? 2 : 0) + (text::iequals(type, L"ico") ? 1 : 0);
            if (score > bestScore) {
                bestScore = score;
                d.iconUrl = url;
                d.iconSha256 = text::lower(text(icon, "IconSha256"));
            }
        }
    }
    // Tags that are Microsoft Store product ids ("9ncbcszsjrsb", "xpdc2rh70k22mn") say nothing to a reader.
    std::erase_if(d.tags, [](const std::wstring& tag) {
        const bool storeId = (tag.size() == 12 && tag.front() == L'9') || (tag.size() == 14 && text::istartsWith(tag, L"xp"));
        return storeId && std::ranges::all_of(tag, [](wchar_t c) { return std::iswalnum(c) != 0; }) &&
               std::ranges::any_of(tag, [](wchar_t c) { return std::iswdigit(c) != 0; });
    });
    // Installers: the root's values are the defaults of every installer.
    const std::wstring rootType = text(doc, "InstallerType");
    const std::wstring rootScope = text(doc, "Scope");
    if (const auto installers = doc.find("Installers"); installers != doc.end() && installers->is_array()) {
        for (const auto& installer : *installers) {
            std::wstring type = text(installer, "InstallerType");
            addOnce(d.installerTypes, type.empty() ? rootType : type);
            std::wstring scope = text(installer, "Scope");
            addOnce(d.scopes, scope.empty() ? rootScope : scope);
            addOnce(d.architectures, text(installer, "Architecture"));
        }
    }
    return d;
}

bool validWingetId(std::wstring_view id) noexcept {
    return !id.empty() && id.size() <= 128 && std::ranges::all_of(id, [](wchar_t c) {
        return c < 128 && (std::iswalnum(c) != 0 || c == L'.' || c == L'-' || c == L'_' || c == L'+');
    });
}

std::wstring sha256Hex(std::span<const std::byte> data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char digest[32] = {};
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    if (ok) {
        ok = BCRYPT_SUCCESS(BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
                                       static_cast<ULONG>(data.size()), digest, sizeof(digest)));
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok ? hexOf(digest) : std::wstring();
}

} // namespace wl::core
