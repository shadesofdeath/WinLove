#include "core/updates/UupLanguages.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <format>
#include <map>

namespace wl::core {

namespace {

constexpr std::wstring_view kListUrl = L"https://api.uupdump.net/listid.php?sortByDate=1&search=";
constexpr std::wstring_view kFilesUrl = L"https://api.uupdump.net/get.php?id=";

std::wstring wide(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (it->is_string()) {
        return utf8::toWide(it->get<std::string>());
    }
    if (it->is_number()) {
        return std::to_wstring(it->get<std::int64_t>());
    }
    return {};
}

std::uint64_t number(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return 0;
    }
    if (it->is_number_unsigned() || it->is_number_integer()) {
        return it->get<std::uint64_t>();
    }
    if (it->is_string()) {
        try {
            return std::stoull(it->get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

// The API's answer: {"response": {...}} or {"response": {"error": "…"}}.
Result<nlohmann::json> response(std::string_view json) {
    auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("response") || !doc["response"].is_object()) {
        return fail(ErrorCode::ParseError, L"uupdump.net did not answer with JSON");
    }
    auto r = std::move(doc["response"]);
    if (const auto e = r.find("error"); e != r.end() && e->is_string()) {
        return fail(ErrorCode::NotFound, L"uupdump.net: " + utf8::toWide(e->get<std::string>()));
    }
    return r;
}

std::wstring uupArch(std::wstring_view architecture) {
    return architecture == L"x64" ? L"amd64" : std::wstring(architecture);
}

bool isPreviewTitle(std::wstring_view title) {
    const std::wstring t = text::lower(title);
    return t.find(L"insider") != std::wstring::npos || t.find(L"preview") != std::wstring::npos;
}

bool validUuid(std::wstring_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::ranges::all_of(id, [](wchar_t c) { return c == L'-' || std::iswxdigit(c) != 0; });
}

bool safeFileName(std::wstring_view name) {
    return !name.empty() && name.size() < 200 && name.find_first_of(L"\\/:*?\"<>|") == std::wstring_view::npos &&
           !name.starts_with(L".");
}

Result<std::string> getJson(const std::wstring& url, const CancelToken& cancel) {
    auto page = httpGet(url, cancel);
    if (!page) {
        return std::unexpected(page.error());
    }
    if (page->status != 200) {
        return fail(ErrorCode::IoError, std::format(L"uupdump.net answered HTTP {}", page->status), url);
    }
    return std::move(page->body);
}

} // namespace

const UupLanguageFile* UupLanguage::find(LanguagePackFile::Kind kind) const {
    const auto it = std::ranges::find_if(files, [&](const UupLanguageFile& f) { return f.file.kind == kind; });
    return it == files.end() ? nullptr : &*it;
}

Result<std::vector<UupBuild>> parseUupBuilds(std::string_view json) {
    auto r = response(json);
    if (!r) {
        return std::unexpected(r.error());
    }
    std::vector<UupBuild> builds;
    const auto list = r->find("builds");
    if (list == r->end() || !(list->is_object() || list->is_array())) {
        return builds;
    }
    for (const auto& b : *list) {
        if (!b.is_object()) {
            continue;
        }
        UupBuild build;
        build.uuid = wide(b, "uuid");
        build.title = wide(b, "title");
        build.arch = text::lower(wide(b, "arch"));
        build.created = static_cast<std::int64_t>(number(b, "created"));
        const std::wstring number = wide(b, "build"); // "26200.8037"
        const auto dot = number.find(L'.');
        try {
            build.build = std::stoi(number.substr(0, dot));
            build.revision = dot == std::wstring::npos ? 0 : std::stoi(number.substr(dot + 1));
        } catch (...) {
            continue;
        }
        if (validUuid(build.uuid)) {
            builds.push_back(std::move(build));
        }
    }
    return builds;
}

std::optional<UupBuild> pickUupBuild(std::span<const UupBuild> builds, int build, int revision, std::wstring_view architecture) {
    const std::wstring arch = uupArch(architecture);
    const UupBuild* best = nullptr;
    auto better = [&](const UupBuild& a, const UupBuild* b) {
        if (!b) {
            return true;
        }
        const bool exactA = a.revision == revision, exactB = b->revision == revision;
        if (exactA != exactB) {
            return exactA;
        }
        const bool previewA = isPreviewTitle(a.title), previewB = isPreviewTitle(b->title);
        if (previewA != previewB) {
            return !previewA;
        }
        return a.revision != b->revision ? a.revision > b->revision : a.created > b->created;
    };
    for (const auto& b : builds) {
        if (b.build == build && b.arch == arch && better(b, best)) {
            best = &b;
        }
    }
    return best ? std::optional<UupBuild>(*best) : std::nullopt;
}

Result<std::vector<UupFile>> parseUupFiles(std::string_view json) {
    auto r = response(json);
    if (!r) {
        return std::unexpected(r.error());
    }
    std::vector<UupFile> files;
    const auto list = r->find("files");
    if (list == r->end() || !list->is_object()) {
        return fail(ErrorCode::ParseError, L"uupdump.net listed no files");
    }
    for (const auto& [name, f] : list->items()) {
        if (!f.is_object()) {
            continue;
        }
        UupFile file;
        file.name = utf8::toWide(name);
        file.size = number(f, "size");
        file.sha1 = text::lower(wide(f, "sha1"));
        file.sha256 = text::lower(wide(f, "sha256"));
        file.url = wide(f, "url");
        files.push_back(std::move(file));
    }
    return files;
}

std::vector<UupLanguage> uupLanguages(std::span<const UupFile> files, std::wstring_view architecture) {
    std::map<std::wstring, std::vector<UupLanguageFile>> byLanguage; // "tr-TR" → its files
    std::map<std::wstring, UupLanguageFile> fonts;                   // "jpan" → the font feature
    std::vector<std::wstring> packs;
    for (const auto& source : files) {
        UupLanguageFile f{classifyLanguageName(source.name), source};
        f.file.size = source.size;
        if (f.file.kind == LanguagePackFile::Kind::Other || f.file.architecture != architecture) {
            continue;
        }
        if (f.file.kind == LanguagePackFile::Kind::Fonts) {
            fonts.emplace(text::lower(f.file.component), std::move(f));
            continue;
        }
        if (f.file.kind == LanguagePackFile::Kind::LanguagePack) {
            packs.push_back(f.file.language);
        }
        byLanguage[f.file.language].push_back(std::move(f));
    }
    std::vector<UupLanguage> languages;
    std::ranges::sort(packs);
    for (const auto& tag : packs) {
        UupLanguage language{tag, byLanguage[tag]};
        for (const auto& script : requiredFontScripts(tag)) {
            if (const auto it = fonts.find(text::lower(script)); it != fonts.end()) {
                language.files.push_back(it->second);
            }
        }
        std::ranges::stable_sort(language.files, [](const UupLanguageFile& a, const UupLanguageFile& b) {
            if (a.file.kind != b.file.kind) {
                return static_cast<int>(a.file.kind) < static_cast<int>(b.file.kind);
            }
            return a.source.name < b.source.name;
        });
        languages.push_back(std::move(language));
    }
    return languages;
}

bool trustedUupUrl(std::wstring_view url) {
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
        return false; // "http://microsoft.com@evil.example/": user info hides the real host
    }
    const std::wstring_view host = authority.substr(0, authority.find(L':'));
    auto under = [&](std::wstring_view domain) { return host.size() > domain.size() && host.ends_with(domain); };
    return under(L".microsoft.com") || under(L".windowsupdate.com");
}

std::wstring uupSaveName(const UupLanguageFile& file) {
    LanguagePackFile f = file.file;
    f.path = file.source.name;
    const std::wstring cbs = cbsFileName(f);
    return cbs.empty() ? file.source.name : cbs;
}

Result<UupBuild> findUupBuild(int build, int revision, std::wstring_view architecture, const CancelToken& cancel) {
    // The exact build first; any revision of it when uupdump.net does not have that one.
    for (const std::wstring& query : {std::format(L"{}.{}", build, revision), std::to_wstring(build)}) {
        auto body = getJson(std::wstring(kListUrl) + query, cancel);
        if (!body) {
            return std::unexpected(body.error());
        }
        auto builds = parseUupBuilds(*body);
        if (!builds) {
            if (builds.error().code == ErrorCode::NotFound) {
                continue; // "NO_SEARCH_RESULTS"
            }
            return std::unexpected(builds.error());
        }
        log::info("languages", std::format(L"uupdump.net search \"{}\": {} build(s)", query, builds->size()));
        if (auto picked = pickUupBuild(*builds, build, revision, architecture)) {
            log::info("languages", std::format(L"language files from {} ({})", picked->title, picked->uuid));
            return *picked;
        }
    }
    return fail(ErrorCode::NotFound, L"uupdump.net knows no update of this build", std::format(L"{}.{} {}", build, revision, architecture));
}

Result<std::vector<UupFile>> uupFiles(std::wstring_view uuid, const CancelToken& cancel) {
    if (!validUuid(uuid)) {
        return fail(ErrorCode::InvalidArgument, L"not an update id", std::wstring(uuid));
    }
    auto body = getJson(std::wstring(kFilesUrl) + std::wstring(uuid), cancel);
    if (!body) {
        return std::unexpected(body.error());
    }
    return parseUupFiles(*body);
}

Result<std::vector<std::filesystem::path>> downloadUupFiles(std::span<const UupLanguageFile> files, const std::filesystem::path& folder,
                                                            const TaskContext& task) {
    std::uint64_t total = 0;
    for (const auto& f : files) {
        total += f.source.size;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::vector<std::filesystem::path> saved;
    std::uint64_t base = 0;
    const TaskContext quiet{task.cancel, {}};
    auto report = [&](std::uint64_t done) {
        task.report(total ? std::min(static_cast<double>(base + done) / static_cast<double>(total), 1.0) : -1.0, L"download");
    };
    for (const auto& f : files) {
        const std::wstring name = uupSaveName(f);
        if (!safeFileName(name)) {
            return fail(ErrorCode::InvalidArgument, L"unexpected file name in the uupdump.net list", name);
        }
        if (f.source.sha256.size() != 64) {
            return fail(ErrorCode::InvalidArgument, L"the uupdump.net list gives no SHA-256 for this file", name);
        }
        if (!trustedUupUrl(f.source.url)) {
            return fail(ErrorCode::AccessDenied, L"download address is not a Microsoft server", f.source.url);
        }
        const auto target = folder / name;
        bool have = false;
        if (std::filesystem::is_regular_file(target, ec)) {
            auto hash = sha256File(target, quiet);
            have = hash && *hash == f.source.sha256;
        }
        if (!have) {
            auto got = httpDownload(f.source.url, target, f.source.size, quiet, [&](std::uint64_t done, std::uint64_t) { report(done); },
                                    [](std::wstring_view finalUrl) { return trustedUupUrl(finalUrl); });
            if (!got) {
                return std::unexpected(got.error());
            }
            auto hash = sha256File(target, quiet);
            if (!hash) {
                return std::unexpected(hash.error());
            }
            if (*hash != f.source.sha256) {
                std::filesystem::remove(target, ec);
                return fail(ErrorCode::IoError, L"the downloaded file does not match its SHA-256", target.wstring());
            }
            log::info("languages", std::format(L"downloaded {} ({} bytes, SHA-256 ok)", target.wstring(), *got));
        } else {
            log::info("languages", L"already downloaded: " + target.wstring());
        }
        base += f.source.size;
        report(0);
        saved.push_back(target);
    }
    task.report(1.0, L"done");
    return saved;
}

} // namespace wl::core
