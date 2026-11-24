// D-141: a VHD / VHDX source in the app — what the Images controller refuses (with the reason the
// disabled buttons show), the ISO page's blocker, the overview's facts and next step, the folders.
// Nothing here attaches a disk: the source is made up, no mount is started.
#include "app/controllers/ImageController.h"
#include "app/controllers/IsoController.h"
#include "app/pages/overview/OverviewModel.h"
#include "core/image/Source.h"
#include "core/image/VirtualDisk.h"
#include "support/UiThread.h"

#include <doctest.h>

using namespace wl;
using namespace wl::app;
using Next = OverviewModel::Next;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"vhd-source";
    std::filesystem::create_directories(dir);
    return dir / name;
}

constexpr std::uint64_t kGb = 1024ull * 1024 * 1024;

core::SourceInfo vhdSource() {
    core::SourceInfo source;
    source.path = L"D:\\Disks\\Win11-Pro.vhdx";
    source.format = core::ImageFormat::Vhdx;
    source.installImage = L"Win11-Pro.vhdx";
    source.installImageSize = 14 * kGb;
    source.install.header.imageCount = 1;
    core::OfflineWindowsFacts facts;
    facts.productName = L"Windows 10 Enterprise";
    facts.editionId = L"Professional";
    facts.installationType = L"Client";
    facts.build = 26200;
    facts.ubr = 8037;
    facts.processorArchitecture = L"AMD64";
    facts.languages = {L"tr-TR"};
    source.install.images.push_back(core::imageInfoFromFacts(facts, 14 * kGb));
    core::VirtualDiskInfo disk;
    disk.virtualSize = 40 * kGb;
    disk.fileSize = 14 * kGb;
    disk.dynamic = true;
    disk.volumeSize = 40 * kGb;
    disk.volumeFree = 26 * kGb;
    source.disk = disk;
    return source;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    test::UiThread ui{test::UiThread::Mode::Queued}; // engine thread → "UI thread" (the test)
    std::vector<Str> refused;
    ImageController images{state,
                           ImageController::Events{
                               ui.post(),
                               [](ImageController::Failure, const Error&) {},
                               [](Str, std::wstring) {},
                               [this](Str why) { refused.push_back(why); },
                               [](std::wstring) {},
                               {},
                               {},
                           }};
};

} // namespace

TEST_CASE("vhd source: one installed Windows — the WIM tools are refused with the reason, mounting is not") {
    Fixture f;
    f.state.setSource(vhdSource());
    CHECK(f.images.isVirtualDiskSource());
    CHECK_FALSE(f.images.isPackedSource());
    CHECK(f.images.canMount());
    CHECK(f.images.editRefusal() == Str::ImagesVhdNoWim);
    CHECK(f.images.deleteRefusal() == Str::ImagesVhdNoWim);
    CHECK(f.images.verifyRefusal() == Str::ImagesVhdNoWim);
    CHECK(f.images.toolsRefusal() == Str::ImagesVhdNoWim);
    // Export is refused before anything reaches the engine (a VHD has no WIM path to export from).
    f.images.exportIndex(1, scratch(L"out.wim"));
    f.images.exportEditions({1}, scratch(L"out2.wim"));
    REQUIRE(f.refused.size() == 2);
    CHECK(f.refused[0] == Str::ImagesVhdNoWim);
    CHECK(f.refused[1] == Str::ImagesVhdNoWim);
    CHECK_FALSE(f.state.operation().has_value());
    CHECK(f.ui.pending() == 0);

    // A WIM is untouched by any of this.
    core::SourceInfo wim;
    wim.path = L"C:\\lab\\install.wim";
    wim.format = core::ImageFormat::Wim;
    wim.installImage = L"install.wim";
    wim.install.header.compression = core::WimCompression::Lzx;
    wim.install.images.push_back(vhdSource().install.images.front());
    wim.install.images.push_back(vhdSource().install.images.front());
    wim.install.images.back().index = 2;
    f.state.setSource(wim);
    CHECK_FALSE(f.images.isVirtualDiskSource());
    CHECK_FALSE(f.images.editRefusal().has_value());
    CHECK_FALSE(f.images.verifyRefusal().has_value());
}

TEST_CASE("vhd source: ISO Oluştur says a virtual disk holds no setup files") {
    Fixture f;
    test::UiThread ui;
    IsoController iso{f.state, IsoController::Events{ui.post(), {}, {}}};
    f.state.setSource(vhdSource());
    REQUIRE(iso.blocker().has_value());
    CHECK(*iso.blocker() == IsoController::Blocker::VirtualDisk);
}

TEST_CASE("vhd source: the overview shows a disk, not a WIM, and a saved disk is the result") {
    AppState state{scratch(L"recent-o.json"), scratch(L"settings-o.json")};
    state.setSource(vhdSource());
    OverviewInputs inputs;
    inputs.canMount = true;
    auto m = buildOverview(state, inputs, 0);
    CHECK(m.stage == OverviewModel::Stage::Closed);
    CHECK(m.image.virtualDisk);
    CHECK(m.image.container == L"VHDX");
    CHECK(m.image.compression.empty());
    CHECK(m.image.dynamicDisk);
    CHECK(m.image.diskBytes == 40 * kGb);
    CHECK(m.image.name == L"Windows 11 Pro");
    CHECK(m.image.editions == 1);
    CHECK(m.next == Next::Mount);

    // Mounted through its changes disk (the folder is WinLove's VHD folder, not the WIM one).
    state.setMounted(MountedImage{state.settings().vhdMountDirectory(), vhdSource().path, 1, L"Windows 11 Pro"});
    m = buildOverview(state, inputs, 0);
    CHECK(m.stage == OverviewModel::Stage::Mounted);
    CHECK(m.next == Next::PickChanges);

    // A saved run (merged and detached): no setup media from it — mount again for more.
    core::ops::ChangeSet changes;
    changes.add(core::ops::Operation{core::ops::OpKind::SetServiceStart, L"DiagTrack", L"disabled"});
    AppState::ApplyRun run;
    run.changes = changes;
    run.plan = core::ops::plan(changes);
    run.groups = core::ops::groups(run.plan);
    run.stage = AppState::ApplyRun::Stage::Done;
    run.stepState.assign(run.plan.steps.size(), 2);
    run.sizeBefore = 14 * kGb;
    run.sizeAfter = 14 * kGb;
    core::ops::ApplyJobResult result;
    result.report.completed = true;
    for (const auto& step : run.plan.steps) {
        result.report.results.push_back({step, {}});
    }
    result.committed = true;
    run.result = std::move(result);
    run.queueVersionAtEnd = state.changes().version();
    state.setApplyRun(std::move(run));
    state.setMounted(std::nullopt);
    m = buildOverview(state, inputs, 0);
    REQUIRE(m.run);
    CHECK(m.run->saved());
    CHECK(m.next == Next::DiskSaved);
}

TEST_CASE("vhd source: its folders are WinLove's own, apart from the WIM mount folder") {
    AppSettings settings;
    settings.workRoot = L"C:\\WinLove";
    CHECK(settings.vhdWorkDirectory() == std::filesystem::path(L"C:\\WinLove\\vhd"));
    CHECK(settings.vhdMountDirectory() == std::filesystem::path(L"C:\\WinLove\\vhd\\mount"));
    CHECK(settings.vhdMountDirectory() != settings.mountDirectory());
    // The WIM mount folder's repair never sees the VHD's mount point: it is not inside it.
    const std::wstring wim = settings.mountDirectory().wstring() + L"\\";
    CHECK_FALSE(settings.vhdMountDirectory().wstring().starts_with(wim));
    CHECK(core::changesDiskPath(L"D:\\Disks\\Win11-Pro.vhdx", settings.vhdWorkDirectory()) ==
          std::filesystem::path(L"C:\\WinLove\\vhd\\Win11-Pro.winlove-changes.vhdx"));
}
