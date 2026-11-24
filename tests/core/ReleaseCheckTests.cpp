// D-140: the update check's pure parts — version comparison over the project's real tag forms (and
// odd ones), GitHub's release list (a recorded answer, drafts, an empty list, malformed answers),
// the notes excerpt, the safe release URL, the failure words. No network: the recorded sample is
// GitHub's real answer of 2026-11-11 (wlcli update-check --save), cut to three releases.
#include "core/net/ReleaseCheck.h"
#include "core/ops/Applier.h"

#include <doctest.h>

#include <string>
#include <string_view>

using namespace wl;
using namespace wl::core;

namespace {

// api.github.com/repos/shadesofdeath/WinLove/releases, 2026-11-11: the newest, one in the middle and
// the first; the first body cut after its third paragraph, author / assets cut to a few fields.
constexpr std::string_view kRecorded = R"json([
 {
  "url": "https://api.github.com/repos/shadesofdeath/WinLove/releases/438873318",
  "html_url": "https://github.com/shadesofdeath/WinLove/releases/tag/v1.3.0-beta",
  "id": 438873318,
  "author": {"login": "shadesofdeath", "id": 61064047, "type": "User"},
  "node_id": "RE_kwDOU9JfHM4aKKzm",
  "tag_name": "v1.3.0-beta",
  "target_commitish": "main",
  "name": "WinLove 1.3.0 Beta",
  "draft": false,
  "immutable": false,
  "prerelease": true,
  "created_at": "2026-11-09T11:37:01Z",
  "updated_at": "2026-11-09T11:37:28Z",
  "published_at": "2026-11-09T08:15:28Z",
  "assets": [{"name": "WinLove.exe", "size": 11211264,
              "browser_download_url": "https://github.com/shadesofdeath/WinLove/releases/download/v1.3.0-beta/WinLove.exe"}],
  "tarball_url": "https://api.github.com/repos/shadesofdeath/WinLove/tarball/v1.3.0-beta",
  "zipball_url": "https://api.github.com/repos/shadesofdeath/WinLove/zipball/v1.3.0-beta",
  "body": "**WinLove 1.3.0 Beta** \u2014 Yeni aray\u00fcz, yeni kurulum ekranlar\u0131 / New interface, new setup screens.\n\n[T\u00fcrk\u00e7e](#t\u00fcrk\u00e7e) \u00b7 [English](#english)\n\n> \u26a0\ufe0f **Beta:** De\u011fi\u015fiklikleri \u00f6nce bir sanal makinede dene.\n> Try changes in a virtual machine first.\n\n## T\u00fcrk\u00e7e\n\n### Yeni\n- **Yeni aray\u00fcz:** yeni gezinme d\u00fczeni."
 },
 {
  "html_url": "https://github.com/shadesofdeath/WinLove/releases/tag/v1.2.3-beta",
  "tag_name": "v1.2.3-beta",
  "name": "WinLove 1.2.3 Beta",
  "draft": false,
  "prerelease": true,
  "created_at": "2026-10-10T23:59:00Z",
  "published_at": "2026-10-11T00:03:03Z",
  "body": "Fixes."
 },
 {
  "html_url": "https://github.com/shadesofdeath/WinLove/releases/tag/v1.0.0-alpha",
  "tag_name": "v1.0.0-alpha",
  "name": "WinLove 1.0 Alpha",
  "draft": false,
  "prerelease": true,
  "published_at": "2026-10-05T19:37:22Z",
  "body": null
 }
])json";

AppVersion v(int major, int minor, int patch, AppVersion::Stage stage = AppVersion::Stage::Release, int n = 0) {
    return AppVersion{major, minor, patch, stage, n};
}

} // namespace

TEST_CASE("D-140: the project's real tag forms are read, odd ones are not") {
    using S = AppVersion::Stage;
    struct Case {
        const wchar_t* text;
        std::optional<AppVersion> expected;
    };
    const Case cases[] = {
        // Every tag and release name the project has published, and the app's own label.
        {L"v1.3.0-beta", v(1, 3, 0, S::Beta)},
        {L"1.3.0 Beta", v(1, 3, 0, S::Beta)},          // WL_VERSION_LABEL
        {L"WinLove 1.3.0 Beta", v(1, 3, 0, S::Beta)},  // the release's name
        {L"v1.2.3-beta", v(1, 2, 3, S::Beta)},
        {L"v1.0.0-alpha", v(1, 0, 0, S::Alpha)},
        {L"WinLove 1.0 Alpha", v(1, 0, 0, S::Alpha)},  // the first release's name: two parts
        {L"v1.0.2-alpha", v(1, 0, 2, S::Alpha)},
        // Forms a later release may take.
        {L"1.4.0", v(1, 4, 0)},
        {L"v1.4.0", v(1, 4, 0)},
        {L"V2.0.0", v(2, 0, 0)},
        {L"v1.4.0-rc.2", v(1, 4, 0, S::Rc, 2)},
        {L"1.4.0 RC 2", v(1, 4, 0, S::Rc, 2)},
        {L"1.4.0-rc2", v(1, 4, 0, S::Rc, 2)},
        {L"v1.3.0-beta.1", v(1, 3, 0, S::Beta, 1)},
        {L"1.3.0_BETA", v(1, 3, 0, S::Beta)},
        {L"  v1.3.0-beta \n", v(1, 3, 0, S::Beta)},
        {L"1.10.0", v(1, 10, 0)},
        // Not WinLove's forms: cannot compare, never a guess.
        {L"", std::nullopt},
        {L"latest", std::nullopt},
        {L"v", std::nullopt},
        {L"1", std::nullopt},
        {L"2.0.0.1", std::nullopt},
        {L"v1.3.0-preview", std::nullopt},
        {L"v1.3.0-dev", std::nullopt},
        {L"1.3.0-beta.", std::nullopt},
        {L"1.3.0-beta.x", std::nullopt},
        {L"1.3.0 Beta extra", std::nullopt},
        {L"v1.3.0+build5", std::nullopt},
        {L"2026-11-09", std::nullopt},
        {L"nightly-20261109", std::nullopt},
        {L"WinLove", std::nullopt},
        {L"WinLove1.3.0", std::nullopt},
        {L"Version 1.0.1 Alpha", std::nullopt},
        {L"1.123456.0", std::nullopt},
        {L"-1.3.0", std::nullopt},
    };
    for (const auto& c : cases) {
        CAPTURE(std::wstring(c.text));
        const auto parsed = parseAppVersion(c.text);
        REQUIRE(parsed.has_value() == c.expected.has_value());
        if (parsed) {
            CHECK(*parsed == *c.expected);
        }
    }
}

TEST_CASE("D-140: alpha < beta < rc < release, numbers compare as numbers, unknown is no answer") {
    struct Order {
        const wchar_t* lower;
        const wchar_t* higher;
    };
    const Order orders[] = {
        {L"v1.0.0-alpha", L"v1.0.1-alpha"},  {L"v1.0.2-alpha", L"v1.1.0-beta"},  {L"v1.2.3-beta", L"v1.3.0-beta"},
        {L"1.3.0 Beta", L"1.3.0-beta.1"},     {L"1.3.0-beta.1", L"1.3.0-beta.2"}, {L"1.3.0-beta.2", L"1.3.0-rc"},
        {L"1.3.0-rc", L"1.3.0-rc.1"},         {L"1.3.0-rc.9", L"1.3.0"},           {L"1.3.0", L"1.3.1-alpha"},
        {L"1.9.0", L"1.10.0"},                {L"1.3.0-alpha", L"1.3.0-beta"},    {L"v1.3.0-beta", L"1.4.0"},
        {L"0.9.9", L"1.0.0-alpha"},
    };
    for (const auto& o : orders) {
        CAPTURE(std::wstring(o.lower));
        CAPTURE(std::wstring(o.higher));
        CHECK(compareAppVersions(o.lower, o.higher) == -1);
        CHECK(compareAppVersions(o.higher, o.lower) == 1);
    }
    CHECK(compareAppVersions(L"v1.3.0-beta", L"1.3.0 Beta") == 0); // the tag and the app's label are one version
    CHECK(compareAppVersions(L"WinLove 1.3.0 Beta", L"v1.3.0-beta") == 0);
    CHECK(compareAppVersions(L"1.3", L"1.3.0") == 0);
    CHECK_FALSE(compareAppVersions(L"v1.3.0-preview", L"1.3.0 Beta").has_value());
    CHECK_FALSE(compareAppVersions(L"1.3.0 Beta", L"latest").has_value());
    CHECK_FALSE(compareAppVersions(L"", L"").has_value());
}

TEST_CASE("D-140: GitHub's recorded answer — releases, dates, notes, page, verdicts") {
    const auto releases = parseReleases(kRecorded);
    REQUIRE(releases);
    REQUIRE(releases->size() == 3);
    const Release& newest = releases->front();
    CHECK(newest.tag == L"v1.3.0-beta");
    CHECK(newest.name == L"WinLove 1.3.0 Beta");
    CHECK(newest.title() == L"WinLove 1.3.0 Beta");
    CHECK(newest.url == L"https://github.com/shadesofdeath/WinLove/releases/tag/v1.3.0-beta");
    CHECK(newest.prerelease);
    CHECK(newest.published == 1794212128); // 2026-11-09T08:15:28Z
    REQUIRE(newest.version);
    CHECK(*newest.version == v(1, 3, 0, AppVersion::Stage::Beta));
    // Plain text: no emphasis marks, the row of anchors and the headings left out, the quote kept.
    CHECK(newest.notes.starts_with(L"WinLove 1.3.0 Beta \u2014 Yeni aray\u00fcz"));
    CHECK(newest.notes.find(L"**") == std::wstring::npos);
    CHECK(newest.notes.find(L"[T\u00fcrk\u00e7e]") == std::wstring::npos);
    CHECK(newest.notes.find(L"T\u00fcrk\u00e7e \u00b7 English") == std::wstring::npos);
    CHECK(newest.notes.find(L"##") == std::wstring::npos);
    CHECK(newest.notes.find(L"Beta: De\u011fi\u015fiklikleri") != std::wstring::npos);
    CHECK(newest.notes.find(L'\n') != std::wstring::npos); // more than one line
    // A null body and a release published without a name of the usual form.
    const Release& first = (*releases)[2];
    CHECK(first.notes.empty());
    REQUIRE(first.version);
    CHECK(first.version->stage == AppVersion::Stage::Alpha);
    // The middle one: published_at wins over created_at.
    CHECK((*releases)[1].published == 1791676983); // 2026-10-11T00:03:03Z

    const auto same = evaluateReleases(L"1.3.0 Beta", *releases);
    CHECK(same.verdict == ReleaseVerdict::Current);
    REQUIRE(same.latest);
    CHECK(same.latest->tag == L"v1.3.0-beta");
    CHECK(same.releasesListed == 3);
    CHECK(same.current == L"1.3.0 Beta");
    CHECK(evaluateReleases(L"1.2.3 Beta", *releases).verdict == ReleaseVerdict::Available);
    CHECK(evaluateReleases(L"v1.0.0-alpha", *releases).verdict == ReleaseVerdict::Available);
    CHECK(evaluateReleases(L"1.3.0-rc", *releases).verdict == ReleaseVerdict::Current); // a build ahead of the list
    CHECK(evaluateReleases(L"1.4.0", *releases).verdict == ReleaseVerdict::Current);
    const auto unreadable = evaluateReleases(L"dev build", *releases);
    CHECK(unreadable.verdict == ReleaseVerdict::Unknown); // never "available" on a guess
    REQUIRE(unreadable.latest);
    CHECK(unreadable.latest->tag == L"v1.3.0-beta");
}

TEST_CASE("D-140: drafts are skipped, the newest is by version, unreadable tags make no claim") {
    const auto releases = parseReleases(R"([
        {"tag_name": "v1.2.0-beta", "name": "WinLove 1.2.0 Beta", "draft": false, "prerelease": true,
         "published_at": "2026-10-09T23:08:40Z", "html_url": "https://github.com/shadesofdeath/WinLove/releases/tag/v1.2.0-beta"},
        {"tag_name": "v9.9.9", "name": "Draft", "draft": true, "published_at": null},
        {"tag_name": "v1.3.0-beta", "name": "WinLove 1.3.0 Beta", "draft": false, "prerelease": true,
         "published_at": "2026-11-09T08:15:28Z", "html_url": "https://evil.example/download"},
        {"tag_name": "nightly", "name": "Nightly build", "draft": false, "published_at": "2026-11-10T00:00:00Z"},
        {"tag_name": "release-1.3.1", "name": "WinLove 1.3.1", "draft": false, "published_at": "2026-11-10T01:00:00Z"}
    ])");
    REQUIRE(releases);
    CHECK(releases->size() == 4); // the draft is not out
    const Release* newest = newestRelease(*releases);
    REQUIRE(newest);
    CHECK(newest->tag == L"release-1.3.1"); // the tag is not readable, the name is
    CHECK(newest->url == kReleasesPage);     // no html_url: the project's list
    CHECK((*releases)[1].url == kReleasesPage); // an address elsewhere is never opened
    CHECK(evaluateReleases(L"1.3.0 Beta", *releases).verdict == ReleaseVerdict::Available);

    // A tie: the later one.
    const auto tie = parseReleases(R"([{"tag_name": "v1.3.0-beta", "name": "first", "published_at": "2026-11-01T00:00:00Z"},
                                       {"tag_name": "1.3.0 Beta", "name": "again", "published_at": "2026-11-02T00:00:00Z"}])");
    REQUIRE(tie);
    REQUIRE(newestRelease(*tie));
    CHECK(newestRelease(*tie)->name == L"again");

    // Nothing comparable: the first listed is named, no verdict either way.
    const auto odd = parseReleases(R"([{"tag_name": "nightly", "name": "Nightly"}, {"tag_name": "preview-2"}])");
    REQUIRE(odd);
    CHECK(newestRelease(*odd) == nullptr);
    const auto unknown = evaluateReleases(L"1.3.0 Beta", *odd);
    CHECK(unknown.verdict == ReleaseVerdict::Unknown);
    REQUIRE(unknown.latest);
    CHECK(unknown.latest->tag == L"nightly");
}

TEST_CASE("D-140: an empty list and malformed answers") {
    const auto empty = parseReleases("[]");
    REQUIRE(empty);
    CHECK(empty->empty());
    const auto none = evaluateReleases(L"1.3.0 Beta", *empty);
    CHECK(none.verdict == ReleaseVerdict::None);
    CHECK_FALSE(none.latest);

    for (const std::string_view bad : {std::string_view(""), std::string_view("{"), std::string_view("null"),
                                       std::string_view(R"({"message":"Not Found","documentation_url":"https://docs.github.com"})"),
                                       std::string_view("<html>rate limited</html>"), std::string_view("[1,2")}) {
        CAPTURE(std::string(bad));
        const auto parsed = parseReleases(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::ParseError);
        CHECK(releaseFailure(parsed.error()) == ReleaseFailure::BadAnswer);
    }
    // Entries of the wrong shape or type are passed over, never thrown on.
    const auto mixed = parseReleases(R"([1, "x", null, [], {"tag_name": 5, "name": null}, {"draft": "yes", "tag_name": "v1.3.0-beta",
                                         "published_at": 7, "prerelease": "no", "body": 3}])");
    REQUIRE(mixed);
    REQUIRE(mixed->size() == 1);
    CHECK(mixed->front().tag == L"v1.3.0-beta");
    CHECK(mixed->front().published == 0);
    CHECK_FALSE(mixed->front().prerelease);
    CHECK(mixed->front().notes.empty());
}

TEST_CASE("D-140: release notes as a few plain lines") {
    const std::string md = "# WinLove 1.4.0\n\n[T\xc3\xbcrk\xc3\xa7" "e](#t) \xc2\xb7 [English](#e)\n---\n"
                           "**Bold** and `code` and [a link](https://x.y) and ![pic](https://x/p.png) end.\n"
                           "```\nnot this\n```\n<!-- hidden -->\n| a | b |\n"
                           "- first item\n* second item\n> quoted line\n- third\n";
    const std::wstring notes = releaseNotesExcerpt(md);
    CHECK(notes == L"Bold and code and a link and  end.\n\u2022 first item\n\u2022 second item\nquoted line");
    CHECK(releaseNotesExcerpt(md, 1) == L"Bold and code and a link and  end.");
    const std::wstring cut = releaseNotesExcerpt(std::string(1000, 'a'), 4, 50);
    CHECK(cut.size() == 50);
    CHECK(cut.back() == L'\u2026');
    CHECK(releaseNotesExcerpt("").empty());
    CHECK(releaseNotesExcerpt("## Only a heading\n\n---\n").empty());
}

TEST_CASE("D-140: GitHub's dates, the safe page address, the failure words") {
    CHECK(parseIsoTime("1970-01-01T00:00:00Z") == 0);
    CHECK(parseIsoTime("2026-11-09T08:15:28Z") == 1794212128);
    CHECK(parseIsoTime("2026-11-09T08:15:28.123Z") == 0);
    CHECK(parseIsoTime("2026-11-09T08:15:28+03:00") == 0);
    CHECK(parseIsoTime("2026-02-30T00:00:00Z") == 0);
    CHECK(parseIsoTime("2026-13-01T00:00:00Z") == 0);
    CHECK(parseIsoTime("") == 0);

    CHECK(releasePageUrl(L"https://github.com/shadesofdeath/WinLove/releases/tag/v1.3.0-beta") ==
          L"https://github.com/shadesofdeath/WinLove/releases/tag/v1.3.0-beta");
    for (const std::wstring_view bad : {std::wstring_view(L""), std::wstring_view(L"https://evil.example/x"),
                                        std::wstring_view(L"http://github.com/shadesofdeath/WinLove/releases/tag/v1"),
                                        std::wstring_view(L"https://github.com/shadesofdeath/WinLove/releases/"),
                                        std::wstring_view(L"https://github.com/shadesofdeath/WinLove/releases/../../other"),
                                        std::wstring_view(L"https://github.com/shadesofdeath/WinLove/releases/tag/v1\" --x"),
                                        std::wstring_view(L"https://github.com/shadesofdeath/WinLoveX/releases/tag/v1"),
                                        std::wstring_view(L"https://github.com.evil.example/shadesofdeath/WinLove/releases/tag/v1")}) {
        CAPTURE(std::wstring(bad));
        CHECK(releasePageUrl(bad) == kReleasesPage);
    }

    CHECK(releaseFailure(Error{ErrorCode::Cancelled}) == ReleaseFailure::Cancelled);
    CHECK(releaseFailure(Error{ErrorCode::IoError}) == ReleaseFailure::Network);
    CHECK(releaseFailure(Error{ErrorCode::Unknown}) == ReleaseFailure::Network);
    CHECK(releaseFailure(Error{ErrorCode::AccessDenied}) == ReleaseFailure::RateLimit);
    CHECK(releaseFailure(Error{ErrorCode::Unsupported}) == ReleaseFailure::Http);
    CHECK(releaseFailure(Error{ErrorCode::ParseError}) == ReleaseFailure::BadAnswer);
}

TEST_CASE("D-131 / 2b: why a run stopped — the session lost, not a cancel") {
    using namespace wl::core::ops;
    ApplyReport done;
    done.completed = true;
    CHECK(runStop(done) == RunStop::None);
    ApplyReport cancelled; // completed false, no session error: a cancel / a stop at a failed step
    CHECK(runStop(cancelled) == RunStop::Stopped);
    ApplyReport lost;
    lost.sessionLost = Error{ErrorCode::DismFailure, L"DismOpenSession failed", L"C:\\mount", static_cast<std::int32_t>(0x80004005)};
    CHECK(runStop(lost) == RunStop::SessionLost);
    lost.completed = true; // never both in practice; the lost session still says why
    CHECK(runStop(lost) == RunStop::SessionLost);
}
