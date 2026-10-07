// P05: plan groups + estimates, duration formatting, the Apply page mode, and what start()
// refuses (no mount / empty queue / not elevated is covered by canStart()).
#include "app/ApplyReport.h"
#include "app/Format.h"
#include "app/controllers/ApplyController.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/pages/ApplyPage.h"
#include "app/pages/apply/RiskConfirm.h"

#include <doctest.h>

#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Phase;

namespace {
std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"apply";
    std::filesystem::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE("plan groups follow the phase order and sum their estimates") {
    core::ops::ChangeSet set;
    set.add(Operation{OpKind::EnableFeature, L"WSL"});
    set.add(Operation{OpKind::RemoveCapability, L"OpenSSH.Client"});
    set.add(Operation{OpKind::DisableFeature, L"NetFx3"});
    set.add(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
    const auto plan = core::ops::plan(set);
    const auto groups = core::ops::groups(plan);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].phase == Phase::Remove);
    CHECK(groups[1].phase == Phase::Features);
    CHECK(groups[1].count == 2);
    CHECK(groups[1].first == 1);
    CHECK(groups[1].estimateSeconds ==
          doctest::Approx(core::ops::estimateSeconds(OpKind::EnableFeature) + core::ops::estimateSeconds(OpKind::DisableFeature)));
    CHECK(groups[2].phase == Phase::Settings);
}

TEST_CASE("formatDuration: exact and approximate, Turkish and English") {
    CHECK(formatDuration(168, Language::Turkish) == L"2 dk 48 sn");
    CHECK(formatDuration(38, Language::Turkish) == L"38 sn");
    CHECK(formatDuration(3840, Language::English) == L"1 h 4 min");
    CHECK(formatDuration(95, Language::Turkish, true) == L"~2 dk");
    CHECK(formatDuration(0.2, Language::English, true) == L"~1 s");
}

TEST_CASE("Apply page mode: no mount, empty queue, summary, running, done") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::NoMount);
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Empty);
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Summary);

    AppState::ApplyRun run;
    state.setApplyRun(run);
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Running);
    // The queue is frozen while Uygula runs: edits would be lost or undone by the post-run cleanup.
    CHECK(state.queueLocked());
    state.queue(Operation{OpKind::EnableFeature, L"Other"});
    CHECK_FALSE(state.unqueue(OpKind::EnableFeature, L"WSL"));
    CHECK(state.changes().size() == 1);
    run.stage = AppState::ApplyRun::Stage::Done;
    state.setApplyRun(run);
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Summary); // queue still has work
    state.setMounted(std::nullopt);                                // committed: unmounted, queue cleared
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Done);
}

TEST_CASE("ApplyController: high-risk list and canStart") {
    AppState state{scratch(L"recent2.json"), scratch(L"settings2.json")};
    ApplyController controller(state, {[](std::function<void()> fn) { fn(); }, {}, {}, {}});
    CHECK_FALSE(controller.canStart()); // nothing mounted
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK_FALSE(controller.canStart()); // empty queue
    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High});
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    CHECK(controller.canStart());
    REQUIRE(controller.highRisk().size() == 1);
    CHECK(controller.highRisk()[0].target == L"OpenSSH.Client");
}

TEST_CASE("risk confirm rows: removals with their size, a high-risk setting once by its name") {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/en.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const Localization strings = Localization::fromJson(text.str()).value();
    std::ifstream catalogFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json", std::ios::binary);
    std::stringstream catalogText;
    catalogText << catalogFile.rdbuf();
    const ImageSettingsCatalog catalog = ImageSettingsCatalog::parse(catalogText.str()).value();

    AppState state{scratch(L"recent-risk.json"), scratch(L"settings-risk.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    Operation winre{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High};
    winre.sizeDelta = -(5ll << 20);
    state.queue(winre);
    // "Microsoft Defender (virus protection)": off is many registry writes (and services), all high risk.
    const auto defender = std::ranges::find(catalog.settings(), "defender", &ImageSetting::id);
    REQUIRE(defender != catalog.settings().end());
    state.queueMany(ImageSettingsController::operationsFor(*defender, 0));

    const auto risky = ApplyController(state, {[](std::function<void()> fn) { fn(); }, {}, {}, {}}).highRisk();
    REQUIRE(risky.size() > 2);
    const auto items = RiskConfirm::itemsFor(state, catalog, strings, Language::English, risky);
    REQUIRE(items.size() == 2); // the removal, and Defender once — not one row per registry value
    CHECK(items[0].removal);
    CHECK(items[0].name == L"OpenSSH.Client");
    CHECK(items[0].size == formatBytes(5ull << 20, Language::English));
    CHECK_FALSE(items[1].removal);
    CHECK(items[1].name == L"Microsoft Defender (virus protection)");
    CHECK(items[1].size == L"Off");
}

TEST_CASE("apply report: one HTML page with the sizes and every step's result") {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const Localization strings = Localization::fromJson(text.str()).value();

    AppState state{scratch(L"recent-report.json"), scratch(L"settings-report.json")};
    core::ops::ChangeSet changes;
    changes.add(Operation{OpKind::RemoveAppx, L"Contoso.App_1.0_x64__abc", L"Contoso <App> & Co"});
    changes.add(Operation{OpKind::RemoveAppx, L"Microsoft.SecHealthUI_1.0_x64__8wekyb3d8bbwe", L"Windows Güvenliği"});
    changes.add(Operation{OpKind::SetServiceStart, L"DiagTrack", L"disabled"});

    AppState::ApplyRun run;
    run.changes = changes;
    run.plan = core::ops::plan(changes);
    run.edition = L"Windows 11 Pro";
    run.sizeBefore = 20ull << 30;
    run.sizeAfter = 15ull << 30;
    core::ops::ApplyJobResult result;
    result.report.results.push_back({run.plan.steps[0], {}});
    result.report.results.push_back(
        {run.plan.steps[1], fail(ErrorCode::DismFailure, L"refused", L"remove appx", static_cast<std::int32_t>(0x80073CFA))});
    result.report.completed = false; // stopped before the third step
    result.committed = true;
    result.elapsed = std::chrono::milliseconds(95000);
    result.stepTimes = {std::chrono::milliseconds(3200), std::chrono::milliseconds(900)};
    run.result = result;

    using namespace std::chrono;
    const auto when = sys_days{2026y / September / 30} + 12h;
    const std::string html = applyReportHtml(state, run, strings, Language::Turkish, when);
    auto has = [&](const char* needle) { return html.find(needle) != std::string::npos; };
    CHECK(html.starts_with("<!doctype html>"));
    CHECK(has("<meta charset=\"utf-8\">"));
    CHECK(has("Windows 11 Pro"));
    CHECK(has("2026-09-"));
    CHECK(has("Contoso &lt;App&gt; &amp; Co")); // names are text, not markup
    CHECK_FALSE(has("<App>"));
    CHECK(has("class=\"ok\">Tamam"));
    CHECK(has("Windows bu uygulamanın imajdan kaldırılmasına izin vermiyor")); // the reason, in words
    CHECK(has("class=\"none\">Çalışmadı"));                                  // the step the run never reached
    CHECK(has("3.2 s"));
    CHECK(has("1 / 3"));                        // one step of three went into the image
    CHECK(has("%25"));                          // 5 of 20 GiB gained
    CHECK(has("İmaj kaydedildi ve çözüldü"));
    CHECK(applyReportFileName(when).starts_with(L"WinLove-rapor-202609"));
    CHECK(applyReportFileName(when).ends_with(L".html"));

    // A run that could not start still gives a page, with the error on it.
    AppState::ApplyRun broken;
    broken.edition = L"Pro";
    broken.error = Error{ErrorCode::AccessDenied, L"DISM needs an elevated (administrator) process", L"apply"};
    const std::string failed = applyReportHtml(state, broken, strings, Language::Turkish, when);
    CHECK(failed.find("DISM needs an elevated") != std::string::npos);
    CHECK(failed.find("</html>") != std::string::npos);
}
