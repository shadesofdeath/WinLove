// D-144: "Bitince ISO oluştur" — the state machine (a saved run starts the ISO; a failed / stopped / held /
// kept run, an unsaved further edition, a refused source or a missing welcome do not, and say why), the
// ISO's end copied into the run (done, failed, stopped), the option only where ISO Oluştur works, the ISO
// page's first values shared, the run summary / overview / Apply page in each stage — without a window and
// without an ISO being built (the engine's start is the test's own).
#include "app/controllers/ApplyController.h"
#include "app/controllers/AutoIsoController.h"
#include "app/pages/ApplyPage.h"
#include "app/pages/IsoPage.h"
#include "app/pages/iso/IsoOptions.h"
#include "app/pages/apply/RunSummary.h"
#include "app/pages/overview/OverviewModel.h"
#include "ui/anim/Tween.h"
#include "support/UiThread.h"

#include <doctest.h>

#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using AutoIso = AppState::ApplyRun::AutoIso;
using Skip = AutoIso::Skip;
using IsoState = AutoIso::State;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"auto-iso";
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

Localization strings(const wchar_t* lang) {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / (std::wstring(L"resources/strings/") + lang + L".json"), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return Localization::fromJson(text.str()).value();
}

core::SourceInfo isoSource(core::ImageFormat format = core::ImageFormat::Iso) {
    core::SourceInfo source;
    source.path = format == core::ImageFormat::Vhdx ? L"D:\\Disks\\Win11.vhdx" : L"D:\\Isos\\x64-9448.iso";
    source.format = format;
    source.volumeLabel = L"CCCOMA_X64FRE_TR-TR_DV9";
    source.installImage = L"sources/install.wim";
    core::ImageInfo pro;
    pro.index = 1;
    pro.name = L"Windows 11 Pro";
    pro.build = 26200;
    pro.totalBytes = 24ull << 30;
    source.install.images.push_back(pro);
    return source;
}

AppState::ApplyRun finishedRun(const core::ops::ChangeSet& changes) {
    AppState::ApplyRun run;
    run.changes = changes;
    run.plan = core::ops::plan(changes);
    run.groups = core::ops::groups(run.plan);
    run.stage = AppState::ApplyRun::Stage::Done;
    run.stepState.assign(run.plan.steps.size(), 2);
    core::ops::ApplyJobResult result;
    result.report.completed = true;
    for (const auto& step : run.plan.steps) {
        result.report.results.push_back({step, {}});
        result.stepTimes.push_back(std::chrono::milliseconds(1000));
    }
    result.committed = true;
    result.elapsed = std::chrono::milliseconds(60'000);
    run.result = std::move(result);
    return run;
}

// A state with a source, the option on, an image mounted and something queued; the ISO folder fixed.
struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::vector<IsoController::Request> started;
    std::optional<IsoController::Blocker> blocker;
    std::wstring welcomeMissing;
    bool engineStarts = true;
    AutoIsoController controller{state, AutoIsoController::Hooks{
                                            [this] { return blocker; },
                                            [this](IsoController::Request r) {
                                                started.push_back(r);
                                                if (!engineStarts) {
                                                    return;
                                                }
                                                AppState::IsoRun iso; // what IsoController::start leaves
                                                iso.running = true;
                                                iso.afterApply = r.afterApply;
                                                iso.output = r.output;
                                                state.setIsoRun(std::move(iso));
                                            },
                                            [this] { return welcomeMissing; },
                                        }};
    explicit Fixture(core::ImageFormat format = core::ImageFormat::Iso, bool on = true) {
        state.setSource(isoSource(format));
        AppSettings s = state.settings();
        s.isoAfterApply = on;
        s.isoFolder = L"D:\\ISO";
        state.setSettings(s);
        state.setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 1, L"Windows 11 Pro"});
        state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    }
    // ApplyController::start's run, then Shell::startApplyRun's attach().
    void startRun() {
        AppState::ApplyRun run;
        run.changes = state.changes();
        run.plan = core::ops::plan(run.changes);
        run.groups = core::ops::groups(run.plan);
        run.stepState.assign(run.plan.steps.size(), 0);
        state.setApplyRun(run);
        controller.attach();
    }
    // ApplyController's end: the run's outcome, the image unmounted when it saved, then `finished`.
    void endRun(const std::function<void(AppState::ApplyRun&)>& outcome) {
        auto run = finishedRun(state.changes());
        run.autoIso = state.applyRun()->autoIso;
        outcome(run);
        const bool saved = run.result && run.result->committed && !run.result->commitError;
        state.setApplyRun(std::move(run));
        if (saved) {
            state.setMounted(std::nullopt);
        }
        controller.runFinished();
    }
    [[nodiscard]] const AutoIso& iso() const { return *state.applyRun()->autoIso; }
};

} // namespace

TEST_CASE("D-144: a run's end decides — saved starts the ISO, anything else says why not") {
    const auto changes = [] {
        core::ops::ChangeSet c;
        c.add(Operation{OpKind::EnableFeature, L"WSL"});
        c.add(Operation{OpKind::DisableFeature, L"MediaPlayback"});
        return c;
    }();
    auto run = finishedRun(changes);
    CHECK(autoIsoSkip(run) == Skip::None);
    run.result->report.results.front().outcome = fail(ErrorCode::DismFailure, L"permanent", L"", 0);
    CHECK(autoIsoSkip(run) == Skip::None); // saved with a skipped step: still the image to install

    run = finishedRun(changes);
    run.error = Error{ErrorCode::AccessDenied, L"not elevated"};
    CHECK(autoIsoSkip(run) == Skip::RunFailed);
    run = finishedRun(changes);
    run.result.reset();
    CHECK(autoIsoSkip(run) == Skip::RunFailed);
    run = finishedRun(changes);
    run.result->commitError = Error{ErrorCode::DismFailure, L"commit"};
    run.result->committed = false;
    CHECK(autoIsoSkip(run) == Skip::RunFailed);
    run = finishedRun(changes);
    run.result->report.completed = false; // "Durdur" (or DISM's session lost)
    run.result->committed = false;
    CHECK(autoIsoSkip(run) == Skip::Stopped);
    run = finishedRun(changes);
    run.result->held = true; // a step failed: nothing saved
    run.result->committed = false;
    CHECK(autoIsoSkip(run) == Skip::NotSaved);
    run = finishedRun(changes);
    run.result->kept = true; // "Dur: bağlı klasörü elle düzenleyeceğim", then Durdur at the pause
    run.result->committed = false;
    CHECK(autoIsoSkip(run) == Skip::NotSaved);
    run = finishedRun(changes);
    run.extras = {{2, L"Windows 11 Home", AppState::ApplyRun::ExtraEdition::State::Done},
                  {3, L"Windows 11 Education", AppState::ApplyRun::ExtraEdition::State::Failed}};
    CHECK(autoIsoSkip(run) == Skip::Edition);
    run.extras[1].state = AppState::ApplyRun::ExtraEdition::State::Done;
    CHECK(autoIsoSkip(run) == Skip::None);
}

TEST_CASE("D-144: the option and its file — ISO Oluştur's first values, only where it works") {
    Fixture f;
    CHECK_FALSE(f.controller.unavailable());
    CHECK(f.controller.output() == std::filesystem::path(L"D:\\ISO\\x64-9448_WinLove.iso"));
    // The ISO page's first values are the same ones.
    CHECK(IsoController::defaultFileName(f.state) == L"x64-9448_WinLove.iso");
    CHECK(IsoController::defaultFolder(f.state) == std::filesystem::path(L"D:\\ISO"));
    const auto request = IsoController::defaultRequest(f.state, f.controller.output());
    CHECK(request.label == L"CCCOMA_X64FRE_TR-TR_DV9");
    CHECK(request.boot == core::BootMode::UefiAndBios);
    CHECK(request.sha256);
    CHECK(request.bootBypass);
    CHECK(request.legacySetupAuto);
    CHECK_FALSE(request.legacySetup);
    CHECK(request.repack == IsoController::Repack::AsIs);
    CHECK(request.workCopy == IsoController::Request::WorkCopy::Keep);
    CHECK(request.afterApply);
    CHECK_FALSE(request.openFolder);
    CHECK_FALSE(request.usb);

    // "Değiştir…": the folder is ISO Oluştur's too, the name kept for this source only.
    f.controller.setOutput(L"E:\\Builds\\custom");
    CHECK(f.controller.output() == std::filesystem::path(L"E:\\Builds\\custom.iso"));
    CHECK(f.state.settings().isoFolder == std::filesystem::path(L"E:\\Builds"));
    auto other = isoSource();
    other.path = L"D:\\Isos\\other.iso";
    f.state.setSource(other);
    CHECK(f.controller.output() == std::filesystem::path(L"E:\\Builds\\other_WinLove.iso"));

    // Not offered where ISO Oluştur cannot work.
    Fixture vhd(core::ImageFormat::Vhdx);
    CHECK(vhd.controller.unavailable() == IsoController::Blocker::VirtualDisk);
    Fixture wim(core::ImageFormat::Wim);
    CHECK(wim.controller.unavailable() == IsoController::Blocker::WimOnly);
    AppState none{scratch(L"recent-none.json"), scratch(L"settings-none.json")};
    CHECK(IsoController::sourceBlocker(none) == IsoController::Blocker::NoSource);
    // A mounted image and a running Apply are what such a run has: never a reason here.
    CHECK_FALSE(IsoController::sourceBlocker(f.state));
}

TEST_CASE("D-144: success — the ISO starts with ISO Oluştur's values; its end goes into the run") {
    Fixture f;
    f.startRun();
    REQUIRE(f.state.applyRun()->autoIso);
    CHECK(f.iso().state == IsoState::Waiting);
    CHECK(f.iso().output == std::filesystem::path(L"D:\\ISO\\x64-9448_WinLove.iso"));
    CHECK(summarizeRun(*f.state.applyRun(), ui::nowMs()).iso == RunSummary::Iso::Waiting);

    f.endRun([](AppState::ApplyRun&) {});
    REQUIRE(f.started.size() == 1);
    CHECK(f.started[0].output == f.iso().output);
    CHECK(f.started[0].afterApply);
    CHECK(f.iso().state == IsoState::Running);
    CHECK(summarizeRun(*f.state.applyRun(), ui::nowMs()).isoRunning());

    // The engine's progress is not the run's business; its end is.
    f.state.isoRunMutable()->fraction = 0.5;
    f.state.notifyIso();
    CHECK(f.iso().state == IsoState::Running);
    f.state.isoRunMutable()->running = false;
    f.state.isoRunMutable()->result = core::IsoResult{6ull << 30, {}};
    f.state.notifyIso();
    CHECK(f.iso().state == IsoState::Done);
    CHECK(f.iso().bytes == 6ull << 30);
    // A later ISO from the ISO page does not change what this run made.
    AppState::IsoRun manual;
    manual.running = false;
    manual.error = Error{ErrorCode::IoError, L"disk full"};
    f.state.setIsoRun(manual);
    CHECK(f.iso().state == IsoState::Done);
    // A second "finished" (none comes, but) never starts a second ISO.
    f.controller.runFinished();
    CHECK(f.started.size() == 1);
}

TEST_CASE("D-144: the ISO fails or is stopped — the run says so") {
    for (const bool cancelled : {false, true}) {
        Fixture f;
        f.startRun();
        f.endRun([](AppState::ApplyRun&) {});
        REQUIRE(f.iso().state == IsoState::Running);
        auto& iso = *f.state.isoRunMutable();
        iso.running = false;
        iso.error = cancelled ? Error{ErrorCode::Cancelled, L"cancelled"} : Error{ErrorCode::IoError, L"disk full", L"D:\\ISO", 0};
        f.state.notifyIso();
        CHECK(f.iso().state == (cancelled ? IsoState::Cancelled : IsoState::Failed));
        if (!cancelled) {
            REQUIRE(f.iso().error);
            CHECK(f.iso().error->message == L"disk full");
        }
    }
}

TEST_CASE("D-144: failed, stopped, held, kept or refused — no ISO, and why") {
    struct Case {
        const char* name;
        std::function<void(AppState::ApplyRun&)> outcome;
        Skip skip;
    };
    const Case cases[] = {
        {"could not start", [](AppState::ApplyRun& r) { r.error = Error{ErrorCode::AccessDenied, L"x"}; r.result.reset(); }, Skip::RunFailed},
        {"save failed", [](AppState::ApplyRun& r) { r.result->commitError = Error{ErrorCode::DismFailure, L"x"}; r.result->committed = false; },
         Skip::RunFailed},
        {"stopped", [](AppState::ApplyRun& r) { r.result->report.completed = false; r.result->committed = false; }, Skip::Stopped},
        {"held", [](AppState::ApplyRun& r) { r.result->held = true; r.result->committed = false; }, Skip::NotSaved},
        {"kept at the pause", [](AppState::ApplyRun& r) { r.result->kept = true; r.result->committed = false; }, Skip::NotSaved},
    };
    for (const auto& c : cases) {
        CAPTURE(c.name);
        Fixture f;
        f.startRun();
        f.endRun(c.outcome);
        CHECK(f.started.empty());
        CHECK(f.iso().state == IsoState::NotStarted);
        CHECK(f.iso().skip == c.skip);
        CHECK(summarizeRun(*f.state.applyRun(), ui::nowMs()).iso == RunSummary::Iso::NotStarted);
    }
    // Saved, but ISO Oluştur refuses now (the answer file was changed during the run) / the welcome is missing.
    Fixture refused;
    refused.startRun();
    refused.blocker = IsoController::Blocker::UnattendInvalid;
    refused.endRun([](AppState::ApplyRun&) {});
    CHECK(refused.started.empty());
    CHECK(refused.iso().skip == Skip::Blocked);
    CHECK(refused.iso().blocker == static_cast<int>(IsoController::Blocker::UnattendInvalid));
    Fixture welcome;
    welcome.startRun();
    welcome.welcomeMissing = L"1";
    welcome.endRun([](AppState::ApplyRun&) {});
    CHECK(welcome.started.empty());
    CHECK(welcome.iso().skip == Skip::Welcome);
    // The engine refused at its door (nothing started): failed, not "running" for ever.
    Fixture door;
    door.engineStarts = false;
    door.startRun();
    door.endRun([](AppState::ApplyRun&) {});
    CHECK(door.started.size() == 1);
    CHECK(door.iso().state == IsoState::Failed);
}

TEST_CASE("D-144: the option off, or not possible — the run carries no ISO") {
    Fixture off(core::ImageFormat::Iso, /*on=*/false);
    off.startRun();
    CHECK_FALSE(off.state.applyRun()->autoIso);
    off.endRun([](AppState::ApplyRun&) {});
    CHECK(off.started.empty());
    CHECK(summarizeRun(*off.state.applyRun(), ui::nowMs()).iso == RunSummary::Iso::None);
    Fixture vhd(core::ImageFormat::Vhdx);
    vhd.startRun();
    CHECK_FALSE(vhd.state.applyRun()->autoIso);
}

TEST_CASE("D-144: the overview and the Apply page in each stage, built without a window") {
    const Localization tr = strings(L"tr");
    std::ifstream catalogFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json", std::ios::binary);
    std::stringstream catalogText;
    catalogText << catalogFile.rdbuf();
    const ImageSettingsCatalog catalog = ImageSettingsCatalog::parse(catalogText.str()).value();
    Fixture f;
    test::UiThread ui;
    ApplyController apply(f.state, {ui.post(), {}, {}, {}});
    std::vector<std::filesystem::path> shown;
    int cancels = 0;
    ApplyPage::Intents intents;
    intents.isoUnavailable = [&] { return f.controller.unavailable(); };
    intents.isoOutput = [&] { return f.controller.output(); };
    intents.cancelIso = [&] { ++cancels; };
    intents.showIsoInFolder = [&](const std::filesystem::path& p) { shown.push_back(p); };
    auto build = [&] {
        auto page = std::make_unique<ApplyPage>(f.state, apply, catalog, tr, Language::Turkish, intents);
        page->setBounds({0, 0, 1100, 700});
        page->layout();
        return page;
    };
    CHECK(build()->mode() == ApplyPage::Mode::Summary);
    const int summaryKey = ApplyPage::modeKey(f.state);
    f.startRun();
    CHECK(build()->mode() == ApplyPage::Mode::Running);
    f.endRun([](AppState::ApplyRun&) {});
    CHECK(build()->mode() == ApplyPage::Mode::Done);
    const int runningKey = ApplyPage::modeKey(f.state);
    CHECK(runningKey != summaryKey);
    f.state.isoRunMutable()->running = false;
    f.state.isoRunMutable()->result = core::IsoResult{6ull << 30, {}};
    f.state.notifyIso();
    CHECK(ApplyPage::modeKey(f.state) != runningKey); // the page is built again for the ISO's end
    CHECK(build()->mode() == ApplyPage::Mode::Done);

    // The overview: the ISO is the next thing to look at.
    OverviewInputs inputs;
    auto model = buildOverview(f.state, inputs, ui::nowMs());
    REQUIRE(model.run);
    CHECK(model.run->iso == RunSummary::Iso::Done);
    CHECK(model.next == OverviewModel::Next::IsoReady);
    f.state.applyRunMutable()->autoIso->state = IsoState::Running;
    model = buildOverview(f.state, inputs, ui::nowMs());
    CHECK(model.next == OverviewModel::Next::WatchRun); // still the run's work
    f.state.applyRunMutable()->autoIso->state = IsoState::NotStarted;
    f.state.applyRunMutable()->autoIso->skip = Skip::Blocked;
    f.state.applyRunMutable()->autoIso->blocker = static_cast<int>(IsoController::Blocker::UnattendInvalid);
    model = buildOverview(f.state, inputs, ui::nowMs());
    CHECK(model.next == OverviewModel::Next::BuildIso); // saved, no ISO: ISO Oluştur as before
    CHECK(build()->mode() == ApplyPage::Mode::Done);
}

// ---- D-144 addendum: the ISO options, one set for the ISO page, the Apply row's dialog and the auto ISO ----

TEST_CASE("D-144 addendum: options chosen on the Apply page reach the ISO the run builds") {
    const Localization tr = strings(L"tr");
    Fixture f;
    ui::Widget host; // the dialog's panel, without a window
    IsoOptionControls controls(host, f.state, tr, {});
    controls.sync(false);
    CHECK(controls.label->text() == L"CCCOMA_X64FRE_TR-TR_DV9");
    CHECK(controls.repack->enabled()); // an ISO source is extracted into the work folder: it can be repacked
    controls.label->onChange(L"WINLOVE_TR");
    controls.boot->onChange(1);   // UEFI only
    controls.repack->onChange(3); // ESD
    controls.noPrompt->onChange(true);
    controls.sha->onChange(false);
    controls.secureBoot->onChange(true);
    controls.legacySetup->onChange(true);
    controls.mediaUpdate->onChange(false);

    f.startRun();
    f.endRun([](AppState::ApplyRun&) {});
    REQUIRE(f.started.size() == 1);
    const auto& r = f.started[0];
    CHECK(r.label == L"WINLOVE_TR");
    CHECK(r.boot == core::BootMode::UefiOnly);
    CHECK(r.repack == IsoController::Repack::Esd);
    CHECK(r.noPrompt);
    CHECK_FALSE(r.sha256);
    CHECK(r.secureBoot2023);
    CHECK(r.legacySetup);
    CHECK_FALSE(r.legacySetupAuto); // the user's own choice, not the WinRE rule
    CHECK_FALSE(r.mediaUpdate);
    CHECK_FALSE(f.state.mediaUpdate().enabled);
    // What stays the auto ISO's own.
    CHECK_FALSE(r.openFolder);
    CHECK(r.afterApply);
    CHECK(r.workCopy == IsoController::Request::WorkCopy::Keep);
    CHECK(r.output == std::filesystem::path(L"D:\\ISO\\x64-9448_WinLove.iso"));

    // Left to the rule ("Önceki kurulumu kullan" untouched): decided on the engine thread as the page does.
    AppState::IsoChoices untouched = f.state.isoChoices();
    untouched.legacySetup.reset();
    f.state.setIsoChoices(untouched);
    const auto rule = IsoController::defaultRequest(f.state, L"D:\\ISO\\a.iso");
    CHECK(rule.legacySetupAuto);
    CHECK_FALSE(rule.legacySetup);
    // The bypass box off.
    controls.bootBypass->onChange(false);
    CHECK_FALSE(IsoController::defaultRequest(f.state, L"D:\\ISO\\a.iso").bootBypass);
}

TEST_CASE("D-144 addendum: the Apply row's summary of the options") {
    const Localization tr = strings(L"tr");
    const Localization en = strings(L"en");
    Fixture f;
    CHECK(isoChoicesSummary(f.state, tr) == L"UEFI + BIOS \u00b7 etiket CCCOMA_X64FRE_TR-TR_DV9 \u00b7 SHA-256");
    CHECK(isoChoicesSummary(f.state, en) == L"UEFI + BIOS \u00b7 label CCCOMA_X64FRE_TR-TR_DV9 \u00b7 SHA-256");
    AppState::IsoChoices c = f.state.isoChoices();
    c.label = L"WINLOVE_TR";
    c.boot = core::BootMode::UefiOnly;
    c.repack = 3;
    c.noPrompt = true;
    c.secureBoot2023 = true;
    c.sha256 = false;
    c.legacySetup = true;
    f.state.setIsoChoices(c);
    CHECK(isoChoicesSummary(f.state, tr) ==
          L"Yaln\u0131zca UEFI \u00b7 etiket WINLOVE_TR \u00b7 ESD \u00b7 tu\u015f istemi yok \u00b7 \u00f6nceki kurulum \u00b7 UEFI CA 2023");
    // The media update is named when its box is on and something is downloaded.
    f.state.setMediaUpdate({L"C:\\u\\windows11.0-kb5129195-x64.msu", {}, true});
    CHECK(isoChoicesSummary(f.state, tr).ends_with(L"\u00b7 ortam g\u00fcncellemesi \u00b7 UEFI CA 2023"));
    // The bypass box off says so only when the answers switch a check off.
    c = f.state.isoChoices();
    c.bootBypass = false;
    f.state.setIsoChoices(c);
    CHECK(isoChoicesSummary(f.state, tr).find(L"boot.wim") == std::wstring::npos);
    AppState::Unattend answers = f.state.unattend();
    answers.options.bypassTpm = true;
    f.state.setUnattend(answers);
    CHECK(isoChoicesSummary(f.state, tr).find(L"atlamalar boot.wim'e yaz\u0131lmaz") != std::wstring::npos);
}

TEST_CASE("D-144 addendum: one set of options - the ISO page and the Apply dialog show each other's changes") {
    const Localization tr = strings(L"tr");
    Fixture f;
    f.state.setMounted(std::nullopt);
    IsoController iso(f.state, IsoController::Events{});
    IsoPage page(f.state, iso, tr, Language::Turkish, IsoPage::Intents{});
    page.setBounds({0, 0, 1100, 900});
    page.layout();
    CHECK(page.request().label == L"CCCOMA_X64FRE_TR-TR_DV9");
    CHECK(page.request().boot == core::BootMode::UefiAndBios);

    // Changed in the Apply page's dialog (its controls on their own host)...
    ui::Widget dialogHost;
    IsoOptionControls dialog(dialogHost, f.state, tr, {});
    dialog.sync(false);
    dialog.boot->onChange(2);   // BIOS only
    dialog.repack->onChange(2); // XPRESS
    dialog.label->onChange(L"FROM_APPLY");
    dialog.noPrompt->onChange(true);
    // ...is what the ISO page shows and builds.
    auto r = page.request();
    CHECK(r.boot == core::BootMode::BiosOnly);
    CHECK(r.repack == IsoController::Repack::Xpress);
    CHECK(r.label == L"FROM_APPLY");
    CHECK(r.noPrompt);
    CHECK(page.options().boot->selected() == 2);

    // Changed on the ISO page...
    page.options().boot->onChange(0);
    page.options().secureBoot->onChange(true);
    page.options().label->onChange(L"FROM_ISO_PAGE");
    // ...is what the dialog shows (it syncs on Change::Iso) and the auto ISO uses.
    dialog.sync(false);
    CHECK(dialog.boot->selected() == 0);
    CHECK(dialog.secureBoot->checked());
    CHECK(dialog.label->text() == L"FROM_ISO_PAGE");
    const auto request = IsoController::defaultRequest(f.state, L"D:\\ISO\\x.iso");
    CHECK(request.boot == core::BootMode::UefiAndBios);
    CHECK(request.secureBoot2023);
    CHECK(request.label == L"FROM_ISO_PAGE");
    CHECK(request.repack == IsoController::Repack::Xpress);
    CHECK(isoChoicesSummary(f.state, tr).starts_with(L"UEFI + BIOS \u00b7 etiket FROM_ISO_PAGE \u00b7 XPRESS"));

    // A page built again (another visit) keeps them; the "ISO seçenekleri…" dialog is built without a window too.
    IsoPage again(f.state, iso, tr, Language::Turkish, IsoPage::Intents{});
    CHECK(again.request().label == L"FROM_ISO_PAGE");
    CHECK(again.request().repack == IsoController::Repack::Xpress);
    bool closed = false;
    auto built = makeIsoOptionsDialog(f.state, tr, {}, [&] { closed = true; });
    REQUIRE(built.dialog);
    CHECK(built.initialFocus != nullptr);
    built.dialog->setBounds({0, 0, 1200, 800});
    built.dialog->layout();
    CHECK_FALSE(closed);
    built.dialog->onCancel(); // Esc
    CHECK(closed);

    // The same source read again after Uygula keeps the label; another source starts from its own.
    f.state.setSource(isoSource());
    CHECK(IsoController::chosenLabel(f.state) == L"FROM_ISO_PAGE");
    auto other = isoSource();
    other.path = L"D:\\Isos\\other.iso";
    other.volumeLabel = L"OTHER";
    f.state.setSource(other);
    CHECK(IsoController::chosenLabel(f.state) == L"OTHER");
    CHECK_FALSE(f.state.isoChoices().legacySetup);
    CHECK(f.state.isoChoices().secureBoot2023); // the rest is how this PC builds ISOs
}

TEST_CASE("D-144 addendum: the Apply summary with the option on, its options changing under it") {
    const Localization tr = strings(L"tr");
    std::ifstream catalogFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json", std::ios::binary);
    std::stringstream catalogText;
    catalogText << catalogFile.rdbuf();
    const ImageSettingsCatalog catalog = ImageSettingsCatalog::parse(catalogText.str()).value();
    Fixture f;
    test::UiThread ui;
    ApplyController apply(f.state, {ui.post(), {}, {}, {}});
    int opened = 0;
    ApplyPage::Intents intents;
    intents.isoUnavailable = [&] { return f.controller.unavailable(); };
    intents.isoOutput = [&] { return f.controller.output(); };
    intents.isoOptions = [&] { ++opened; };
    ApplyPage page(f.state, apply, catalog, tr, Language::Turkish, intents);
    page.setBounds({0, 0, 1100, 700});
    page.layout();
    CHECK(page.mode() == ApplyPage::Mode::Summary);
    // A change of the options (Change::Iso) while the page is up: the row follows, no rebuild.
    AppState::IsoChoices c = f.state.isoChoices();
    c.repack = 3;
    f.state.setIsoChoices(c);
    page.layout();
    CHECK(ApplyPage::modeFor(f.state) == ApplyPage::Mode::Summary);
    CHECK(opened == 0);
}
