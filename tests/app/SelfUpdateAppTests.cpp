// D-145: "Yükle" and "Değişiklikleri gör" in the app — the controller's install flow with an installer of
// the test's own (download → check → swap → ready; each failure, a stop, a refusal while work runs or an
// image is mounted; the restart and its undo), the restart's command line, the About card in each state,
// the changelog window's sections, the notice toast's two actions. No network, no file of the build.
#include "app/Localization.h"
#include "app/controllers/UpdateController.h"
#include "app/pages/AboutPage.h"
#include "app/shell/Changelog.h"
#include "app/shell/ChromeText.h"
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Toast.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

using namespace wl;
using namespace wl::app;
using ui::RectF;
using Install = AppState::AppUpdate::Install;
using Why = Install::Why;
using Stage = Install::Stage;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"self-update-app";
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

constexpr std::int64_t kNow = 1794398400; // 2026-11-11 12:00 UTC

core::ReleaseCheck sample(const wchar_t* current = L"1.3.0 Beta") {
    auto releases = core::parseReleases(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"tests/integration/fixtures/changelog-sample.json"));
    REQUIRE(releases);
    return core::evaluateReleases(current, std::move(*releases));
}

void setAvailable(AppState& state) {
    AppState::AppUpdate u;
    u.phase = AppState::AppUpdate::Phase::Done;
    u.result = sample();
    u.at = kNow;
    state.setAppUpdate(std::move(u));
}

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

// What the installer did, step by step; each step's answer the test's own.
struct FakeInstaller {
    std::vector<std::string> steps;
    std::optional<Error> downloadFails;
    std::optional<core::VerifyFailure> verifyFails;
    std::optional<Error> swapFails;
    std::optional<Error> launchFails;
    std::optional<Error> notWritable;
    bool waitForCancel = false;
    std::wstring launchedArgs;
    core::UpdateExpectation expected;
    core::ReleaseAsset asset;
    int closed = 0;
    UpdateController::Installer make() {
        UpdateController::Installer in;
        in.exe = L"C:\\Program Files\\WinLove\\WinLove.exe";
        in.writable = [this](const std::filesystem::path&) -> Result<void> {
            steps.push_back("writable");
            if (notWritable) {
                return std::unexpected(*notWritable);
            }
            return {};
        };
        in.download = [this](const core::ReleaseAsset& a, const std::filesystem::path& exe, const core::TaskContext& task,
                             const std::function<void(std::uint64_t, std::uint64_t)>& bytes) -> Result<std::filesystem::path> {
            steps.push_back("download");
            asset = a;
            if (bytes) {
                bytes(a.size / 2, a.size);
                bytes(a.size, a.size);
            }
            for (int i = 0; waitForCancel && !task.cancel.cancelled() && i < 5000; ++i) {
                Sleep(1); // the test's "İptal" comes while the download "runs"
            }
            if (task.cancel.cancelled()) {
                return std::unexpected(Error{ErrorCode::Cancelled, L"download cancelled"});
            }
            if (downloadFails) {
                return std::unexpected(*downloadFails);
            }
            return core::updatePaths(exe).staged;
        };
        in.verify = [this](const std::filesystem::path&, const core::UpdateExpectation& e, const core::TaskContext&) -> Result<core::UpdateVerdict> {
            steps.push_back("verify");
            expected = e;
            core::UpdateVerdict v;
            if (verifyFails) {
                v.failure = verifyFails;
                v.detail = L"SHA-256 differs";
            }
            return v;
        };
        in.swap = [this](const std::filesystem::path&, const std::filesystem::path&) -> Result<void> {
            steps.push_back("swap");
            if (swapFails) {
                return std::unexpected(*swapFails);
            }
            return {};
        };
        in.rollback = [this](const std::filesystem::path&) -> Result<void> {
            steps.push_back("rollback");
            return {};
        };
        in.launch = [this](const std::filesystem::path&, const std::wstring& args) -> Result<void> {
            steps.push_back("launch");
            launchedArgs = args;
            if (launchFails) {
                return std::unexpected(*launchFails);
            }
            return {};
        };
        in.close = [this] { ++closed; };
        return in;
    }
};

struct Rig {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    Posted posted;
    FakeInstaller fake;
    UpdateController controller{state, posted.post(), [](const core::CancelToken&) -> Result<core::ReleaseCheck> {
                                    return std::unexpected(Error{ErrorCode::IoError, L"no network in tests"});
                                }};
    Rig() {
        setAvailable(state);
        controller.setInstaller(fake.make());
    }
    void finish() {
        controller.drain();
        posted.run();
        controller.drain();
        posted.run();
    }
    [[nodiscard]] const std::optional<Install>& install() const { return state.appUpdate().install; }
};

} // namespace

TEST_CASE("D-145: install — download, check, swap, then ready for the restart") {
    Rig rig;
    rig.controller.install();
    REQUIRE(rig.install());
    CHECK(rig.install()->stage == Stage::Downloading);
    CHECK(rig.install()->tag == L"v1.4.0-beta");
    CHECK(rig.install()->total == 11534336);
    rig.finish();
    CHECK(rig.fake.steps == std::vector<std::string>{"writable", "download", "verify", "swap"});
    CHECK(rig.fake.asset.name == L"WinLove.exe");
    CHECK(rig.fake.expected.size == 11534336);
    CHECK(rig.fake.expected.sha256 == L"9b1e04c2a4f1b0d6e8c3a7f5b2d9e4c1a6f3b8d0e7c5a2f9b4d1e8c3a6f0b7d2");
    REQUIRE(rig.fake.expected.version);
    CHECK(*rig.fake.expected.version == *core::parseAppVersion(L"v1.4.0-beta"));
    REQUIRE(rig.install());
    CHECK(rig.install()->stage == Stage::Ready);
    CHECK(rig.install()->done == rig.install()->total);
    // A second click while it is ready: nothing again.
    rig.controller.install();
    rig.finish();
    CHECK(rig.fake.steps.size() == 4);

    // The restart: the new exe with where it came from, then this window closes.
    CHECK_FALSE(rig.controller.restart());
    CHECK(rig.fake.steps.back() == "launch");
    CHECK(rig.fake.launchedArgs.find(L"--relaunched") != std::wstring::npos);
    CHECK(rig.fake.launchedArgs.find(L"\"--updated-from=" WL_VERSION_LABEL L"\"") != std::wstring::npos);
    CHECK(rig.fake.closed == 1);
}

TEST_CASE("D-145: the restart's command line never asks for elevation") {
    CHECK(UpdateController::restartArguments(L"1.3.0 Beta", true) == L"--relaunched \"--updated-from=1.3.0 Beta\"");
    CHECK(UpdateController::restartArguments(L"1.3.0 Beta", false) == L"--relaunched \"--updated-from=1.3.0 Beta\" --no-elevate");
}

TEST_CASE("D-145: install refused — an image mounted, work running, no asset, a folder that cannot be written") {
    {
        Rig rig;
        rig.state.setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\w\\install.wim", 1, L"Pro"});
        CHECK(installBlocker(rig.state) == Why::Mounted);
        rig.controller.install();
        REQUIRE(rig.install());
        CHECK(rig.install()->stage == Stage::Failed);
        CHECK(rig.install()->why == Why::Mounted);
        CHECK(rig.fake.steps.empty());
        // Unmounted: "Yeniden dene" goes on.
        rig.state.setMounted(std::nullopt);
        rig.controller.install();
        rig.finish();
        CHECK(rig.install()->stage == Stage::Ready);
    }
    {
        Rig rig;
        AppState::IsoRun iso;
        iso.running = true;
        rig.state.setIsoRun(iso);
        CHECK(installBlocker(rig.state) == Why::Busy);
        rig.controller.install();
        CHECK(rig.install()->why == Why::Busy);
        rig.state.setIsoRun(std::nullopt);
        AppState::ApplyRun run;
        rig.state.setApplyRun(run); // Running
        CHECK(installBlocker(rig.state) == Why::Busy);
        rig.state.setApplyRun(std::nullopt);
        rig.state.beginOperation(EngineOperation{EngineOperation::Kind::Exporting, L"Pro", L"C:\\x", 1});
        CHECK(installBlocker(rig.state) == Why::Busy);
        rig.state.endOperation();
        CHECK_FALSE(installBlocker(rig.state));
    }
    {
        Rig rig;
        rig.fake.notWritable = Error{ErrorCode::AccessDenied, L"the program's folder cannot be written to"};
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        CHECK(rig.install()->why == Why::NotWritable);
        CHECK(rig.fake.steps == std::vector<std::string>{"writable"});
    }
    {
        Rig rig;
        AppState::AppUpdate u = rig.state.appUpdate();
        u.result->latest->assets.clear(); // a release with nothing to install
        rig.state.setAppUpdate(u);
        rig.controller.install();
        CHECK(rig.install()->why == Why::NoAsset);
        CHECK(rig.fake.steps.empty());
    }
    {
        // Up to date: "Yükle" does nothing.
        Rig rig;
        AppState::AppUpdate u = rig.state.appUpdate();
        u.result = sample(L"1.4.0 Beta");
        rig.state.setAppUpdate(u);
        rig.controller.install();
        CHECK_FALSE(rig.install());
    }
}

TEST_CASE("D-145: install fails — the download, the check, the swap; a stop; the restart undone") {
    {
        Rig rig;
        rig.fake.downloadFails = Error{ErrorCode::IoError, L"the connection broke off"};
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        rig.finish();
        CHECK(rig.install()->stage == Stage::Failed);
        CHECK(rig.install()->why == Why::Download);
        CHECK(rig.install()->detail == L"the connection broke off");
    }
    {
        Rig rig;
        rig.fake.verifyFails = core::VerifyFailure::Hash;
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        rig.finish();
        CHECK(rig.install()->why == Why::Verify);
        CHECK(rig.install()->detail.starts_with(L"sha256: "));
        CHECK(std::ranges::find(rig.fake.steps, "swap") == rig.fake.steps.end()); // never swapped in
    }
    {
        Rig rig;
        rig.fake.swapFails = Error{ErrorCode::IoError, L"cannot move the update into place"};
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        rig.finish();
        CHECK(rig.install()->why == Why::Swap);
    }
    {
        Rig rig;
        rig.fake.waitForCancel = true;
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        rig.controller.cancelInstall(); // the download sees the stop
        rig.finish();
        CHECK(rig.install()->stage == Stage::Failed);
        CHECK(rig.install()->why == Why::Cancelled);
    }
    {
        Rig rig;
        rig.fake.launchFails = Error{ErrorCode::IoError, L"the new WinLove could not be started"};
        rig.controller.setInstaller(rig.fake.make());
        rig.controller.install();
        rig.finish();
        REQUIRE(rig.install()->stage == Stage::Ready);
        rig.controller.restart();
        CHECK(rig.fake.steps.back() == "rollback"); // the old exe back in place
        CHECK(rig.install()->stage == Stage::Failed);
        CHECK(rig.install()->why == Why::Restart);
        CHECK(rig.fake.closed == 0);
    }
    {
        // Ready, then an image mounted: the restart waits (and says why).
        Rig rig;
        rig.controller.install();
        rig.finish();
        rig.state.setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\w\\install.wim", 1, L"Pro"});
        CHECK(rig.controller.restart() == Why::Mounted);
        CHECK(std::ranges::find(rig.fake.steps, "launch") == rig.fake.steps.end());
        CHECK(rig.install()->stage == Stage::Ready);
    }
}

TEST_CASE("D-145: the About card — install, what is new, the download, ready, failed, updated") {
    AppState state{scratch(L"recent-about.json"), scratch(L"settings-about.json")};
    const auto& tr = turkish();
    setAvailable(state);
    auto view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.available);
    CHECK(view.install);
    CHECK(view.changelog);
    CHECK_FALSE(view.openDownload);
    CHECK_FALSE(view.installing);

    AppState::AppUpdate u = state.appUpdate();
    Install st;
    st.tag = L"v1.4.0-beta";
    st.name = L"WinLove 1.4.0 Beta";
    st.total = 10 * 1024 * 1024;
    st.done = 4 * 1024 * 1024;
    u.install = st;
    state.setAppUpdate(u);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.installing);
    CHECK_FALSE(view.install);
    CHECK(view.title == L"İndiriliyor: WinLove 1.4.0 Beta");
    CHECK(view.progress == doctest::Approx(0.4f));
    CHECK(view.progressText == L"%40");

    u.install->stage = Stage::Ready;
    state.setAppUpdate(u);
    view = updateCardView(state, tr, Language::Turkish, kNow);
    CHECK(view.restart);
    CHECK(view.title == L"Güncelleme hazır: WinLove 1.4.0 Beta");

    const std::pair<Why, bool> failures[] = {{Why::NoAsset, true}, {Why::NotWritable, true}, {Why::Verify, true},
                                             {Why::Swap, true},    {Why::Restart, true},     {Why::Download, false},
                                             {Why::Cancelled, false}, {Why::Mounted, false}, {Why::Busy, false}};
    for (const auto& [why, page] : failures) {
        u.install->stage = Stage::Failed;
        u.install->why = why;
        u.install->detail = L"x";
        state.setAppUpdate(u);
        view = updateCardView(state, tr, Language::Turkish, kNow);
        CAPTURE(static_cast<int>(why));
        CHECK(view.title == tr.get(Str::AppUpdateSelfFailedTitle));
        CHECK(view.openDownload == page);
        CHECK(view.retry == !page); // "Yeniden dene" when trying again can work
        CHECK(view.install == !page);
        CHECK_FALSE(view.detail.empty());
    }

    // The instance a swap started.
    AppState fresh{scratch(L"recent-updated.json"), scratch(L"settings-updated.json")};
    AppState::AppUpdate updated;
    updated.updatedFrom = L"1.2.3 Beta";
    fresh.setAppUpdate(updated);
    view = updateCardView(fresh, tr, Language::Turkish, kNow);
    CHECK(view.title == L"WinLove güncellendi: 1.2.3 Beta → " WL_VERSION_LABEL);
    CHECK(view.tone == UpdateCardView::Tone::Success);

    // The page's buttons call what they are given.
    int installs = 0;
    int changelogs = 0;
    AboutPage::Intents intents;
    intents.install = [&] { ++installs; };
    intents.changelog = [&] { ++changelogs; };
    setAvailable(state);
    AboutPage page(state, tr, Language::Turkish, intents);
    page.setBounds({0, 0, 900, 1200});
    CHECK(page.installButton().visible());
    CHECK(page.changelogButton().visible());
    CHECK_FALSE(page.releaseButton().visible());
    page.installButton().onInvoke();
    page.changelogButton().onInvoke();
    CHECK(installs == 1);
    CHECK(changelogs == 1);
}

TEST_CASE("D-145: the status bar says the download and the restart") {
    AppState state{scratch(L"recent-status.json"), scratch(L"settings-status.json")};
    setAvailable(state);
    AppState::AppUpdate u = state.appUpdate();
    u.notice = true;
    state.setAppUpdate(u);
    CHECK(statusModel(state, turkish(), Language::Turkish).update == L"Yeni sürüm: WinLove 1.4.0 Beta");
    Install st;
    st.total = 200;
    st.done = 50;
    u.install = st;
    state.setAppUpdate(u);
    CHECK(statusModel(state, turkish(), Language::Turkish).update == L"Güncelleme indiriliyor · %25");
    u.install->stage = Stage::Ready;
    state.setAppUpdate(u);
    CHECK(statusModel(state, turkish(), Language::Turkish).update == L"Güncelleme hazır · yeniden başlat");
    u.install->stage = Stage::Failed;
    u.notice = false;
    state.setAppUpdate(u);
    CHECK(statusModel(state, turkish(), Language::Turkish).update.empty());
}

TEST_CASE("D-145: the changelog window's sections — newer first, then the older ones under a line") {
    const auto check = sample();
    const auto sections = changelogSections(check, turkish(), Language::Turkish, kNow);
    REQUIRE(sections.size() == 5);
    CHECK(sections[0].title == L"WinLove 1.4.0 Beta");
    CHECK(sections[0].meta == L"v1.4.0-beta · 1 gün önce yayımlandı");
    REQUIRE(sections[0].tags.size() == 2);
    CHECK(sections[0].tags[0].text == L"Yeni");
    CHECK(sections[0].tags[1].text == L"Ön sürüm");
    CHECK(sections[0].divider.empty());
    CHECK_FALSE(sections[0].body.blocks.empty());
    CHECK(sections[1].title == L"WinLove 1.3.1 Beta");
    CHECK(sections[2].title == L"WinLove 1.3.0 Beta");
    CHECK(sections[2].divider == L"Önceki sürümler");
    CHECK(sections[2].tags[0].text == L"Bu sürüm");
    CHECK(sections[4].body.blocks.empty()); // "body": null
    CHECK(sections[4].emptyBody == turkish().get(Str::ChangelogEmpty));
    CHECK(changelogSubtitle(check, turkish()) == L"Bu bilgisayarda 1.3.0 Beta · 2 yeni sürüm");
    // Up to date: the older ones only, no line above them.
    const auto current = sample(L"1.4.0 Beta");
    const auto latest = changelogSections(current, turkish(), Language::Turkish, kNow);
    REQUIRE(latest.size() == 3);
    CHECK(latest[0].divider.empty());
    CHECK(changelogSubtitle(current, turkish()) == L"Bu bilgisayarda 1.4.0 Beta · güncel");

    // The view built and laid out without a window; Esc closes it.
    int closed = 0;
    ChangelogView::Services services;
    services.close = [&] { ++closed; };
    auto root = std::make_unique<ChangelogView>(turkish(), Language::Turkish, services);
    ChangelogView* view = root.get();
    ui::Host host(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
    host.setRoot(std::move(root));
    view->setCheck(check, kNow);
    host.layout({780, 820});
    CHECK(view->notes().runCount() > 50);
    CHECK(view->notes().contentHeight() > 820);
    view->onKeyDown({VK_ESCAPE, false, false, false});
    CHECK(closed == 1);
}

TEST_CASE("D-145: the notice toast has two actions, each one its own") {
    ui::Host host(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
    auto owned = std::make_unique<ui::Toast>();
    ui::Toast* toast = owned.get();
    host.setRoot(std::move(owned));
    host.layout({1200, 700});
    toast->setBounds({600, 500, ui::Toast::kWidth, ui::Toast::kHeight});
    int install = 0;
    int changes = 0;
    toast->show(ui::InfoKind::Info, L"Yeni WinLove sürümü: WinLove 1.4.0 Beta", L"2 gün önce GitHub'da yayımlandı.", L"Yükle",
                [&] { ++install; }, L"Değişiklikleri gör", [&] { ++changes; });
    const RectF b = toast->bounds();
    CHECK(b.height > ui::Toast::kHeight); // a row of its own for the two actions
    // The first action sits at the right of the last row, the second before it.
    const ui::PointF first{b.right() - 30, b.bottom() - 20};
    toast->onPointerMove(first);
    toast->onClick();
    CHECK(install == 1);
    CHECK(changes == 0);
    toast->show(ui::InfoKind::Info, L"x", L"y", L"Yükle", [&] { ++install; }, L"Değişiklikleri gör", [&] { ++changes; });
    const RectF c = toast->bounds();
    toast->onPointerMove({c.right() - 120, c.bottom() - 20});
    toast->onClick();
    CHECK(changes == 1);
    CHECK(install == 1);
    // One action: as before (the action beside the text, no extra row).
    toast->show(ui::InfoKind::Info, L"x", L"y", L"Göster", [&] { ++install; });
    CHECK(toast->bounds().height < c.height);
}
