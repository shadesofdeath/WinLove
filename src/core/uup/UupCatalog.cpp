#include "core/uup/UupCatalog.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/net/Http.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <regex>
#include <thread>

namespace wl::core::uup {

namespace {

using nlohmann::json;

constexpr wchar_t kApi[] = L"https://api.uupdump.net/";

std::wstring str(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return utf8::toWide(it->get<std::string>());
    }
    return utf8::toWide(it->dump());
}

std::uint64_t number(const json& j, const char* key) {
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

// {"response": {...}} → the response, or its "error" as an Error.
Result<json> response(std::string_view text) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return fail(ErrorCode::ParseError, L"the UUP API did not answer with JSON");
    }
    const auto it = root.find("response");
    if (it == root.end() || !it->is_object()) {
        return fail(ErrorCode::ParseError, L"the UUP API's answer has no response");
    }
    if (const auto e = it->find("error"); e != it->end() && e->is_string()) {
        const std::wstring code = utf8::toWide(e->get<std::string>());
        const ErrorCode kind = code == L"UNSUPPORTED_LANG" || code == L"UNSUPPORTED_COMBINATION" || code == L"NO_FILES"
                                   ? ErrorCode::NotFound
                                   : ErrorCode::IoError;
        return fail(kind, code, L"UUP API");
    }
    return *it;
}

std::wstring hex(std::wstring text) {
    return text::lower(text);
}

Result<std::string> get(const std::wstring& url, const CancelToken& cancel) {
    auto r = httpGet(url, cancel);
    if (!r) {
        return std::unexpected(r.error());
    }
    if (r->status == 429 || r->status / 100 != 2) {
        // The API answers errors with a JSON body and a 4xx / 5xx status: keep the body.
        if (!r->body.empty() && r->body.front() == '{') {
            return std::move(r->body);
        }
        return fail(ErrorCode::IoError, std::format(L"HTTP {}", r->status), url);
    }
    return std::move(r->body);
}

std::wstring query(std::wstring_view text) {
    return utf8::toWide(urlEncode(utf8::fromWide(text)));
}

} // namespace

std::uint64_t FileSet::totalSize() const noexcept {
    std::uint64_t total = 0;
    for (const auto& f : files) {
        total += f.size;
    }
    return total;
}

BuildKind buildKind(std::wstring_view title) {
    const std::wstring t = text::lower(title);
    if (t.find(L"server") != std::wstring::npos) {
        return BuildKind::Server;
    }
    if (t.find(L"insider preview") != std::wstring::npos) {
        // "Windows 11 Insider Preview 27965.1000 (rs_prerelease)"; an Insider cumulative update
        // is still an update.
        return t.find(L"cumulative update") != std::wstring::npos ? BuildKind::Update : BuildKind::Insider;
    }
    // "Windows 11, version 26H2 (26300.9550)", "Windows 10, version 22H2 (19045.6332)"
    static const std::wregex release{LR"(^windows 1[01], version \w+ \()"};
    if (std::regex_search(t, release)) {
        return BuildKind::Release;
    }
    return BuildKind::Update;
}

FileKind fileKind(std::wstring_view nameView) {
    const std::wstring name = text::lower(nameView);
    const auto ends = [&](std::wstring_view s) { return name.ends_with(s); };
    if (name == L"edge.wim") {
        return FileKind::Edge;
    }
    if (ends(L".msu")) {
        return FileKind::Msu;
    }
    if (ends(L".msix") || ends(L".appx") || ends(L".msixbundle") || ends(L".appxbundle") || ends(L".eappx") ||
        ends(L".emsix") || ends(L".eappxbundle") || ends(L".emsixbundle") || name.starts_with(L"appxmetadata") ||
        name.starts_with(L"appx/") || name.starts_with(L"appx\\") || ends(L"_license.xml") || ends(L".appxbundle.xml")) {
        return FileKind::App;
    }
    if (ends(L".cab")) {
        return FileKind::Cab;
    }
    if (ends(L".esd")) {
        // professional_tr-tr.esd, core_en-us.esd: an edition's metadata (edition_language.esd);
        // everything else (Microsoft-Windows-…-Package.ESD) lends it files.
        static const std::wregex metadata{LR"(^[a-z]+_[a-z]{2,3}(-[a-z]{2,4}){1,2}\.esd$)"};
        return std::regex_match(name, metadata) ? FileKind::Metadata : FileKind::PackageEsd;
    }
    return FileKind::Other;
}

Result<std::vector<Build>> parseBuilds(std::string_view text) {
    auto r = response(text);
    if (!r) {
        return std::unexpected(r.error());
    }
    std::vector<Build> builds;
    const auto it = r->find("builds");
    if (it == r->end()) {
        return builds;
    }
    auto add = [&](const json& b) {
        if (!b.is_object()) {
            return;
        }
        Build build;
        build.id = str(b, "uuid");
        build.title = str(b, "title");
        build.build = str(b, "build");
        build.arch = str(b, "arch");
        build.created = static_cast<std::int64_t>(number(b, "created"));
        build.kind = buildKind(build.title);
        if (!build.id.empty()) {
            builds.push_back(std::move(build));
        }
    };
    if (it->is_object()) {
        for (const auto& [key, b] : it->items()) {
            add(b);
        }
    } else if (it->is_array()) {
        for (const auto& b : *it) {
            add(b);
        }
    }
    std::ranges::stable_sort(builds, [](const Build& a, const Build& b) { return a.created > b.created; });
    return builds;
}

Result<std::vector<Language>> parseLanguages(std::string_view text) {
    auto r = response(text);
    if (!r) {
        return std::unexpected(r.error());
    }
    std::vector<Language> list;
    const json names = r->value("langFancyNames", json::object());
    for (const auto& code : r->value("langList", json::array())) {
        if (!code.is_string()) {
            continue;
        }
        Language l;
        l.code = utf8::toWide(code.get<std::string>());
        l.name = names.contains(code.get<std::string>()) ? str(names, code.get<std::string>().c_str()) : l.code;
        list.push_back(std::move(l));
    }
    std::ranges::sort(list, [](const Language& a, const Language& b) { return a.name < b.name; });
    return list;
}

Result<std::vector<Edition>> parseEditions(std::string_view text) {
    auto r = response(text);
    if (!r) {
        return std::unexpected(r.error());
    }
    std::vector<Edition> list;
    const json names = r->value("editionFancyNames", json::object());
    for (const auto& code : r->value("editionList", json::array())) {
        if (!code.is_string()) {
            continue;
        }
        Edition e;
        e.code = utf8::toWide(code.get<std::string>());
        e.name = names.contains(code.get<std::string>()) ? str(names, code.get<std::string>().c_str()) : e.code;
        list.push_back(std::move(e));
    }
    return list;
}

Result<FileSet> parseFiles(std::string_view text) {
    auto r = response(text);
    if (!r) {
        return std::unexpected(r.error());
    }
    FileSet set;
    set.updateName = str(*r, "updateName");
    set.build = str(*r, "build");
    set.arch = str(*r, "arch");
    set.hasUpdates = r->value("hasUpdates", false);
    set.appxPresent = r->value("appxPresent", false);
    const auto files = r->find("files");
    if (files != r->end() && files->is_object()) {
        for (const auto& [name, f] : files->items()) {
            File file;
            file.name = utf8::toWide(name);
            file.size = number(f, "size");
            file.sha256 = hex(str(f, "sha256"));
            file.sha1 = hex(str(f, "sha1"));
            file.url = str(f, "url");
            file.kind = fileKind(file.name);
            set.files.push_back(std::move(file));
        }
    }
    std::ranges::sort(set.files, [](const File& a, const File& b) { return a.name < b.name; });
    return set;
}

// ---- network ----

namespace {

// One retry after the API's 10 s window when it says USER_RATE_LIMITED.
template <class Parse>
auto ask(const std::wstring& url, const CancelToken& cancel, Parse parse) -> decltype(parse(std::string_view{})) {
    for (int attempt = 0;; ++attempt) {
        auto body = get(url, cancel);
        if (!body) {
            return std::unexpected(body.error());
        }
        auto parsed = parse(*body);
        if (parsed || parsed.error().message != L"USER_RATE_LIMITED" || attempt > 0) {
            if (!parsed) {
                log::warn("uup", std::format(L"{}: {}", url, parsed.error().message));
            }
            return parsed;
        }
        log::info("uup", L"rate limited: waiting 11 s");
        for (int i = 0; i < 110; ++i) {
            if (cancel.cancelled()) {
                return fail(ErrorCode::Cancelled, L"cancelled", url);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace

Result<std::vector<Build>> listBuilds(std::wstring_view search, const CancelToken& cancel) {
    std::wstring url = std::wstring(kApi) + L"listid.php?sortByDate=1";
    if (!search.empty()) {
        url += L"&search=" + query(search);
    }
    return ask(url, cancel, parseBuilds);
}

Result<std::vector<Language>> listLanguages(std::wstring_view id, const CancelToken& cancel) {
    return ask(std::wstring(kApi) + L"listlangs.php?id=" + query(id), cancel, parseLanguages);
}

Result<std::vector<Edition>> listEditions(std::wstring_view id, std::wstring_view language, const CancelToken& cancel) {
    return ask(std::wstring(kApi) + L"listeditions.php?id=" + query(id) + L"&lang=" + query(language), cancel,
               parseEditions);
}

Result<FileSet> listFiles(std::wstring_view id, std::wstring_view language, std::span<const std::wstring> editions,
                          bool links, const CancelToken& cancel) {
    std::wstring url = std::wstring(kApi) + L"get.php?id=" + query(id) + L"&lang=" + query(language);
    if (editions.size() == 1) {
        url += L"&edition=" + query(text::lower(editions.front()));
    } else {
        for (const auto& e : editions) {
            url += L"&edition%5B%5D=" + query(text::lower(e)); // edition[]=…: the sets of several, merged
        }
    }
    if (!links) {
        url += L"&noLinks=1";
    }
    return ask(url, cancel, parseFiles);
}

} // namespace wl::core::uup
