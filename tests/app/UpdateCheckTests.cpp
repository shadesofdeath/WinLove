// D-140: the update check in the app — the once-a-day rule and the never-twice notice with an injected
// clock, the settings it keeps, the controller with a fetch of the test's own (no network: the real
// one is only made by App in a windowed run), the About card's states, the Settings toggle, and the
// notice (one toast, the status bar segment until the About page is seen).
#include "app/Localization.h"
#include "app/controllers/UpdateController.h"
#include "app/pages/AboutPage.h"
#include "app/pages/SettingsPage.h"
#include "app/shell/ChromeText.h"
#include "app/shell/Shell.h"
#include "app/state/AppState.h"
#include "support/TestGraphics.h"
#include "support/UiThread.h"
#include "ui/widget/Host.h"

#include <doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

using namespace wl;
using namespace wl::app;
using ui::RectF;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"update-check";
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

const Localization& turkish() {
    static const Localization strings =
        Localization::fromJson(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json")).value();
    return strings;
}

constexpr std::int64_t kNow = 1794300000; // 2026-11-10, a fixed instant: nothing here reads the clock
constexpr std::int64_t kHour = 3600;

core::Release release(const wchar_t* tag, const wchar_t* name, std::int64_t published = kNow - 2 * 86400) {
    core::Release r;
    r.tag = tag;
    r.name = name;
    r.published = published;
    r.url = core::releasePageUrl(std::wstring(L"https://github.com/shadesofdeath/WinLove/releases/tag/") + tag);
    r.notes = L"Yeni sürüm notları.\n• bir madde";
    r.version = core::parseAppVersion(tag);
    r.prerelease = true;
    return r;
}

core::ReleaseCheck checkOf(std::vector<core::Release> releases, const wchar_t* current = L"1.3.0 Beta") {
    return core::evaluateReleases(current, std::move(releases));
}

// The posted work of the controller, run when the test says (the UI thread's turn).
struct Posted {
    std::vector<std::function<void()>> items;
    std::function<void(std::function<void()>)> post() {
        return [this](std::function<void()> fn) { items.push_back(std::move(fn)); };
    }
    void run() {
        auto now = std::move(items);
        items.clear();
        for (auto& fn : now) {
            fn();
        }
    }
};

class OneChild : public ui::Widget {
public:
    OneChild(std::unique_ptr<ui::Widget> child, RectF rect) : m_rect(rect) { addChild(std::move(child)); }
    void layout() override {
        if (!children().empty()) {
            children()[0]->setBounds(m_rect);
        }
    }

private:
    RectF m_rect;
};

struct Hosted {
    std::unique_ptr<ui::Host> host;
    ui::Widget* page = nullptr;
    Hosted(std::unique_ptr<ui::Widget> body, RectF rect) {
        page = body.get();
        host = std::make_unique<ui::Host>(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
        host->setRoot(std::make_unique<OneChild>(std::move(body), rect));
        host->layout({rect.right() + 20, rect.bottom() + 20});
    }
};

} // namespace

TEST_CASE("D-140: the startup check is due once a day, only when the setting is on") {
    AppSettings s;
    CHECK(s.checkUpdates); // on by default
    CHECK(updateCheckDue(s, kNow)); // never checked
    s.updateCheckedAt = kNow - 23 * kHour;
    CHECK_FALSE(updateCheckDue(s, kNow));
    s.updateCheckedAt = kNow - 24 * kHour;
    CHECK(updateCheckDue(s, kNow));
    s.updateCheckedAt = kNow - 400 * 24 * kHour;
    CHECK(updateCheckDue(s, kNow));
    s.updateCheckedAt = kNow + 10 * kHour; // the clock was set back: not "never again"
    CHECK(updateCheckDue(s, kNow));
    s.updateCheckedAt = kNow + 30 * 60; // a few minutes of clock drift: still today's
    CHECK_FALSE(updateCheckDue(s, kNow));
    s.updateCheckedAt = 0;
    s.checkUpdates = false;
    CHECK_FALSE(updateCheckDue(s, kNow));
}

TEST_CASE("D-140: the notice is for a newer release that was not announced before") {
    const auto available = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta")});
    REQUIRE(available.verdict == core::ReleaseVerdict::Available);
    CHECK(updateNoticeDue(available, L""));
    CHECK(updateNoticeDue(available, L"v1.3.5-beta"));
    CHECK_FALSE(updateNoticeDue(available, L"v1.4.0-beta")); // announced (dismissed) once: never again
    CHECK_FALSE(updateNoticeDue(checkOf({release(L"v1.3.0-beta", L"WinLove 1.3.0 Beta")}), L""));
    CHECK_FALSE(updateNoticeDue(checkOf({release(L"nightly", L"Nightly")}), L""));   // unknown: no claim
    CHECK_FALSE(updateNoticeDue(checkOf({}), L""));                                   // none
}

TEST_CASE("D-140: the update settings survive save / load; damaged ones fall back") {
    AppSettings settings;
    settings.checkUpdates = false;
    settings.updateCheckedAt = kNow;
    settings.updateLatestTag = L"v1.4.0-beta";
    settings.updateNotifiedTag = L"v1.4.0-beta";
    const auto file = scratch(L"roundtrip.json");
    settings.save(file);
    const auto loaded = AppSettings::load(file);
    CHECK(loaded == settings);
    CHECK_FALSE(loaded.checkUpdates);
    CHECK(loaded.updateCheckedAt == kNow);

    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << R"({"checkUpdates":"no","updateCheckedAt":"yesterday","updateLatestTag":5,"updateNotifiedTag":null})";
    const auto damaged = AppSettings::load(file);
    CHECK(damaged.checkUpdates); // the default
    CHECK(damaged.updateCheckedAt == 0);
    CHECK(damaged.updateLatestTag.empty());
    CHECK(damaged.updateNotifiedTag.empty());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << R"({"updateCheckedAt":-5})";
    CHECK(AppSettings::load(file).updateCheckedAt == 0);
}

TEST_CASE("D-140: the controller — a startup check once a day, one notice per release, failures quiet") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    Posted posted;
    std::int64_t now = kNow;
    std::atomic<int> calls{0};
    Result<core::ReleaseCheck> answer = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta")});
    UpdateController controller(
        state, posted.post(),
        [&](const core::CancelToken&) -> Result<core::ReleaseCheck> {
            ++calls;
            return answer;
        },
        [&] { return now; });
    auto settle = [&] {
        controller.drain();
        posted.run();
    };

    // First start: due; while it runs the state says so, then the answer lands on the "UI thread".
    REQUIRE(controller.checkIfDue());
    CHECK(state.appUpdate().phase == AppState::AppUpdate::Phase::Checking);
    CHECK_FALSE(controller.checkIfDue()); // one at a time
    settle();
    CHECK(calls == 1);
    const auto& u = state.appUpdate();
    CHECK(u.phase == AppState::AppUpdate::Phase::Done);
    REQUIRE(u.result);
    CHECK(u.result->verdict == core::ReleaseVerdict::Available);
    CHECK(u.notice);
    CHECK_FALSE(u.toastShown);
    CHECK_FALSE(u.manual);
    CHECK(state.settings().updateCheckedAt == kNow);
    CHECK(state.settings().updateLatestTag == L"v1.4.0-beta");
    CHECK(state.settings().updateNotifiedTag == L"v1.4.0-beta"); // saved with the notice: never twice
    // The status bar's segment while the notice stands.
    CHECK(statusModel(state, turkish(), Language::Turkish).update == L"Yeni sürüm: WinLove 1.4.0 Beta");

    // The same day: not again.
    now += 5 * kHour;
    CHECK_FALSE(controller.checkIfDue());
    CHECK(calls == 1);
    // The About page seen: the segment goes.
    state.dismissAppUpdateNotice();
    CHECK(statusModel(state, turkish(), Language::Turkish).update.empty());

    // A day later: checked again, the same release: no second notice.
    now += 20 * kHour;
    REQUIRE(controller.checkIfDue());
    settle();
    CHECK(calls == 2);
    CHECK_FALSE(state.appUpdate().notice);
    CHECK(state.settings().updateCheckedAt == now);

    // A newer one the next day: announced.
    now += 25 * kHour;
    answer = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta"), release(L"v1.4.1-beta", L"WinLove 1.4.1 Beta")});
    REQUIRE(controller.checkIfDue());
    settle();
    CHECK(state.appUpdate().notice);
    CHECK(state.settings().updateNotifiedTag == L"v1.4.1-beta");

    // Offline the next day: a quiet failure; the time of the last answer stays (the next start retries).
    const std::int64_t answeredAt = state.settings().updateCheckedAt;
    now += 25 * kHour;
    answer = std::unexpected(Error{ErrorCode::IoError, L"the server did not answer", core::kReleasesApi,
                                   static_cast<std::int32_t>(0x80072EE7)});
    REQUIRE(controller.checkIfDue());
    settle();
    CHECK(state.appUpdate().phase == AppState::AppUpdate::Phase::Failed);
    CHECK(state.settings().updateCheckedAt == answeredAt);
    CHECK(updateCheckDue(state.settings(), now)); // still due: the next start asks again

    // The setting off: no startup check at all — but "Şimdi denetle" still asks, and what it shows is seen.
    AppSettings off = state.settings();
    off.checkUpdates = false;
    off.updateNotifiedTag.clear();
    state.setSettings(off);
    now += 30 * kHour;
    const int before = calls;
    CHECK_FALSE(controller.checkIfDue());
    CHECK(calls == before);
    answer = checkOf({release(L"v1.5.0", L"WinLove 1.5.0")});
    controller.checkNow();
    settle();
    CHECK(calls == before + 1);
    CHECK(state.appUpdate().manual);
    CHECK_FALSE(state.appUpdate().notice); // the About page shows it; no toast
    CHECK(state.settings().updateNotifiedTag == L"v1.5.0");
    CHECK_FALSE(state.settings().checkUpdates); // untouched
}

TEST_CASE("D-140: the About card says each state, and its buttons follow") {
    AppState state{scratch(L"recent-about.json"), scratch(L"settings-about.json")};
    const auto& tr = turkish();
    using Phase = AppState::AppUpdate::Phase;

    // Never checked: what happens at start.
    auto view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.title == tr.get(Str::AppUpdateIdleTitle));
    CHECK(view.detail == tr.get(Str::AppUpdateAutoOn));
    CHECK_FALSE(view.available);
    CHECK_FALSE(view.checking);
    AppSettings settings = state.settings();
    settings.checkUpdates = false;
    settings.updateCheckedAt = kNow - 26 * kHour;
    settings.updateLatestTag = L"v1.3.0-beta";
    state.setSettings(settings);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.title.starts_with(L"Son denetim: "));
    CHECK(view.detail == tr.format(Str::AppUpdateLastKnown, {{L"tag", L"v1.3.0-beta"}}) + L" · " + tr.get(Str::AppUpdateAutoOff));

    AppState::AppUpdate u;
    u.phase = Phase::Checking;
    state.setAppUpdate(u);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.checking);
    CHECK(view.title == tr.get(Str::AppUpdateChecking));

    u.phase = Phase::Done;
    u.at = kNow - 60;
    u.result = checkOf({release(L"v1.3.0-beta", L"WinLove 1.3.0 Beta")});
    state.setAppUpdate(u);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.title == tr.get(Str::AppUpdateCurrentTitle));
    CHECK(view.tone == UpdateCardView::Tone::Success);
    CHECK(view.detail.starts_with(L"En son sürüm v1.3.0-beta · 2 gün önce yayımlandı · son denetim "));
    CHECK_FALSE(view.available);

    u.result = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta")});
    state.setAppUpdate(u);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.title == L"Yeni sürüm var: WinLove 1.4.0 Beta");
    CHECK(view.detail == L"v1.4.0-beta · 2 gün önce yayımlandı · bu bilgisayarda 1.3.0 Beta");
    CHECK(view.available);
    CHECK(view.url == L"https://github.com/shadesofdeath/WinLove/releases/tag/v1.4.0-beta");
    CHECK(view.notes.starts_with(L"Yeni sürüm"));

    u.result = checkOf({release(L"nightly", L"Nightly")});
    state.setAppUpdate(u);
    CHECK(updateCardView(state, tr, Language::Turkish, kNow).title == tr.get(Str::AppUpdateUnknownTitle));
    u.result = checkOf({});
    state.setAppUpdate(u);
    CHECK(updateCardView(state, tr, Language::Turkish, kNow).title == tr.get(Str::AppUpdateNoneTitle));

    u.phase = Phase::Failed;
    u.result.reset();
    const std::pair<Error, Str> failures[] = {
        {Error{ErrorCode::IoError, L"no answer"}, Str::AppUpdateFailedNetwork},
        {Error{ErrorCode::AccessDenied, L"rate"}, Str::AppUpdateFailedRateLimit},
        {Error{ErrorCode::ParseError, L"json"}, Str::AppUpdateFailedAnswer},
    };
    for (const auto& [error, text] : failures) {
        u.error = error;
        state.setAppUpdate(u);
        view = updateCardView(state, tr, Language::Turkish, kNow);
        CHECK(view.title == tr.get(Str::AppUpdateFailedTitle));
        CHECK(view.detail == tr.get(text));
        CHECK(view.tone == UpdateCardView::Tone::Warning);
    }
    u.error = Error{ErrorCode::Unsupported, L"GitHub answered HTTP 503"};
    state.setAppUpdate(u);
    CHECK(updateCardView(state, tr, Language::Turkish, kNow).detail ==
          tr.format(Str::AppUpdateFailedHttp, {{L"detail", L"GitHub answered HTTP 503"}}));

    // The page itself: the buttons follow the state; its "Şimdi denetle" calls only what it is given.
    int asked = 0;
    std::vector<std::wstring> opened;
    auto page = std::make_unique<AboutPage>(state, tr, Language::Turkish,
                                            AboutPage::Intents{[&](const std::wstring& url) { opened.push_back(url); }, [&] { ++asked; }});
    AboutPage* about = page.get();
    Hosted hosted(std::move(page), {0, 0, 900, 1200});
    CHECK(about->checkButton().enabled());
    CHECK_FALSE(about->releaseButton().visible());
    about->checkButton().onInvoke();
    CHECK(asked == 1);
    u.phase = Phase::Checking;
    state.setAppUpdate(u);
    CHECK_FALSE(about->checkButton().enabled());
    const float plain = about->contentHeight();
    u.phase = Phase::Done;
    u.result = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta")});
    state.setAppUpdate(u);
    CHECK(about->checkButton().enabled());
    // D-145: "Yükle" takes the release page's place; the page stays for a release that cannot be installed.
    CHECK_FALSE(about->releaseButton().visible());
    CHECK(about->installButton().visible());
    CHECK(about->changelogButton().visible());
    CHECK(about->contentHeight() > plain); // the notes under the band
    u.install = AppState::AppUpdate::Install{};
    u.install->stage = AppState::AppUpdate::Install::Stage::Failed;
    u.install->why = AppState::AppUpdate::Install::Why::NoAsset;
    state.setAppUpdate(u);
    REQUIRE(about->releaseButton().visible());
    about->releaseButton().onInvoke();
    REQUIRE(opened.size() == 1);
    CHECK(opened.front() == L"https://github.com/shadesofdeath/WinLove/releases/tag/v1.4.0-beta");
    // Without a check function (renders, tests): the button is harmless.
    auto quiet = std::make_unique<AboutPage>(state, tr, Language::Turkish, AboutPage::Intents{});
    quiet->checkButton().onInvoke();
}

TEST_CASE("D-140: Ayarlar › Güncellemeleri denetle is on by default and saved when switched") {
    const auto file = scratch(L"settings-toggle.json");
    AppState state{scratch(L"recent-settings.json"), file};
    auto page = std::make_unique<SettingsPage>(state, turkish(), SettingsPage::Intents{});
    SettingsPage* settings = page.get();
    Hosted hosted(std::move(page), {0, 0, 900, 1400});
    CHECK(settings->checkUpdatesToggle().isOn());
    settings->checkUpdatesToggle().onChange(false);
    CHECK_FALSE(state.settings().checkUpdates);
    CHECK_FALSE(settings->checkUpdatesToggle().isOn());
    CHECK_FALSE(AppSettings::load(file).checkUpdates); // saved at once
    settings->resetToDefaults();
    CHECK(state.settings().checkUpdates);
    CHECK(settings->checkUpdatesToggle().isOn());
}

TEST_CASE("D-140: the notice — one toast, the status bar segment until the About page is seen; a shell never checks") {
    AppState state{scratch(L"recent-shell.json"), scratch(L"settings-shell.json")};
    test::UiThread ui{test::UiThread::Mode::Queued}; // the reader / engine threads' posts wait for this thread
    Shell::Services services;
    services.minimize = services.toggleMaximize = services.close = services.toggleTheme = [] {};
    services.postToUi = ui.post();
    CHECK_FALSE(services.checkForUpdates); // nothing in a shell reaches the network unless App gives it
    ui::Host host(ui::HostServices{[] {}, nullptr, nullptr, test::graphics().text.get()});
    auto owned = std::make_unique<Shell>(turkish(), Language::Turkish, state, services);
    Shell* shell = owned.get();
    host.setRoot(std::move(owned));
    host.layout({1207, 700});

    AppState::AppUpdate u;
    u.phase = AppState::AppUpdate::Phase::Done;
    u.result = checkOf({release(L"v1.4.0-beta", L"WinLove 1.4.0 Beta")});
    u.notice = true;
    state.setAppUpdate(u);
    CHECK(state.appUpdate().toastShown); // its one toast went up
    CHECK_FALSE(statusModel(state, turkish(), Language::Turkish).update.empty());
    state.setAppUpdate(state.appUpdate()); // another notification: not a second toast (still marked)
    CHECK(state.appUpdate().toastShown);

    shell->showPage(PageId::About);
    CHECK_FALSE(state.appUpdate().notice); // seen
    CHECK(statusModel(state, turkish(), Language::Turkish).update.empty());
    shell->showPage(PageId::Source);
    CHECK_FALSE(state.appUpdate().notice);
}
