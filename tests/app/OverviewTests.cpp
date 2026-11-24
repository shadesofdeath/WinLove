// D-138: the image overview's model (Görüntü › Özet) in each state the page shows, the run summary the
// overview and the Apply screen share, the plan rows of the Apply summary, and the two pages built in
// every state without a window (their texts are made where the state changes, not while painting).
#include "app/ApplyReport.h"
#include "app/controllers/ApplyController.h"
#include "app/pages/ApplyPage.h"
#include "app/pages/OverviewPage.h"
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
using core::ops::Phase;
using Model = OverviewModel;
using Next = OverviewModel::Next;
using ListState = OverviewModel::ListState;
using TileKind = OverviewModel::TileKind;
using Outcome = RunSummary::Outcome;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"overview";
    std::filesystem::create_directories(dir);
    return dir / name;
}

constexpr std::uint64_t kGb = 1024ull * 1024 * 1024;
constexpr std::int64_t kMb = 1024ll * 1024;

core::SourceInfo sampleSource() {
    core::SourceInfo source;
    source.path = L"C:\\isos\\x64-9448.iso";
    source.format = core::ImageFormat::Iso;
    source.installImage = L"sources/install.wim";
    source.installImageSize = 6 * kGb;
    source.install.header.compression = core::WimCompression::Lzx;
    core::ImageInfo pro;
    pro.index = 1;
    pro.name = L"Windows 11 Pro";
    pro.editionId = L"Professional";
    pro.architecture = core::Architecture::X64;
    pro.build = 26200;
    pro.spBuild = 9445;
    pro.languages = {L"tr-TR", L"en-US"};
    pro.defaultLanguage = L"tr-TR";
    pro.totalBytes = 24 * kGb;
    source.install.images.push_back(pro);
    return source;
}

MountedImage mountOf(int index = 1) {
    return MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", index, L"Windows 11 Pro"};
}

Model::Tile tile(const Model& m, TileKind kind) {
    return m.tile(kind);
}

core::OptionalFeature feature(const wchar_t* name, core::ServicingState state,
                              core::OptionalFeature::Kind kind = core::OptionalFeature::Kind::Feature) {
    return core::OptionalFeature{kind, name, name, {}, state, 0, false};
}

Localization english() {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/en.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return Localization::fromJson(text.str()).value();
}

// A finished run over `changes`: every step ran, saved unless `held`.
AppState::ApplyRun finishedRun(const core::ops::ChangeSet& changes, bool held, std::uint64_t before, std::uint64_t after) {
    AppState::ApplyRun run;
    run.changes = changes;
    run.plan = core::ops::plan(changes);
    run.groups = core::ops::groups(run.plan);
    run.stage = AppState::ApplyRun::Stage::Done;
    run.stepState.assign(run.plan.steps.size(), 2);
    run.sizeBefore = before;
    run.sizeAfter = after;
    core::ops::ApplyJobResult result;
    result.report.completed = true;
    for (const auto& step : run.plan.steps) {
        result.report.results.push_back({step, {}});
        result.stepTimes.push_back(std::chrono::milliseconds(1000));
    }
    if (held) {
        result.report.results.front().outcome = fail(ErrorCode::DismFailure, L"in use", L"step", static_cast<std::int32_t>(0x80070020));
        run.stepState.front() = 3;
    }
    result.committed = !held;
    result.held = held;
    result.elapsed = std::chrono::milliseconds(125'000);
    run.result = std::move(result);
    return run;
}

} // namespace

// ---- the overview's model --------------------------------------------------------------------

TEST_CASE("overview: nothing open — choose a source, nothing read, nothing queued") {
    AppState state{scratch(L"recent-none.json"), scratch(L"settings-none.json")};
    const Model m = buildOverview(state, {}, 0);
    CHECK(m.stage == Model::Stage::NoSource);
    CHECK(m.next == Next::ChooseSource);
    CHECK_FALSE(m.run);
    CHECK(m.queue.changes == 0);
    for (const auto& t : m.tiles) {
        CHECK(t.state == ListState::NotMounted); // no image: not even the WIM's languages
    }
}

TEST_CASE("overview: a source open, nothing mounted — the WIM's facts, mount next, no list read") {
    AppState state{scratch(L"recent-closed.json"), scratch(L"settings-closed.json")};
    state.setSource(sampleSource());
    OverviewInputs inputs;
    inputs.canMount = true;
    Model m = buildOverview(state, inputs, 0);
    CHECK(m.stage == Model::Stage::Closed);
    CHECK(m.image.name == L"Windows 11 Pro");
    CHECK(m.image.editionId == L"Professional");
    CHECK(m.image.build == 26200);
    CHECK(m.image.spBuild == 9445);
    CHECK(m.image.architecture == L"x64");
    CHECK(m.image.language == L"tr-TR");
    CHECK(m.image.languages == 2);
    CHECK(m.image.bytes == 24 * kGb);
    CHECK(m.image.file == L"install.wim");
    CHECK(m.image.container == L"WIM");
    CHECK(m.image.compression == std::wstring(core::compressionName(core::WimCompression::Lzx)));
    CHECK(m.image.fileBytes == 6 * kGb);
    CHECK(m.image.source == L"x64-9448.iso");
    CHECK(m.image.index == 1);
    CHECK(m.image.editions == 1);
    CHECK(m.next == Next::Mount);
    // The languages come from the WIM's own description; every other list waits for a mount.
    CHECK(tile(m, TileKind::Languages).state == ListState::Ready);
    CHECK(tile(m, TileKind::Languages).value == 2);
    CHECK(tile(m, TileKind::Languages).text == L"tr-TR");
    CHECK(tile(m, TileKind::Components).state == ListState::NotMounted);
    CHECK(tile(m, TileKind::Features).state == ListState::NotMounted);
    CHECK(tile(m, TileKind::Updates).state == ListState::NotMounted);
    CHECK(m.queue.sizeBefore == 0); // nothing can be queued yet

    inputs.canMount = false;
    inputs.packedSource = true;
    CHECK(buildOverview(state, inputs, 0).next == Next::ConvertEsd);
    inputs.packedSource = false;
    CHECK(buildOverview(state, inputs, 0).next == Next::Wait); // busy: nothing to press

    // Downloads that came in with nothing mounted are said (they join the queue at the next mount).
    state.heldDownloads().source = state.source()->path;
    state.heldDownloads().updates = {L"C:\\u\\kb1.msu", L"C:\\u\\kb2.msu"};
    CHECK(buildOverview(state, inputs, 0).queue.heldDownloads == 2);
    state.heldDownloads().source = L"C:\\isos\\other.iso";
    CHECK(buildOverview(state, inputs, 0).queue.heldDownloads == 0);
    state.heldDownloads() = {};

    // Mounting: the stage and its progress, and nothing to press meanwhile.
    EngineOperation op{EngineOperation::Kind::Mounting, L"Windows 11 Pro", L"C:\\WinLove\\mount", 1};
    state.beginOperation(op);
    state.updateOperation(0.38);
    m = buildOverview(state, inputs, 0);
    CHECK(m.stage == Model::Stage::Mounting);
    CHECK(m.image.progress == doctest::Approx(0.38));
    CHECK(m.next == Next::Wait);
    state.endOperation();
}

TEST_CASE("overview: mounted, lists not read — said plainly, the reading one as reading") {
    AppState state{scratch(L"recent-unread.json"), scratch(L"settings-unread.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Loading, L"C:\\WinLove\\mount", {}, {}});
    state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Failed, L"C:\\WinLove\\mount", {}, {}});
    EngineOperation op{EngineOperation::Kind::Reading, L"Windows 11 Pro", L"C:\\WinLove\\mount", 1};
    state.beginOperation(op);
    const Model m = buildOverview(state, {}, 0);
    CHECK(m.stage == Model::Stage::Mounted);
    CHECK(m.image.mountDir == L"C:\\WinLove\\mount");
    CHECK(tile(m, TileKind::Components).state == ListState::Unread);
    CHECK(tile(m, TileKind::Apps).state == ListState::Unread);
    CHECK(tile(m, TileKind::Features).state == ListState::Reading);
    CHECK(tile(m, TileKind::Services).state == ListState::Failed);
    CHECK(tile(m, TileKind::Drivers).state == ListState::Unread);
    CHECK(tile(m, TileKind::Updates).state == ListState::Ready); // the queue's own count
    CHECK(tile(m, TileKind::Updates).value == 0);
    // Reading the lists does not keep the user from picking changes.
    CHECK(m.next == Next::PickChanges);
    state.endOperation();
}

TEST_CASE("overview: lists read — the counts the pages have, the components' largest categories") {
    AppState state{scratch(L"recent-read.json"), scratch(L"settings-read.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    using S = core::ServicingState;
    state.setOptionalFeatures(AppState::OptionalFeatures{
        AppState::OptionalFeatures::Status::Ready,
        L"C:\\WinLove\\mount",
        {feature(L"NetFx3", S::Staged), feature(L"WSL", S::Installed), feature(L"Hyper-V", S::InstallPending),
         feature(L"OpenSSH.Client", S::Installed, core::OptionalFeature::Kind::Capability),
         feature(L"WordPad", S::Removed, core::OptionalFeature::Kind::Capability)},
        {}});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\WinLove\\mount", std::vector<core::AppxComponent>(3), {}});
    core::ServiceEntry on;
    on.start = core::StartType::Auto;
    core::ServiceEntry off;
    off.start = core::StartType::Disabled;
    state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Ready, L"C:\\WinLove\\mount", {on, off, off}, {}});
    core::DriverEntry thirdParty;
    core::DriverEntry inBox;
    inBox.inBox = true;
    state.setImageDrivers(AppState::ImageDrivers{AppState::ImageDrivers::Status::Ready, L"C:\\WinLove\\mount", {thirdParty, inBox}, {}});
    state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Ready, L"C:\\WinLove\\mount",
                                           core::ImageIntl{{}, {L"tr-TR", L"en-US", L"de-DE"}}, {}, {}});
    OverviewInputs inputs;
    inputs.categories = {{L"Apps", 17}, {L"Drivers", 610}, {L"Fonts", 0}, {L"Shell", 22}, {L"Languages", 40}};
    inputs.pageCounts.assign(static_cast<std::size_t>(PageId::Count), 0);
    inputs.pageCounts[static_cast<std::size_t>(PageId::Components)] = 12;
    inputs.pageCounts[static_cast<std::size_t>(PageId::Features)] = 2;
    const Model m = buildOverview(state, inputs, 0);

    const auto components = tile(m, TileKind::Components);
    CHECK(components.state == ListState::Ready);
    CHECK(components.value == 689);
    CHECK(components.total == 4); // categories with something in them
    REQUIRE(components.top.size() == 3);
    CHECK(components.top[0].first == L"Drivers");
    CHECK(components.top[1].first == L"Languages");
    CHECK(components.top[2].first == L"Shell");
    CHECK(components.queued == 12);
    CHECK(tile(m, TileKind::Apps).value == 3);
    const auto features = tile(m, TileKind::Features);
    CHECK(features.value == 2);     // WSL, Hyper-V (pending counts as on)
    CHECK(features.total == 3);     // features, not capabilities
    CHECK(features.secondary == 1); // OpenSSH installed; WordPad removed
    CHECK(features.queued == 2);
    CHECK(tile(m, TileKind::Services).value == 3);
    CHECK(tile(m, TileKind::Services).secondary == 2);
    CHECK(tile(m, TileKind::Drivers).value == 1); // in-box drivers are not counted
    CHECK(tile(m, TileKind::Languages).value == 3); // the image's own list once read
    CHECK(tile(m, TileKind::Components).page == PageId::Components);
    CHECK(tile(m, TileKind::Apps).page == PageId::Components); // removed there
    CHECK(tile(m, TileKind::Languages).page == PageId::Languages);
}

TEST_CASE("overview: a queue — grouped as the navigation, size before → after, risk, the compatibility report") {
    AppState state{scratch(L"recent-queue.json"), scratch(L"settings-queue.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    OverviewInputs inputs;
    inputs.queue = {{Str::TabsGroupAdd, {{PageId::Updates, Str::NavUpdates, 2}, {PageId::Drivers, Str::NavDrivers, 0}}},
                    {Str::TabsGroupRemove, {{PageId::Components, Str::NavComponents, 0}}},
                    {Str::TabsGroupConfigure, {{PageId::Features, Str::NavFeatures, 1}}}};
    inputs.risky = {L"OpenSSH"};
    inputs.impacted = {L"Printing"};
    inputs.guardsOn = 5;
    inputs.estimateSeconds = 300;

    // Nothing queued yet: no risk and no report to speak of.
    Model m = buildOverview(state, inputs, 0);
    CHECK(m.next == Next::PickChanges);
    CHECK(m.queue.changes == 0);
    CHECK(m.queue.risky.empty());
    CHECK(m.queue.impacted.empty());
    CHECK(m.queue.seconds == 0);
    CHECK(m.queue.guardsOn == 5);

    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High, -100 * kMb});
    state.queue(Operation{OpKind::AddPackage, L"C:\\u\\kb.msu", L"lcu", core::ops::Risk::Low, 10 * kMb});
    m = buildOverview(state, inputs, 0);
    CHECK(m.next == Next::ReviewQueue);
    CHECK(m.queue.changes == 2);
    CHECK(m.queue.sizeDelta == -90 * kMb);
    CHECK(m.queue.sizeBefore == 24 * kGb);
    CHECK(m.queue.sizeAfter == 24 * kGb - 90 * static_cast<std::uint64_t>(kMb));
    CHECK(m.queue.seconds == 300);
    CHECK(m.queue.risky == std::vector<std::wstring>{L"OpenSSH"});
    CHECK(m.queue.impacted == std::vector<std::wstring>{L"Printing"});
    // Only what is queued: the empty items and the empty group are left out.
    REQUIRE(m.queue.groups.size() == 2);
    CHECK(m.queue.groups[0].caption == Str::TabsGroupAdd);
    REQUIRE(m.queue.groups[0].items.size() == 1);
    CHECK(m.queue.groups[0].items[0].page == PageId::Updates);
    CHECK(m.queue.groups[1].caption == Str::TabsGroupConfigure);
    CHECK_FALSE(m.queue.locked);

    // A larger image afterwards: an estimate that adds.
    state.unqueue(OpKind::RemoveCapability, L"OpenSSH.Client");
    m = buildOverview(state, inputs, 0);
    CHECK(m.queue.sizeAfter == 24 * kGb + 10 * static_cast<std::uint64_t>(kMb));
    inputs.waitingForDownload = true;
    CHECK(buildOverview(state, inputs, 0).queue.waitingForDownload);
}

TEST_CASE("overview: a run in progress — watch it; the queue is frozen") {
    AppState state{scratch(L"recent-running.json"), scratch(L"settings-running.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client"});
    state.queue(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
    state.queue(Operation{OpKind::SetServiceStart, L"dmwappushservice", L"4"});
    AppState::ApplyRun run;
    run.changes = state.changes();
    run.plan = core::ops::plan(run.changes);
    run.groups = core::ops::groups(run.plan);
    run.stepState = {2, 3, 1, 0};
    run.fraction = 0.5;
    run.startedMs = 1'000'000.0;
    run.sizeBefore = 24 * kGb;
    state.setApplyRun(run);
    const Model m = buildOverview(state, {}, 1'060'000.0);
    REQUIRE(m.run);
    CHECK(m.run->outcome == Outcome::Running);
    CHECK(m.run->ok == 1);
    CHECK(m.run->skipped == 1);
    CHECK(m.run->pending == 2);
    CHECK(m.run->current == 2);
    CHECK(m.run->elapsedSeconds == doctest::Approx(60.0));
    CHECK(m.run->remainingSeconds == doctest::Approx(60.0));
    CHECK(m.next == Next::WatchRun);
    CHECK(m.queue.locked);
}

TEST_CASE("overview: a run done — saved: the ISO next; held: its report until the queue changes") {
    AppState state{scratch(L"recent-done.json"), scratch(L"settings-done.json")};
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client"});

    // Held: a step failed, nothing saved, still mounted.
    auto held = finishedRun(state.changes(), /*held=*/true, 24 * kGb, 0);
    held.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(held);
    Model m = buildOverview(state, {}, 0);
    REQUIRE(m.run);
    CHECK(m.run->outcome == Outcome::Held);
    CHECK(m.run->waiting());
    CHECK(m.next == Next::ResolveRun);
    state.queue(Operation{OpKind::DisableFeature, L"NetFx3"}); // the user moved on
    m = buildOverview(state, {}, 0);
    CHECK_FALSE(m.run);
    CHECK(m.next == Next::ReviewQueue);

    // Saved and unmounted: what it came to, and the setup media next.
    auto saved = finishedRun(state.changes(), /*held=*/false, 24 * kGb, 23 * kGb);
    saved.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(saved);
    state.setMounted(std::nullopt);
    OverviewInputs inputs;
    inputs.canMount = true;
    m = buildOverview(state, inputs, 0);
    CHECK(m.stage == Model::Stage::Closed);
    REQUIRE(m.run);
    CHECK(m.run->outcome == Outcome::Succeeded);
    CHECK(m.run->saved());
    CHECK(m.run->gain == kGb);
    CHECK(m.run->gainPct == 4);
    CHECK(m.next == Next::BuildIso);
    // Mounted again: the old run is not the news any more.
    state.setMounted(mountOf());
    m = buildOverview(state, inputs, 0);
    CHECK_FALSE(m.run);
    CHECK(m.next == Next::PickChanges);
}

// ---- the run summary (the Apply screen's head cards, the overview's run card) -----------------

TEST_CASE("run summary: each way a run ends, in the Apply page's order") {
    core::ops::ChangeSet changes;
    changes.add(Operation{OpKind::EnableFeature, L"WSL"});
    changes.add(Operation{OpKind::RemoveCapability, L"OpenSSH.Client"});
    changes.add(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});

    auto run = finishedRun(changes, false, 20 * kGb, 15 * kGb);
    RunSummary s = summarizeRun(run, 0);
    CHECK(s.outcome == Outcome::Succeeded);
    CHECK(s.total == 3);
    CHECK(s.ok == 3);
    CHECK(s.skipped == 0);
    CHECK(s.pending == 0);
    CHECK(s.elapsedSeconds == doctest::Approx(125.0));
    CHECK(s.remainingSeconds < 0);
    CHECK(s.gain == 5 * kGb);
    CHECK(s.gainPct == 25);
    CHECK(s.saved());

    run.result->report.results[1].outcome = fail(ErrorCode::DismFailure, L"permanent", L"step", static_cast<std::int32_t>(0x800F0825));
    s = summarizeRun(run, 0);
    CHECK(s.outcome == Outcome::Skipped);
    CHECK(s.ok == 2);
    CHECK(s.skipped == 1);
    CHECK(s.saved());

    run.result->held = true;
    run.result->committed = false;
    CHECK(summarizeRun(run, 0).outcome == Outcome::Held);
    run.result->held = false;
    run.result->kept = true;
    CHECK(summarizeRun(run, 0).outcome == Outcome::Kept);
    CHECK(summarizeRun(run, 0).waiting());

    run.result->kept = false;
    run.result->report.completed = false;
    run.result->report.results.pop_back(); // stopped before the last step
    s = summarizeRun(run, 0);
    CHECK(s.outcome == Outcome::Stopped);
    CHECK(s.pending == 1);
    CHECK_FALSE(s.saved());

    run.result->report.completed = true;
    run.result->commitError = Error{ErrorCode::DismFailure, L"commit failed", L"", 0};
    CHECK(summarizeRun(run, 0).outcome == Outcome::CommitFailed);

    AppState::ApplyRun refused;
    refused.stage = AppState::ApplyRun::Stage::Done;
    refused.error = Error{ErrorCode::DismFailure, L"session refused", L"", 0};
    CHECK(summarizeRun(refused, 0).outcome == Outcome::StartFailed);

    // Unknown size after (an unsaved run): no gain claimed.
    auto unsaved = finishedRun(changes, true, 20 * kGb, 0);
    s = summarizeRun(unsaved, 0);
    CHECK(s.gain == 0);
    CHECK(s.gainPct == 0);
}

TEST_CASE("run summary: running and paused — elapsed from the start, a remaining guess only while running") {
    core::ops::ChangeSet changes;
    changes.add(Operation{OpKind::EnableFeature, L"WSL"});
    changes.add(Operation{OpKind::DisableFeature, L"NetFx3"});
    AppState::ApplyRun run;
    run.changes = changes;
    run.plan = core::ops::plan(changes);
    run.stepState = {2, 1};
    run.fraction = 0.02; // too early to guess
    run.startedMs = 10'000.0;
    RunSummary s = summarizeRun(run, 40'000.0);
    CHECK(s.outcome == Outcome::Running);
    CHECK(s.active());
    CHECK(s.current == 1);
    CHECK(s.elapsedSeconds == doctest::Approx(30.0));
    CHECK(s.remainingSeconds < 0);
    run.fraction = 0.75;
    CHECK(summarizeRun(run, 40'000.0).remainingSeconds == doctest::Approx(10.0));
    run.stage = AppState::ApplyRun::Stage::Paused;
    s = summarizeRun(run, 40'000.0);
    CHECK(s.outcome == Outcome::Paused);
    CHECK(s.active());
    CHECK(s.remainingSeconds < 0); // a pause waits for the user
    run.stage = AppState::ApplyRun::Stage::Committing;
    CHECK(summarizeRun(run, 40'000.0).outcome == Outcome::Committing);

    // D-055: further editions — how many there are and how many were saved.
    run.extras = {{2, L"Home", AppState::ApplyRun::ExtraEdition::State::Done},
                  {3, L"Education", AppState::ApplyRun::ExtraEdition::State::Failed}};
    s = summarizeRun(run, 40'000.0);
    CHECK(s.editions == 3);
    CHECK(s.editionsDone == 1);
}

TEST_CASE("plan rows: each group's size effect and high-risk steps; the whole estimate with the save") {
    core::ops::ChangeSet changes;
    changes.add(Operation{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High, -6 * kMb});
    changes.add(Operation{OpKind::RemoveAppx, L"Microsoft.GamingApp", L"Xbox", core::ops::Risk::Medium, -412 * kMb});
    changes.add(Operation{OpKind::EnableFeature, L"WSL"});
    changes.add(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
    const auto plan = core::ops::plan(changes);
    const auto rows = planRows(plan);
    const auto groups = core::ops::groups(plan);
    REQUIRE(rows.size() == groups.size());
    std::int64_t total = 0;
    std::size_t risky = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CHECK(rows[i].group.phase == groups[i].phase);
        CHECK(rows[i].group.count == groups[i].count);
        total += rows[i].sizeDelta;
        risky += rows[i].risky;
    }
    CHECK(total == -418 * kMb);
    CHECK(risky == 1);
    double seconds = core::ops::kCommitSeconds + core::ops::kOptimizeSeconds;
    for (const auto& g : groups) {
        seconds += g.estimateSeconds;
    }
    CHECK(planSeconds(groups) == doctest::Approx(seconds));
    CHECK(planSeconds({}) == doctest::Approx(core::ops::kCommitSeconds + core::ops::kOptimizeSeconds));
}

// ---- the pages, built without a window ------------------------------------------------------

TEST_CASE("overview page: built and laid out in each state; the button follows the next step") {
    const Localization strings = english();
    AppState state{scratch(L"recent-page.json"), scratch(L"settings-page.json")};
    std::vector<Next> acted;
    std::vector<PageId> opened;
    unsigned gathered = 0;
    OverviewPage::Intents intents;
    intents.gather = [&](OverviewInputs& in, unsigned parts) {
        gathered |= parts;
        in.canMount = true;
    };
    intents.open = [&](PageId page) { opened.push_back(page); };
    intents.act = [&](Next next) { acted.push_back(next); };
    OverviewPage page(state, strings, Language::English, intents);
    page.setBounds({0, 0, 900, 480});
    CHECK(gathered == OverviewPage::kAllParts);
    CHECK(page.model().stage == Model::Stage::NoSource);

    state.setSource(sampleSource());
    CHECK(page.model().stage == Model::Stage::Closed);
    CHECK(page.model().next == Next::Mount);
    CHECK(page.contentHeight() > 0);

    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL", L"", core::ops::Risk::Low, 50 * kMb});
    CHECK(page.model().next == Next::ReviewQueue);
    CHECK(page.model().queue.changes == 1);

    auto held = finishedRun(state.changes(), true, 24 * kGb, 0);
    held.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(held);
    CHECK(page.model().next == Next::ResolveRun);
    page.setBounds({0, 0, 560, 480}); // narrow: two tiles a row
    CHECK(page.contentHeight() > 0);
}

TEST_CASE("Apply page: head cards in every mode, built without a window") {
    const Localization strings = english();
    std::ifstream catalogFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json", std::ios::binary);
    std::stringstream catalogText;
    catalogText << catalogFile.rdbuf();
    const ImageSettingsCatalog catalog = ImageSettingsCatalog::parse(catalogText.str()).value();
    AppState state{scratch(L"recent-apply.json"), scratch(L"settings-apply.json")};
    test::UiThread ui;
    ApplyController controller(state, {ui.post(), {}, {}, {}});
    state.setSource(sampleSource());
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High, -6 * kMb});
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    auto build = [&] {
        auto page = std::make_unique<ApplyPage>(state, controller, catalog, strings, Language::English, ApplyPage::Intents{});
        page->setBounds({0, 0, 900, 480});
        return page;
    };
    CHECK(build()->mode() == ApplyPage::Mode::Summary);

    AppState::ApplyRun run;
    run.changes = state.changes();
    run.plan = core::ops::plan(run.changes);
    run.groups = core::ops::groups(run.plan);
    run.stepState = {2, 1};
    run.fraction = 0.4;
    run.startedMs = ui::nowMs() - 30'000.0;
    state.setApplyRun(run);
    CHECK(build()->mode() == ApplyPage::Mode::Running);
    run.stage = AppState::ApplyRun::Stage::Paused;
    state.setApplyRun(run);
    CHECK(build()->mode() == ApplyPage::Mode::Paused);

    // Held: still mounted, the report first. Saved: unmounted, the result.
    auto held = finishedRun(state.changes(), true, 24 * kGb, 0);
    held.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(held);
    CHECK(build()->mode() == ApplyPage::Mode::Done);
    const auto changes = state.changes();
    auto saved = finishedRun(changes, false, 24 * kGb, 23 * kGb);
    state.setApplyRun(saved);
    state.setMounted(std::nullopt);
    CHECK(build()->mode() == ApplyPage::Mode::Done);
    state.setMounted(mountOf());
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    auto failed = finishedRun(state.changes(), false, 24 * kGb, 0);
    failed.result->commitError = Error{ErrorCode::DismFailure, L"commit failed", L"", 0};
    failed.result->committed = false;
    failed.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(failed);
    state.setMounted(std::nullopt);
    CHECK(build()->mode() == ApplyPage::Mode::Done);
}
