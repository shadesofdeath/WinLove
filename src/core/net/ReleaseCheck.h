#pragma once
// D-140: is there a newer WinLove on GitHub? Notify only — nothing is downloaded or installed.
// One GET of https://api.github.com/repos/shadesofdeath/WinLove/releases with a generic User-Agent
// ("WinLove/<version>"), the GitHub API's Accept header and nothing else: no cookies, no account,
// no machine or user data. Short timeouts, cancellable; every failure is a quiet Result error
// (offline is a normal state). The answer is read with nlohmann/json; drafts are skipped,
// pre-releases count (every WinLove release so far is one).
//
// Versions are compared by a pure function that knows the project's real tag forms:
//   "v1.3.0-beta", "1.3.0 Beta", "WinLove 1.3.0 Beta", "v1.0.0-alpha", "1.4.0", "v1.4.0-rc.2"
// alpha < beta < rc < release; a stage may carry a number ("beta.2", "Beta 2"). Anything else is
// "cannot compare" — never a false "update available".
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct AppVersion {
    enum class Stage : std::uint8_t { Alpha, Beta, Rc, Release };
    int major = 0;
    int minor = 0;
    int patch = 0;
    Stage stage = Stage::Release;
    int stageNumber = 0; // "beta.2" → 2; none → 0 (so "beta" < "beta.1")
    // Member order is the ordering: 1.4.0-alpha > 1.3.9; 1.4.0-beta < 1.4.0-rc < 1.4.0.
    auto operator<=>(const AppVersion&) const = default;
};

// nullopt for any form not listed above (a date, "latest", "1", "2.0.0.1", "v1.3.0-preview"); "1.3"
// reads as 1.3.0.
[[nodiscard]] std::optional<AppVersion> parseAppVersion(std::wstring_view text);
// -1 / 0 / +1 (a against b); nullopt when either side cannot be read.
[[nodiscard]] std::optional<int> compareAppVersions(std::wstring_view a, std::wstring_view b);

// D-145: a file attached to a release, as GitHub's answer lists it (the same single GET: nothing more
// is asked for it).
struct ReleaseAsset {
    std::wstring name;        // "WinLove.exe"
    std::uint64_t size = 0;   // bytes
    std::wstring url;         // browser_download_url
    std::wstring contentType; // "application/x-msdownload"
    std::wstring sha256;      // from "digest": "sha256:<hex>" (lower-case hex), else empty
    bool uploaded = true;     // "state": "uploaded" (a half-uploaded file is "starter")
};

struct Release {
    std::wstring tag;     // "v1.3.0-beta"
    std::wstring name;    // "WinLove 1.3.0 Beta" (may be empty)
    std::wstring url;     // the release page (always on github.com/shadesofdeath/WinLove, see releasePageUrl)
    std::wstring notes;   // the first lines of the body as plain text (no Markdown), at most a few hundred characters
    std::int64_t published = 0; // seconds since 1970 (UTC); 0: unknown
    bool prerelease = false;
    std::optional<AppVersion> version; // from the tag, else from the name
    // D-145: the whole body (Markdown, at most kMaxReleaseBody characters) for the changelog window,
    // and the files attached to the release.
    std::wstring body;
    std::vector<ReleaseAsset> assets;
    // What a person reads: the name, or the tag when there is none.
    [[nodiscard]] const std::wstring& title() const noexcept { return name.empty() ? tag : name; }
};

// GitHub's /releases answer (a JSON array) → its releases, drafts left out, in the answer's order.
// ParseError for anything that is not such an array.
[[nodiscard]] Result<std::vector<Release>> parseReleases(std::string_view json);
// The newest release by version (a tie: the later one); nullptr when no release has a version
// that can be read.
[[nodiscard]] const Release* newestRelease(const std::vector<Release>& releases);
// "2026-11-09T12:34:56Z" → seconds since 1970; 0 for anything else.
[[nodiscard]] std::int64_t parseIsoTime(std::string_view text);
// Markdown → a few plain lines: headings, rules, link-only lines and images out; links keep their
// text; emphasis and code marks go; list items start with "• ".
[[nodiscard]] std::wstring releaseNotesExcerpt(std::string_view markdown, std::size_t maxLines = 4, std::size_t maxChars = 600);

inline constexpr wchar_t kReleasesApi[] = L"https://api.github.com/repos/shadesofdeath/WinLove/releases?per_page=20";
inline constexpr wchar_t kReleasesPage[] = L"https://github.com/shadesofdeath/WinLove/releases";
// The page to open for a release: its html_url when it is on the project's own releases, else the
// project's release list (an answer must never send the browser somewhere else).
[[nodiscard]] std::wstring releasePageUrl(std::wstring_view htmlUrl);

enum class ReleaseVerdict : std::uint8_t {
    Current,   // the running version is the newest (or newer: a development build)
    Available, // a newer release is out
    Unknown,   // a version could not be read: no claim either way
    None,      // GitHub lists no release at all
};

struct ReleaseCheck {
    std::wstring current;          // the running version as the app shows it ("1.3.0 Beta")
    std::optional<Release> latest; // the newest readable release; Unknown: the newest listed one, if any
    ReleaseVerdict verdict = ReleaseVerdict::Unknown;
    int releasesListed = 0;        // non-draft releases in the answer
    // D-145: every listed release, newest version first (those without a readable version last, in
    // the answer's order) — the changelog window's list. Same answer: no request more.
    std::vector<Release> releases;
};

inline constexpr std::size_t kMaxReleaseBody = 128 * 1024; // GitHub caps a body at 125 000 characters

// D-145: the changelog window's two parts of `check.releases` (indexes into it): every release newer
// than the running version, newest first, then at most `older` of the rest (the running one among
// them). Unknown versions are neither: they are left out.
struct ChangelogParts {
    std::vector<std::size_t> newer;
    std::vector<std::size_t> older;
};
[[nodiscard]] ChangelogParts changelogParts(const ReleaseCheck& check, std::size_t older = 3);

// The verdict for `current` against the parsed answer (pure: the unit tests' way in).
[[nodiscard]] ReleaseCheck evaluateReleases(std::wstring_view current, std::vector<Release> releases);

// Why a check failed, for the words the app and wlcli show. The check's errors are made so:
//   Cancelled  → the token was cancelled
//   Network    → IoError: no connection, DNS, timeout, TLS (offline is a normal state)
//   RateLimit  → AccessDenied: GitHub's hourly limit for unauthenticated calls (403 / 429)
//   Http       → Unsupported: another status (404, 5xx); the message names it
//   BadAnswer  → ParseError: not the JSON GitHub sends
enum class ReleaseFailure : std::uint8_t { Cancelled, Network, RateLimit, Http, BadAnswer };
[[nodiscard]] ReleaseFailure releaseFailure(const Error& error) noexcept;

struct ReleaseCheckOptions {
    std::wstring userAgent;      // "WinLove/1.3.0"
    unsigned timeoutMs = 6000;   // per phase (resolve, connect, send, receive)
    bool keepRequestHeaders = false; // wlcli --verbose: say exactly what was sent
};
struct ReleaseCheckAnswer {
    ReleaseCheck check;
    std::string raw;          // the answer as received (wlcli --save: a recorded sample for the tests)
    std::wstring sentHeaders; // keepRequestHeaders
    std::wstring rateLimitRemaining;
};
[[nodiscard]] Result<ReleaseCheckAnswer> checkLatestRelease(std::wstring_view current, const ReleaseCheckOptions& options,
                                                            const CancelToken& cancel = {});

} // namespace wl::core
