// 2026-11-11, three of the owner's reports:
// - Sürümler: a deleted edition's Durum said "Bağlanıyor" — each work says its own word on its rows,
//   and the rows go back when it ends;
// - Özet › Kuyruk: "2 değişiklik" with an empty left — every operation is a chip of its page's tab, so
//   the chips add up to the queue (the welcome's files: Kurulum › Katılımsız);
// - Uygula: stopped, then "Kaydetmeden çöz" — the page still said "Durduruldu" — the save / discard of
//   what a run left mounted is the run's ending: discarding → discarded, saving → saved, failure.
#include "app/pages/ApplyPage.h"
#include "app/pages/apply/RunSummary.h"
#include "app/pages/images/EditionTable.h"
#include "app/pages/overview/OverviewModel.h"
#include "app/pages/overview/QueueChips.h"
#include "core/unattend/Welcome.h"
#include "support/UiThread.h"

#include <doctest.h>

#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using Kind = EngineOperation::Kind;
using Close = RunSummary::Close;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"workstate";
    std::filesystem::create_directories(dir);
    return dir / name;
}

Localization english() {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/en.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return Localization::fromJson(text.str()).value();
}

// The navigation's groups as the Shell lays out the card (workspaceTabs, Uygula left out) and the
// answer file's own.
std::vector<QueueLayout> navLayout() {
    return {
        {Str::TabsGroupStart, {{PageId::Images, Str::TabsImage, {PageId::Source, PageId::Images, PageId::Overview}}}},
        {Str::TabsGroupAdd,
         {{PageId::Updates, Str::NavUpdates, {PageId::Updates, PageId::Languages, PageId::Apps}},
          {PageId::Drivers, Str::NavDrivers, {PageId::Drivers}},
          {PageId::Registry, Str::NavRegistry, {PageId::Registry, PageId::Hosts}},
          {PageId::PostSetup, Str::NavPostsetup, {PageId::PostSetup, PageId::Programs, PageId::Files}}}},
        {Str::TabsGroupRemove, {{PageId::Components, Str::NavComponents, {PageId::Components}}}},
        {Str::TabsGroupConfigure,
         {{PageId::Features, Str::NavFeatures, {PageId::Features}},
          {PageId::Tweaks, Str::TabsTweaks, {PageId::Tweaks, PageId::Services, PageId::StartMenu, PageId::Branding, PageId::Icons}},
          {PageId::Tasks, Str::NavTasks, {PageId::Tasks}}}},
        {Str::OverviewQueueGroupSetup, {{PageId::Unattended, Str::TabsUnattended, {PageId::Unattended}}}},
    };
}

int sum(const std::vector<OverviewInputs::QueueGroup>& groups) {
    int n = 0;
    for (const auto& g : groups) {
        for (const auto& item : g.items) {
            n += item.count;
        }
    }
    return n;
}

core::SourceInfo sampleSource() {
    core::SourceInfo source;
    source.path = L"C:\\isos\\x64-9448.iso";
    source.format = core::ImageFormat::Iso;
    source.installImage = L"sources/install.wim";
    for (int i = 1; i <= 3; ++i) {
        core::ImageInfo image;
        image.index = i;
        image.name = L"Windows 11 #" + std::to_wstring(i);
        image.totalBytes = 24ull << 30;
        source.install.images.push_back(image);
    }
    return source;
}

MountedImage mountOf() {
    return MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 1, L"Windows 11 #1"};
}

// A run stopped after its first step: the image still mounted, nothing saved.
AppState::ApplyRun stoppedRun(const AppState& state) {
    AppState::ApplyRun run;
    run.changes = state.changes();
    run.plan = core::ops::plan(run.changes);
    run.groups = core::ops::groups(run.plan);
    run.stepState.assign(run.plan.steps.size(), 0);
    run.stage = AppState::ApplyRun::Stage::Done;
    run.sizeBefore = 24ull << 30;
    run.mountSerial = state.mountSerial();
    core::ops::ApplyJobResult result;
    result.report.completed = false;
    result.report.results.push_back({run.plan.steps.front(), {}});
    run.stepState.front() = 2;
    run.result = std::move(result);
    run.queueVersionAtEnd = state.changes().version();
    return run;
}

} // namespace

// ---- bug 2: the Durum words ---------------------------------------------------------------------

TEST_CASE("editions: each long work says its own word on the rows it is about") {
    EngineOperation op{Kind::Deleting, L"Windows 11 #2", L"C:\\w\\install.wim", 2};
    CHECK(operationRowWord(op) == Str::ImagesRowDeleting);
    CHECK(operationRows(op) == std::vector<int>{2});
    // Several editions deleted (or exported): each of their rows.
    op.index = 0;
    op.indexes = {2, 3};
    CHECK(operationRows(op) == std::vector<int>{2, 3});
    // The ISO's copy step says the work it is made for — the reported "Bağlanıyor" of a delete.
    EngineOperation copy{Kind::Preparing, L"x64.iso", L"C:\\work", 2};
    copy.purpose = Kind::Deleting;
    CHECK(operationRowWord(copy) == Str::ImagesRowDeleting);
    copy.purpose = Kind::Mounting;
    CHECK(operationRowWord(copy) == Str::ImagesMountingState); // a mount's copy step: "Bağlanıyor", as before
    copy.purpose.reset();
    CHECK(operationRowWord(copy) == Str::ImagesRowPreparing);
    CHECK(operationRowWord({Kind::Exporting, L"", L"", 1}) == Str::ImagesRowExporting);
    CHECK(operationRowWord({Kind::Renaming, L"", L"", 1}) == Str::ImagesRowRenaming);
    CHECK(operationRowWord({Kind::Unmounting, L"", L"", 1}) == Str::ImagesRowUnmounting);
    CHECK(operationRowWord({Kind::Duplicating, L"", L"", 1}) == Str::ImagesRowDuplicating);
    CHECK(operationRowWord({Kind::Moving, L"", L"", 1}) == Str::ImagesRowMoving);
    CHECK(operationRowWord({Kind::Mounting, L"", L"", 1}) == Str::ImagesMountingState);
    // Not about one edition of the list: no word (reading the lists keeps "Bağlı").
    CHECK_FALSE(operationRowWord({Kind::Reading, L"", L"", 1}));
    CHECK_FALSE(operationRowWord({Kind::Adopting, L"", L"", 0}));
    CHECK_FALSE(operationRowWord({Kind::Cleaning, L"", L"", 0}));
    CHECK(operationRows({Kind::Verifying, L"", L"", 0}).empty());
}

TEST_CASE("editions: a delete's copy step is not a mount on Özet either") {
    AppState state{scratch(L"recent-copystep.json"), scratch(L"settings-copystep.json")};
    state.setSource(sampleSource());
    EngineOperation copy{Kind::Preparing, L"x64.iso", L"C:\\work", 2};
    copy.purpose = Kind::Deleting;
    state.beginOperation(copy);
    CHECK(buildOverview(state, {}, 0).stage == OverviewModel::Stage::Closed);
    copy.purpose = Kind::Mounting;
    state.beginOperation(copy);
    CHECK(buildOverview(state, {}, 0).stage == OverviewModel::Stage::Mounting);
    state.endOperation();
}

// ---- bug 3: the Kuyruk card's chips -------------------------------------------------------------

TEST_CASE("queue chips: one operation of every kind — the chips add up to the queue") {
    core::ops::ChangeSet changes;
    for (int k = 0; k <= static_cast<int>(OpKind::CreateTask); ++k) {
        changes.add(Operation{static_cast<OpKind>(k), L"target-" + std::to_wstring(k), k == static_cast<int>(OpKind::AddPackage) ? L"lcu" : L""});
    }
    changes.add(Operation{OpKind::AddPackage, L"lang.cab", L"language"});
    REQUIRE(changes.size() == static_cast<std::size_t>(OpKind::CreateTask) + 2);
    const auto groups = queueGroups(changes, {}, navLayout());
    CHECK(sum(groups) == static_cast<int>(changes.size()));
    // No kind is left to "Diğer" with the navigation's layout.
    for (const auto& g : groups) {
        CHECK(g.caption != Str::OverviewQueueOther);
    }
    // A layout without some tabs: what is in none of them still counts, in "Diğer".
    const auto partial = queueGroups(changes, {}, {navLayout()[2]});
    CHECK(sum(partial) == static_cast<int>(changes.size()));
    REQUIRE(partial.back().caption == Str::OverviewQueueOther);
}

TEST_CASE("queue chips: the reported two changes — the welcome's files are Kurulum › Katılımsız") {
    core::ops::ChangeSet changes;
    for (const auto& [kind, target] : core::welcomeSlots()) {
        changes.add(Operation{kind, target, L"…"});
    }
    REQUIRE(changes.size() == 2);
    // Unclaimed, files are the settings' (Ayarlar); claimed by the answer file, its own.
    CHECK(queuePageOf(changes.operations().front(), {}) == PageId::Tweaks);
    QueueClaims claims;
    claims.unattended = core::welcomeSlots();
    CHECK(queuePageOf(changes.operations().front(), claims) == PageId::Unattended);
    const auto groups = queueGroups(changes, claims, navLayout());
    CHECK(sum(groups) == 2);
    CHECK(groups.back().caption == Str::OverviewQueueGroupSetup);
    CHECK(groups.back().items.front().count == 2);

    // Through the overview's model: a chip on the left, the counts the card's header says.
    AppState state{scratch(L"recent-chips.json"), scratch(L"settings-chips.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    for (const auto& op : changes.operations()) {
        state.queue(op);
    }
    OverviewInputs inputs;
    inputs.queue = groups;
    const auto m = buildOverview(state, inputs, 0);
    CHECK(m.queue.changes == 2);
    REQUIRE(m.queue.groups.size() == 1);
    CHECK(m.queue.groups.front().caption == Str::OverviewQueueGroupSetup);
    CHECK(m.queue.sizeDelta == 0); // "Boyutu değişmez": no gain on the bar either (paintSizeBar)
}

TEST_CASE("queue chips: pages that share a kind — the registry page's own values, a component's companion") {
    const Operation typed{OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Contoso::Agent", L"dword:00000001"};
    const Operation setting{OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\X::Y", L"dword:00000000"};
    QueueClaims claims;
    claims.registry = {{OpKind::SetRegistryValue, L"hklm\\software\\contoso::agent"}}; // any case
    CHECK(queuePageOf(typed, claims) == PageId::Registry);
    CHECK(queuePageOf(setting, claims) == PageId::Tweaks);
    claims.components = {{OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\X::Y"}};
    CHECK(queuePageOf(setting, claims) == PageId::Components);
    CHECK(queuePageOf({OpKind::SetEdition, L"edition", L"Professional"}, {}) == PageId::Images);
    CHECK(queuePageOf({OpKind::AddPackage, L"x.cab", L"language"}, {}) == PageId::Languages);
    CHECK(queuePageOf({OpKind::AddPackage, L"x.msu", L"lcu"}, {}) == PageId::Updates);
}

// ---- bug 4: after a stopped run, the save / discard of what it left -----------------------------

TEST_CASE("run close: stopped → discarding → discarded; the Apply page follows") {
    AppState state{scratch(L"recent-close1.json"), scratch(L"settings-close1.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    state.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
    state.setApplyRun(stoppedRun(state));
    CHECK(state.runLeftImageMounted());
    CHECK(summarizeRun(*state.applyRun(), 0).outcome == RunSummary::Outcome::Stopped);
    CHECK(summarizeRun(*state.applyRun(), 0).waiting());
    const int stoppedKey = ApplyPage::modeKey(state);

    state.beginRunClose(/*commit=*/false);
    RunSummary s = summarizeRun(*state.applyRun(), 0);
    CHECK(s.close == Close::Discarding);
    CHECK(s.closing());
    CHECK_FALSE(s.waiting());
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Done);
    CHECK(ApplyPage::modeKey(state) != stoppedKey); // the Shell builds the page again
    CHECK(buildOverview(state, {}, 0).next == OverviewModel::Next::Wait);

    state.endRunClose(std::nullopt);
    state.setMounted(std::nullopt);
    s = summarizeRun(*state.applyRun(), 0);
    CHECK(s.close == Close::Discarded);
    CHECK(s.discarded());
    CHECK_FALSE(s.saved());
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Done);
    OverviewInputs inputs;
    inputs.canMount = true;
    const auto m = buildOverview(state, inputs, 0);
    REQUIRE(m.run);
    CHECK(m.run->close == Close::Discarded);
    CHECK(m.next == OverviewModel::Next::Mount); // nothing was saved: mount again
}

TEST_CASE("run close: stopped → saving → saved — the ISO next; a failure keeps the reason; a cancel forgets it") {
    AppState state{scratch(L"recent-close2.json"), scratch(L"settings-close2.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    state.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
    state.setApplyRun(stoppedRun(state));

    state.beginRunClose(/*commit=*/true);
    CHECK(summarizeRun(*state.applyRun(), 0).close == Close::Saving);
    state.endRunClose(std::nullopt);
    state.setMounted(std::nullopt);
    const RunSummary saved = summarizeRun(*state.applyRun(), 0);
    CHECK(saved.close == Close::Saved);
    CHECK(saved.saved());
    OverviewInputs inputs;
    inputs.canMount = true;
    CHECK(buildOverview(state, inputs, 0).next == OverviewModel::Next::BuildIso);

    // Failure: still mounted, the reason kept, the run waits for the user's word again.
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"}); // a new mount: a new queue
    state.setApplyRun(stoppedRun(state));
    state.beginRunClose(/*commit=*/false);
    state.endRunClose(Error{ErrorCode::DismFailure, L"in use", L"unmount", static_cast<std::int32_t>(0xC1420117)});
    const RunSummary failed = summarizeRun(*state.applyRun(), 0);
    CHECK(failed.close == Close::DiscardFailed);
    CHECK(failed.waiting());
    REQUIRE(state.applyRun()->closing->error);
    CHECK(state.applyRun()->closing->error->message == L"in use");
    // A second try, cancelled: back to the run as it ended.
    state.beginRunClose(/*commit=*/true);
    state.endRunClose(Error{ErrorCode::Cancelled, L"cancelled", L""}, /*cancelled=*/true);
    CHECK_FALSE(state.applyRun()->closing);
    CHECK(summarizeRun(*state.applyRun(), 0).outcome == RunSummary::Outcome::Stopped);
}

TEST_CASE("run close: only the image the run left — not a saved run's, not a later mount's") {
    AppState state{scratch(L"recent-close3.json"), scratch(L"settings-close3.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    auto run = stoppedRun(state);
    run.result->committed = true; // it saved: it unmounted its image
    state.setApplyRun(run);
    CHECK_FALSE(state.runLeftImageMounted());
    state.beginRunClose(false);
    CHECK_FALSE(state.applyRun()->closing);

    state.setApplyRun(stoppedRun(state));
    CHECK(state.runLeftImageMounted());
    state.setMounted(std::nullopt);
    state.setMounted(mountOf()); // mounted again since: another image's work
    CHECK_FALSE(state.runLeftImageMounted());
    state.beginRunClose(true);
    CHECK_FALSE(state.applyRun()->closing);
}

TEST_CASE("run close: the Apply page in each step, built without a window") {
    const Localization strings = english();
    std::ifstream catalogFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json", std::ios::binary);
    std::stringstream catalogText;
    catalogText << catalogFile.rdbuf();
    const ImageSettingsCatalog catalog = ImageSettingsCatalog::parse(catalogText.str()).value();
    AppState state{scratch(L"recent-close4.json"), scratch(L"settings-close4.json")};
    test::UiThread ui;
    ApplyController controller(state, {ui.post(), {}, {}, {}});
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    state.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
    state.setApplyRun(stoppedRun(state));
    auto header = [&] {
        ApplyPage page(state, controller, catalog, strings, Language::English, ApplyPage::Intents{});
        page.setBounds({0, 0, 900, 480});
        CHECK(page.mode() == ApplyPage::Mode::Done);
        return page.header().first;
    };
    CHECK(header() == strings.get(Str::ApplyStoppedTitle));
    state.beginRunClose(false);
    EngineOperation op{Kind::Unmounting, L"Windows 11 #1", L"C:\\WinLove\\mount", 1};
    state.beginOperation(op);
    state.updateOperation(0.4);
    CHECK(header() == strings.get(Str::ApplyCloseDiscardingTitle));
    state.endOperation();
    state.endRunClose(std::nullopt);
    state.setMounted(std::nullopt);
    CHECK(header() == strings.get(Str::ApplyCloseDiscardedTitle));
}
