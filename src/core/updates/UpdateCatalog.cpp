#include "core/updates/UpdateCatalog.h"

#include "base/Encoding.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/WindowsRelease.h"
#include "core/iso/IsoBuilder.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <map>

namespace wl::core {

namespace {

constexpr std::wstring_view kSearchUrl = L"https://www.catalog.update.microsoft.com/Search.aspx?q=";
constexpr std::wstring_view kDownloadUrl = L"https://www.catalog.update.microsoft.com/DownloadDialog.aspx";

bool contains(std::wstring_view text, std::wstring_view part) {
    return text.find(part) != std::wstring_view::npos;
}

// Text of an HTML fragment: tags dropped, the entities the catalog uses decoded, whitespace
// collapsed to single spaces.
std::wstring cellText(std::string_view html) {
    std::string text;
    bool tag = false;
    for (const char c : html) {
        if (c == '<') {
            tag = true;
        } else if (c == '>') {
            tag = false;
            text.push_back(' ');
        } else if (!tag) {
            text.push_back(c);
        }
    }
    static constexpr std::pair<std::string_view, std::string_view> kEntities[] = {
        {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}, {"&reg;", ""}};
    for (const auto& [entity, plain] : kEntities) {
        for (std::size_t at = text.find(entity); at != std::string::npos; at = text.find(entity, at + plain.size())) {
            text.replace(at, entity.size(), plain);
        }
    }
    std::wstring wide = utf8::toWide(text);
    std::wstring out;
    bool space = false;
    for (const wchar_t c : wide) {
        if (std::iswspace(c)) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(L' ');
            space = false;
        }
        out.push_back(c);
    }
    return out;
}

bool isGuid(std::string_view text) {
    if (text.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? c != '-' : !std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// "…(26200.9457)" at the end of a title.
void parseBuild(std::wstring_view title, int& build, int& revision) {
    const auto close = title.rfind(L')');
    const auto open = title.rfind(L'(');
    if (close == std::wstring_view::npos || open == std::wstring_view::npos || open > close) {
        return;
    }
    const std::wstring inner(title.substr(open + 1, close - open - 1));
    int b = 0;
    int r = 0;
    if (swscanf_s(inner.c_str(), L"%d.%d", &b, &r) == 2 && b >= 10000 && inner.find_first_not_of(L"0123456789.") == std::wstring::npos) {
        build = b;
        revision = r;
    }
}

} // namespace

CatalogTarget catalogTarget(int build, int revision, std::wstring_view architecture) {
    CatalogTarget target;
    target.windows = build >= 22000 ? 11 : 10;
    target.build = build;
    target.revision = revision;
    target.architecture = text::lower(architecture.empty() ? L"x64" : architecture);
    if (target.architecture == L"amd64") {
        target.architecture = L"x64";
    }
    const std::wstring label = releaseLabel(build); // "11 25H2" or "11 build 27000"
    if (!contains(label, L"build")) {
        target.release = label.substr(label.find(L' ') + 1);
    }
    return target;
}

void classifyCatalogEntry(CatalogEntry& entry) {
    const std::wstring title = text::lower(entry.title);
    entry.preview = contains(title, L"cumulative update preview");
    if (contains(title, L"safe os dynamic update")) {
        entry.kind = CatalogKind::SafeOs;
    } else if (contains(title, L"dynamic update")) {
        entry.kind = CatalogKind::Setup;
    } else if (contains(title, L".net framework") && contains(title, L"cumulative update")) {
        entry.kind = CatalogKind::DotNet;
    } else if (contains(title, L"cumulative update for windows") || contains(title, L"cumulative update preview for windows")) {
        entry.kind = CatalogKind::Cumulative;
    } else {
        entry.kind = CatalogKind::Other;
    }
    if (const auto at = entry.title.find(L"(KB"); at != std::wstring::npos) {
        const auto end = entry.title.find(L')', at);
        if (end != std::wstring::npos) {
            entry.kb = entry.title.substr(at + 1, end - at - 1);
        }
    }
    parseBuild(entry.title, entry.build, entry.revision);
}

std::vector<CatalogEntry> parseCatalogSearch(std::string_view html) {
    std::vector<CatalogEntry> entries;
    constexpr std::string_view rowStart = "<tr id=\"";
    for (std::size_t at = html.find(rowStart); at != std::string_view::npos; at = html.find(rowStart, at + 1)) {
        const std::size_t idStart = at + rowStart.size();
        const std::size_t quote = html.find('"', idStart);
        if (quote == std::string_view::npos) {
            break;
        }
        const std::string_view rowId = html.substr(idStart, quote - idStart); // "<guid>_R3"
        const auto underscore = rowId.find("_R");
        if (underscore == std::string_view::npos || !isGuid(rowId.substr(0, underscore))) {
            continue;
        }
        const std::size_t end = std::min(html.find("</tr>", quote), html.size());
        const std::string_view row = html.substr(quote, end - quote);
        auto cell = [&](int column) -> std::wstring {
            const std::string marker = std::format("_C{}_R", column);
            const auto c = row.find(marker);
            if (c == std::string_view::npos) {
                return {};
            }
            const auto open = row.find('>', c);
            const auto close = row.find("</td>", open);
            return open == std::string_view::npos || close == std::string_view::npos
                       ? std::wstring()
                       : cellText(row.substr(open + 1, close - open - 1));
        };
        CatalogEntry entry;
        entry.id = utf8::toWide(rowId.substr(0, underscore));
        entry.title = cell(1);
        entry.products = cell(2);
        entry.classification = cell(3);
        const std::wstring date = cell(4);
        if (swscanf_s(date.c_str(), L"%d/%d/%d", &entry.month, &entry.day, &entry.year) != 3) {
            entry.year = entry.month = entry.day = 0;
        }
        constexpr std::string_view sizeMarker = "_originalSize\">";
        if (const auto s = row.find(sizeMarker); s != std::string_view::npos) {
            entry.size = std::strtoull(std::string(row.substr(s + sizeMarker.size(), 20)).c_str(), nullptr, 10);
        }
        if (entry.title.empty()) {
            continue;
        }
        classifyCatalogEntry(entry);
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<CatalogFile> parseCatalogDownload(std::string_view script) {
    // downloadInformation[0].files[1].url = '…'; — only the first update's files.
    std::map<int, CatalogFile> files;
    constexpr std::string_view prefix = "downloadInformation[0].files[";
    for (std::size_t at = script.find(prefix); at != std::string_view::npos; at = script.find(prefix, at + 1)) {
        const std::size_t indexStart = at + prefix.size();
        const std::size_t indexEnd = script.find("].", indexStart);
        if (indexEnd == std::string_view::npos || indexEnd - indexStart > 3) {
            continue;
        }
        const int index = std::atoi(std::string(script.substr(indexStart, indexEnd - indexStart)).c_str());
        const std::size_t nameStart = indexEnd + 2;
        const std::size_t eq = script.find(" = '", nameStart);
        const std::size_t lineEnd = script.find('\n', nameStart);
        if (eq == std::string_view::npos || (lineEnd != std::string_view::npos && eq > lineEnd)) {
            continue;
        }
        const std::string_view field = script.substr(nameStart, eq - nameStart);
        const std::size_t valueStart = eq + 4;
        const std::size_t valueEnd = script.find('\'', valueStart);
        if (valueEnd == std::string_view::npos) {
            continue;
        }
        const std::string_view value = script.substr(valueStart, valueEnd - valueStart);
        auto& file = files[index];
        if (field == "url") {
            file.url = utf8::toWide(value);
        } else if (field == "fileName") {
            file.fileName = utf8::toWide(value);
        } else if (field == "sha256") {
            file.sha256 = base64Decode(value);
            if (file.sha256.size() != 32) {
                file.sha256.clear();
            }
        }
    }
    std::vector<CatalogFile> out;
    for (auto& [index, file] : files) {
        if (!file.url.empty()) {
            if (file.fileName.empty()) {
                file.fileName = file.url.substr(file.url.rfind(L'/') + 1);
            }
            out.push_back(std::move(file));
        }
    }
    return out;
}

bool matchesTarget(const CatalogEntry& entry, const CatalogTarget& target) {
    if (entry.kind != CatalogKind::Cumulative && entry.kind != CatalogKind::DotNet) {
        return false;
    }
    if (target.release.empty()) {
        return false;
    }
    const std::wstring title = text::lower(entry.title);
    if (contains(title, L"hotpatch") || contains(title, L"server") || contains(title, L"azure")) {
        return false;
    }
    const std::wstring release = text::lower(target.release);
    const std::wstring product = target.windows == 11 ? L"windows 11" : L"windows 10";
    if (!contains(title, product + L", version " + release) && !contains(title, product + L" version " + release)) {
        return false;
    }
    const std::wstring arch = text::lower(target.architecture);
    return contains(title, L"for " + arch + L"-based systems") || contains(title, L"for " + arch + L" (");
}

std::vector<std::wstring> catalogQueries(const CatalogTarget& target) {
    if (target.release.empty()) {
        return {};
    }
    const std::wstring product = std::format(L"Windows {} Version {}", target.windows, target.release);
    return {std::format(L"Cumulative Update for {} for {}", product, target.architecture),
            std::format(L"Cumulative Update .NET Framework {} {}", product, target.architecture)};
}

std::vector<CatalogOffer> pickCatalogOffers(const std::vector<CatalogEntry>& entries, const CatalogTarget& target) {
    std::map<std::wstring, const CatalogEntry*> unique;
    for (const auto& e : entries) {
        if (matchesTarget(e, target)) {
            unique.emplace(e.id, &e);
        }
    }
    // Newer first; on the same day the higher revision, then (.NET on Windows 10 ships 3.5 + 4.8,
    // 3.5 + 4.8.1 and a combined package the same day) the title naming more frameworks.
    auto newer = [](const CatalogEntry* a, const CatalogEntry* b) {
        if (a->dateKey() != b->dateKey()) {
            return a->dateKey() > b->dateKey();
        }
        if (a->revision != b->revision) {
            return a->revision > b->revision;
        }
        return a->title.size() > b->title.size();
    };
    std::vector<CatalogOffer> offers;
    for (const CatalogKind kind : {CatalogKind::Cumulative, CatalogKind::DotNet}) {
        std::vector<const CatalogEntry*> released;
        std::vector<const CatalogEntry*> previews;
        for (const auto& [id, e] : unique) {
            if (e->kind == kind) {
                (e->preview ? previews : released).push_back(e);
            }
        }
        std::ranges::sort(released, newer);
        std::ranges::sort(previews, newer);
        const CatalogEntry* best = released.empty() ? nullptr : released.front();
        auto offer = [&](const CatalogEntry& e, bool recommended) {
            CatalogOffer o{e, recommended, false};
            o.olderThanImage = e.build == target.build && e.revision > 0 && e.revision <= target.revision;
            offers.push_back(std::move(o));
        };
        if (best) {
            offer(*best, true);
        }
        if (!previews.empty() && (!best || previews.front()->dateKey() > best->dateKey())) {
            offer(*previews.front(), false);
        }
    }
    return offers;
}

bool trustedDownloadUrl(std::wstring_view url) {
    const std::wstring u = text::lower(url);
    std::wstring_view rest;
    if (u.starts_with(L"https://")) {
        rest = std::wstring_view(u).substr(8);
    } else if (u.starts_with(L"http://")) {
        rest = std::wstring_view(u).substr(7); // allowed below only for windowsupdate.com (hash-checked)
    } else {
        return false;
    }
    // The authority ends at the first "/?#"; "user:pass@" before the host would make a check on
    // the text before ":" pass for a host WinHTTP never connects to
    // ("https://download.microsoft.com:x@evil.example/").
    const std::wstring_view authority = rest.substr(0, rest.find_first_of(L"/?#"));
    if (authority.find_first_of(L"@\\") != std::wstring_view::npos) {
        return false;
    }
    const std::wstring_view host = authority.substr(0, authority.find(L':'));
    auto under = [&](std::wstring_view domain) {
        return host == domain.substr(1) || (host.size() > domain.size() && host.ends_with(domain));
    };
    if (under(L".windowsupdate.com")) {
        return true;
    }
    return u.starts_with(L"https://") && under(L".microsoft.com");
}

Result<std::vector<CatalogOffer>> findCatalogUpdates(const CatalogTarget& target, const CancelToken& cancel) {
    const auto queries = catalogQueries(target);
    if (queries.empty()) {
        return fail(ErrorCode::Unsupported, L"the update catalog has no name for this Windows build",
                    std::format(L"{}.{}", target.build, target.revision));
    }
    std::vector<CatalogEntry> entries;
    for (const auto& query : queries) {
        const std::wstring url = std::wstring(kSearchUrl) + utf8::toWide(urlEncode(utf8::fromWide(query)));
        auto page = httpGet(url, cancel);
        if (!page) {
            return std::unexpected(page.error());
        }
        if (page->status != 200) {
            return fail(ErrorCode::IoError, std::format(L"the update catalog answered HTTP {}", page->status), url);
        }
        auto found = parseCatalogSearch(page->body);
        log::info("updates", std::format(L"catalog search \"{}\": {} entries", query, found.size()));
        entries.insert(entries.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    }
    return pickCatalogOffers(entries, target);
}

Result<std::vector<CatalogFile>> catalogFiles(std::wstring_view updateId, const CancelToken& cancel) {
    const std::string id = utf8::fromWide(updateId);
    if (!isGuid(id)) {
        return fail(ErrorCode::InvalidArgument, L"not an update id", std::wstring(updateId));
    }
    const std::string ids = std::format(R"([{{"size":0,"languages":"","uidInfo":"{0}","updateID":"{0}"}}])", id);
    auto page = httpPostForm(kDownloadUrl, "updateIDs=" + urlEncode(ids), cancel);
    if (!page) {
        return std::unexpected(page.error());
    }
    if (page->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"the update catalog answered HTTP {}", page->status),
                    std::wstring(kDownloadUrl));
    }
    auto files = parseCatalogDownload(page->body);
    if (files.empty()) {
        return fail(ErrorCode::NotFound, L"the update catalog lists no file for this update", std::wstring(updateId));
    }
    return files;
}

Result<DownloadedUpdate> downloadCatalogUpdate(const CatalogEntry& entry, const std::filesystem::path& folder,
                                               const TaskContext& task) {
    auto files = catalogFiles(entry.id, task.cancel);
    if (!files) {
        return std::unexpected(files.error());
    }
    DownloadedUpdate result;
    const std::wstring kb = text::lower(entry.kb);
    std::uint64_t base = 0;
    const double total = static_cast<double>(std::max<std::uint64_t>(entry.size, 1));
    for (const auto& file : *files) {
        // A name from a web page: no folders, no drive, no stream.
        if (file.fileName.empty() || file.fileName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
            file.fileName.starts_with(L".")) {
            return fail(ErrorCode::InvalidArgument, L"unexpected file name in the update catalog", file.fileName);
        }
        if (!trustedDownloadUrl(file.url) || (file.sha256.empty() && !text::lower(file.url).starts_with(L"https://"))) {
            return fail(ErrorCode::AccessDenied, L"download address is not a Microsoft server", file.url);
        }
        const std::filesystem::path target = folder / file.fileName;
        const TaskContext quiet{task.cancel, {}};
        bool have = false;
        std::error_code ec;
        if (std::filesystem::is_regular_file(target, ec) && !file.sha256.empty()) {
            auto hash = sha256File(target, quiet);
            have = hash && *hash == hexLower(file.sha256);
        }
        if (!have) {
            // A redirect must land on a Microsoft server too (WinHTTP follows it on its own).
            auto got = httpDownload(
                file.url, target, 0, quiet,
                [&](std::uint64_t done, std::uint64_t) {
                    task.report(std::min(static_cast<double>(base + done) / total, 1.0), L"download");
                },
                [&](std::wstring_view finalUrl) {
                    return trustedDownloadUrl(finalUrl) && (!file.sha256.empty() || text::lower(finalUrl).starts_with(L"https://"));
                });
            if (!got) {
                return std::unexpected(got.error());
            }
            result.bytes += *got;
            base += *got;
            if (!file.sha256.empty()) {
                task.report(-1.0, L"verify");
                auto hash = sha256File(target, quiet);
                if (!hash) {
                    return std::unexpected(hash.error());
                }
                if (*hash != hexLower(file.sha256)) {
                    std::filesystem::remove(target, ec);
                    return fail(ErrorCode::IoError, L"the downloaded file does not match the catalog's SHA-256",
                                target.wstring());
                }
            }
            log::info("updates", std::format(L"downloaded {} ({} bytes{})", target.wstring(), *got,
                                             file.sha256.empty() ? L", no hash to check" : L", SHA-256 ok"));
        } else {
            log::info("updates", L"already downloaded: " + target.wstring());
        }
        const bool main = !kb.empty() && contains(text::lower(file.fileName), kb);
        (main && result.main.empty() ? result.main : result.prerequisites.emplace_back()) = target;
    }
    if (result.main.empty()) { // no file carries the KB: the first one is the package
        result.main = result.prerequisites.front();
        result.prerequisites.erase(result.prerequisites.begin());
    }
    task.report(1.0, L"done");
    return result;
}

} // namespace wl::core
