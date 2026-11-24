#include "core/net/ReleaseCheck.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/net/Http.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>

namespace wl::core {

namespace {

using nlohmann::json;

bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0;
}

std::wstring_view trim(std::wstring_view s) {
    while (!s.empty() && isSpace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isSpace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool isDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

bool isAsciiLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

bool isSeparator(wchar_t c) {
    return c == L'-' || c == L'_' || c == L'.' || c == L' ';
}

// Up to 5 digits at `pos`; nullopt when there are none (or more: not a version part).
std::optional<int> readNumber(std::wstring_view s, std::size_t& pos) {
    const std::size_t start = pos;
    int value = 0;
    while (pos < s.size() && isDigit(s[pos])) {
        if (pos - start >= 5) {
            return std::nullopt;
        }
        value = value * 10 + (s[pos] - L'0');
        ++pos;
    }
    return pos == start ? std::nullopt : std::optional<int>(value);
}

std::wstring lower(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

// A string field of a JSON object; empty when missing, null or of another type.
std::string text(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool flag(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

// "[text](url)" → "text", "![alt](url)" → "", "**x**" / "__x__" / "`x`" / "~~x~~" → "x".
std::wstring plainInline(std::wstring_view line) {
    std::wstring out;
    out.reserve(line.size());
    for (std::size_t i = 0; i < line.size();) {
        const bool image = line[i] == L'!' && i + 1 < line.size() && line[i + 1] == L'[';
        if (line[i] == L'[' || image) {
            const std::size_t open = image ? i + 1 : i;
            const std::size_t close = line.find(L']', open + 1);
            if (close != std::wstring_view::npos && close + 1 < line.size() && line[close + 1] == L'(') {
                const std::size_t end = line.find(L')', close + 2);
                if (end != std::wstring_view::npos) {
                    if (!image) {
                        out += line.substr(open + 1, close - open - 1);
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if (line.substr(i, 2) == L"**" || line.substr(i, 2) == L"__" || line.substr(i, 2) == L"~~") {
            i += 2;
            continue;
        }
        if (line[i] == L'`') {
            ++i;
            continue;
        }
        out.push_back(line[i]);
        ++i;
    }
    return out;
}

// A line that is nothing but links and separators ("[Türkçe](#türkçe) · [English](#english)").
bool linksOnly(std::wstring_view line) {
    bool sawLink = false;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == L'[' || (line[i] == L'!' && i + 1 < line.size() && line[i + 1] == L'[')) {
            const std::size_t close = line.find(L']', i);
            if (close != std::wstring_view::npos && close + 1 < line.size() && line[close + 1] == L'(') {
                const std::size_t end = line.find(L')', close + 2);
                if (end != std::wstring_view::npos) {
                    sawLink = true;
                    i = end + 1;
                    continue;
                }
            }
            return false;
        }
        const wchar_t c = line[i];
        if (!(isSpace(c) || c == L'·' || c == L'|' || c == L',' || c == L'-' || c == L'/' || c == L'•')) {
            return false;
        }
        ++i;
    }
    return sawLink;
}

// 64 hex digits, lower case; anything else empty.
std::wstring normalizeHex(std::wstring_view hex) {
    std::wstring out;
    for (const wchar_t c : hex) {
        if (c >= L'0' && c <= L'9') {
            out.push_back(c);
        } else if (c >= L'a' && c <= L'f') {
            out.push_back(c);
        } else if (c >= L'A' && c <= L'F') {
            out.push_back(static_cast<wchar_t>(c - L'A' + L'a'));
        } else {
            return {};
        }
    }
    return out.size() == 64 ? out : std::wstring();
}

bool rule(std::wstring_view line) {
    if (line.size() < 3) {
        return false;
    }
    return std::ranges::all_of(line, [](wchar_t c) { return c == L'-' || c == L'*' || c == L'_' || c == L'=' || isSpace(c); });
}

} // namespace

std::optional<AppVersion> parseAppVersion(std::wstring_view input) {
    std::wstring_view s = trim(input);
    if (s.size() > 7 && lower(s.substr(0, 7)) == L"winlove" && isSpace(s[7])) {
        s = trim(s.substr(7));
    }
    if (!s.empty() && (s.front() == L'v' || s.front() == L'V')) {
        s.remove_prefix(1);
    }
    AppVersion v;
    int parts[3] = {};
    int count = 0;
    std::size_t pos = 0;
    for (;;) {
        const auto number = readNumber(s, pos);
        if (!number) {
            return std::nullopt;
        }
        parts[count++] = *number;
        if (pos + 1 < s.size() && s[pos] == L'.' && isDigit(s[pos + 1])) {
            if (count == 3) {
                return std::nullopt; // "2.0.0.1": not a form WinLove uses
            }
            ++pos;
            continue;
        }
        break;
    }
    if (count < 2) {
        return std::nullopt;
    }
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    if (pos == s.size()) {
        return v; // a release
    }
    while (pos < s.size() && isSeparator(s[pos])) {
        ++pos;
    }
    const std::size_t wordStart = pos;
    while (pos < s.size() && isAsciiLetter(s[pos])) {
        ++pos;
    }
    const std::wstring word = lower(s.substr(wordStart, pos - wordStart));
    if (word == L"alpha") {
        v.stage = AppVersion::Stage::Alpha;
    } else if (word == L"beta") {
        v.stage = AppVersion::Stage::Beta;
    } else if (word == L"rc") {
        v.stage = AppVersion::Stage::Rc;
    } else {
        return std::nullopt; // "preview", "dev", "final", a date: cannot compare
    }
    if (pos == s.size()) {
        return v;
    }
    while (pos < s.size() && isSeparator(s[pos])) {
        ++pos;
    }
    const auto number = readNumber(s, pos);
    if (!number || pos != s.size()) {
        return std::nullopt;
    }
    v.stageNumber = *number;
    return v;
}

std::optional<int> compareAppVersions(std::wstring_view a, std::wstring_view b) {
    const auto va = parseAppVersion(a);
    const auto vb = parseAppVersion(b);
    if (!va || !vb) {
        return std::nullopt;
    }
    return *va < *vb ? -1 : *vb < *va ? 1 : 0;
}

std::int64_t parseIsoTime(std::string_view text) {
    // "2026-11-09T12:34:56Z" (GitHub's form); a fraction or an offset is not.
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z') {
        return 0;
    }
    auto number = [&](std::size_t at, std::size_t n) {
        int value = 0;
        for (std::size_t i = at; i < at + n; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return -1;
            }
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const int year = number(0, 4);
    const int month = number(5, 2);
    const int day = number(8, 2);
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60) {
        return 0;
    }
    const std::chrono::year_month_day date{std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)),
                                           std::chrono::day(static_cast<unsigned>(day))};
    if (!date.ok()) {
        return 0;
    }
    const auto days = std::chrono::sys_days(date).time_since_epoch().count();
    return static_cast<std::int64_t>(days) * 86400 + hour * 3600 + minute * 60 + second;
}

std::wstring releaseNotesExcerpt(std::string_view markdown, std::size_t maxLines, std::size_t maxChars) {
    const std::wstring body = utf8::toWide(markdown);
    std::wstring out;
    std::size_t lines = 0;
    bool fence = false;
    std::size_t start = 0;
    while (start <= body.size() && lines < maxLines && out.size() < maxChars) {
        std::size_t end = body.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = body.size();
        }
        std::wstring_view line = trim(std::wstring_view(body).substr(start, end - start));
        start = end + 1;
        if (line.starts_with(L"```") || line.starts_with(L"~~~")) {
            fence = !fence;
            continue;
        }
        if (fence || line.empty() || line.starts_with(L"#") || line.starts_with(L"<") || line.starts_with(L"|") || rule(line) ||
            linksOnly(line)) {
            continue; // headings, HTML, tables, rules, a row of anchors
        }
        while (line.starts_with(L">")) {
            line = trim(line.substr(1));
        }
        std::wstring prefix;
        if (line.size() > 2 && (line[0] == L'-' || line[0] == L'*' || line[0] == L'+') && line[1] == L' ') {
            prefix = L"• ";
            line = trim(line.substr(2));
        }
        std::wstring plain(trim(plainInline(line)));
        if (plain.empty()) {
            continue;
        }
        plain = prefix + plain;
        const std::size_t room = maxChars > out.size() ? maxChars - out.size() : 0;
        if (plain.size() > room) {
            plain = plain.substr(0, room > 1 ? room - 1 : 0) + L"…";
        }
        if (!out.empty()) {
            out += L'\n';
        }
        out += plain;
        ++lines;
        if (start > body.size()) {
            break;
        }
    }
    return out;
}

std::wstring releasePageUrl(std::wstring_view htmlUrl) {
    constexpr std::wstring_view prefix = L"https://github.com/shadesofdeath/WinLove/releases/";
    const bool plain = std::ranges::none_of(htmlUrl, [](wchar_t c) { return c <= L' ' || c == L'"' || c == L'<' || c == L'>' || c == L'\\'; });
    if (plain && htmlUrl.size() > prefix.size() && htmlUrl.substr(0, prefix.size()) == prefix &&
        htmlUrl.find(L"..") == std::wstring_view::npos) {
        return std::wstring(htmlUrl);
    }
    return kReleasesPage;
}

Result<std::vector<Release>> parseReleases(std::string_view answer) {
    const json doc = json::parse(answer, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_array()) {
        return fail(ErrorCode::ParseError, L"not a list of releases", L"api.github.com");
    }
    std::vector<Release> out;
    for (const auto& item : doc) {
        if (!item.is_object() || flag(item, "draft")) {
            continue; // a draft is not out; anything else is not a release
        }
        Release r;
        r.tag = utf8::toWide(text(item, "tag_name"));
        r.name = std::wstring(trim(utf8::toWide(text(item, "name"))));
        if (r.tag.empty() && r.name.empty()) {
            continue;
        }
        r.url = releasePageUrl(utf8::toWide(text(item, "html_url")));
        const std::string body = text(item, "body");
        r.notes = releaseNotesExcerpt(body);
        r.body = utf8::toWide(body);
        if (r.body.size() > kMaxReleaseBody) {
            r.body.resize(kMaxReleaseBody);
        }
        // D-145: the attached files (the asset an update installs is picked from these).
        if (const auto assets = item.find("assets"); assets != item.end() && assets->is_array()) {
            for (const auto& a : *assets) {
                if (!a.is_object()) {
                    continue;
                }
                ReleaseAsset asset;
                asset.name = utf8::toWide(text(a, "name"));
                asset.url = utf8::toWide(text(a, "browser_download_url"));
                asset.contentType = utf8::toWide(text(a, "content_type"));
                if (const auto size = a.find("size"); size != a.end() && size->is_number_unsigned()) {
                    asset.size = size->get<std::uint64_t>();
                }
                const std::string digest = text(a, "digest");
                if (digest.rfind("sha256:", 0) == 0) {
                    asset.sha256 = normalizeHex(utf8::toWide(digest.substr(7)));
                }
                const std::string state = text(a, "state");
                asset.uploaded = state.empty() || state == "uploaded";
                if (!asset.name.empty()) {
                    r.assets.push_back(std::move(asset));
                }
            }
        }
        r.published = parseIsoTime(text(item, "published_at"));
        if (r.published == 0) {
            r.published = parseIsoTime(text(item, "created_at"));
        }
        r.prerelease = flag(item, "prerelease");
        r.version = parseAppVersion(r.tag);
        if (!r.version) {
            r.version = parseAppVersion(r.name);
        }
        out.push_back(std::move(r));
    }
    return out;
}

const Release* newestRelease(const std::vector<Release>& releases) {
    const Release* best = nullptr;
    for (const auto& r : releases) {
        if (!r.version) {
            continue;
        }
        if (!best || *best->version < *r.version || (*best->version == *r.version && r.published > best->published)) {
            best = &r;
        }
    }
    return best;
}

ReleaseCheck evaluateReleases(std::wstring_view current, std::vector<Release> releases) {
    ReleaseCheck check;
    check.current = std::wstring(current);
    check.releasesListed = static_cast<int>(releases.size());
    // D-145: the list for the changelog window, newest version first (a tie: the later one); the
    // unreadable ones after them, as GitHub listed them.
    check.releases = releases;
    std::ranges::stable_sort(check.releases, [](const Release& a, const Release& b) {
        if (a.version.has_value() != b.version.has_value()) {
            return a.version.has_value();
        }
        if (!a.version) {
            return false;
        }
        if (*a.version != *b.version) {
            return *b.version < *a.version;
        }
        return a.published > b.published;
    });
    if (releases.empty()) {
        check.verdict = ReleaseVerdict::None;
        return check;
    }
    const Release* newest = newestRelease(releases);
    const auto running = parseAppVersion(current);
    if (!newest) {
        check.latest = releases.front(); // GitHub lists the newest first: named, never compared
        check.verdict = ReleaseVerdict::Unknown;
        return check;
    }
    check.latest = *newest;
    if (!running) {
        check.verdict = ReleaseVerdict::Unknown;
        return check;
    }
    check.verdict = *running < *newest->version ? ReleaseVerdict::Available : ReleaseVerdict::Current;
    return check;
}

ChangelogParts changelogParts(const ReleaseCheck& check, std::size_t older) {
    ChangelogParts parts;
    const auto running = parseAppVersion(check.current);
    for (std::size_t i = 0; i < check.releases.size(); ++i) {
        const auto& r = check.releases[i];
        if (!r.version) {
            continue;
        }
        if (running && *running < *r.version) {
            parts.newer.push_back(i);
        } else if (parts.older.size() < older) {
            parts.older.push_back(i);
        }
    }
    return parts;
}

ReleaseFailure releaseFailure(const Error& error) noexcept {
    switch (error.code) {
    case ErrorCode::Cancelled: return ReleaseFailure::Cancelled;
    case ErrorCode::AccessDenied: return ReleaseFailure::RateLimit;
    case ErrorCode::Unsupported: return ReleaseFailure::Http;
    case ErrorCode::ParseError: return ReleaseFailure::BadAnswer;
    default: return ReleaseFailure::Network;
    }
}

Result<ReleaseCheckAnswer> checkLatestRelease(std::wstring_view current, const ReleaseCheckOptions& options,
                                              const CancelToken& cancel) {
    HttpOptions http;
    http.userAgent = options.userAgent.empty() ? std::wstring(L"WinLove") : options.userAgent;
    // GitHub's documented headers for its REST API; nothing about the machine or the user.
    http.headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    http.timeoutMs = options.timeoutMs;
    http.keepRequestHeaders = options.keepRequestHeaders;
    http.maxBytes = 4u << 20;
    auto response = httpGet(kReleasesApi, http, cancel);
    if (!response) {
        Error e = response.error();
        if (e.code != ErrorCode::Cancelled) {
            e.code = ErrorCode::IoError; // whatever WinHTTP said: no answer from GitHub (ReleaseFailure::Network)
        }
        log::info("update", L"release check: no answer: " + describe(e));
        return std::unexpected(std::move(e));
    }
    ReleaseCheckAnswer answer;
    answer.raw = std::move(response->body);
    answer.sentHeaders = std::move(response->sentHeaders);
    answer.rateLimitRemaining = std::move(response->rateLimitRemaining);
    const int status = response->status;
    if (status == 429 || (status == 403 && (answer.rateLimitRemaining == L"0" || answer.raw.find("rate limit") != std::string::npos))) {
        log::info("update", std::format(L"release check: GitHub's rate limit (HTTP {})", status));
        return fail(ErrorCode::AccessDenied, std::format(L"GitHub's hourly limit for unauthenticated requests is used up (HTTP {})", status),
                    kReleasesApi);
    }
    if (status != 200) {
        log::info("update", std::format(L"release check: HTTP {}", status));
        return fail(ErrorCode::Unsupported, std::format(L"GitHub answered HTTP {}", status), kReleasesApi);
    }
    auto releases = parseReleases(answer.raw);
    if (!releases) {
        log::info("update", L"release check: the answer is not a release list");
        return std::unexpected(releases.error());
    }
    answer.check = evaluateReleases(current, std::move(*releases));
    const auto& c = answer.check;
    static constexpr const wchar_t* kVerdicts[] = {L"current", L"available", L"unknown", L"none"};
    log::info("update", std::format(L"release check: {} release(s), newest {}, running {} -> {}", c.releasesListed,
                                    c.latest ? c.latest->tag : std::wstring(L"-"), c.current,
                                    kVerdicts[static_cast<std::size_t>(c.verdict)]));
    return answer;
}

} // namespace wl::core
