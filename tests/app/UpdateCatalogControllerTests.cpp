// D-046: "Güncellemeleri bul" — search, download, stop — with the network faked.
#include "app/controllers/UpdateCatalogController.h"
#include "app/pages/UpdatesPage.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"update-catalog";
    std::filesystem::create_directories(dir);
    return dir / name;
}

core::SourceInfo source(int build, int revision) {
    core::SourceInfo info;
    info.path = L"C:\\x.iso";
    core::ImageInfo image;
    image.index = 1;
    image.name = L"Windows 11 Pro";
    image.build = build;
    image.spBuild = revision;
    image.architecture = core::Architecture::X64;
    info.install.images.push_back(image);
    return info;
}

core::CatalogEntry entry(const wchar_t* kb, std::uint64_t size) {
    core::CatalogEntry e;
    e.id = L"5cc91450-4f0f-40a8-a67a-63dc58d9dac9";
    e.kb = kb;
    e.size = size;
    e.kind = core::CatalogKind::Cumulative;
    return e;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::vector<std::function<void()>> posted;
    std::vector<core::CatalogOffer> offered;
    std::optional<core::CatalogTarget> searchedFor;
    std::vector<core::DownloadedUpdate> downloaded;
    std::optional<std::pair<Error, bool>> failure;
    bool stopped = false;
    core::CancelToken release; // the fake download waits for this (so a test can stop it)
    bool waitInDownload = false;

    UpdateCatalogController::Backend backend() {
        return {[this](const core::CatalogTarget& t, const core::CancelToken&) -> Result<std::vector<core::CatalogOffer>> {
                    searchedFor = t;
                    return std::vector<core::CatalogOffer>{{entry(L"KB5129195", 1000), true, false}};
                },
                [this](const core::CatalogEntry& e, const std::filesystem::path& folder,
                       const core::TaskContext& task) -> Result<core::DownloadedUpdate> {
                    task.report(0.5, L"download");
                    while (waitInDownload && !task.cancel.cancelled()) {
                        Sleep(1);
                    }
                    if (task.cancel.cancelled()) {
                        return fail(ErrorCode::Cancelled, L"stopped");
                    }
                    return core::DownloadedUpdate{folder / (L"windows11.0-" + e.kb + L"-x64.msu"), {}, e.size};
                }};
    }
    UpdateCatalogController controller{
        state,
        UpdateCatalogController::Events{
            [this](std::function<void()> f) { posted.push_back(std::move(f)); },
            [this](const core::CatalogTarget&, std::vector<core::CatalogOffer> o) { offered = std::move(o); },
            [this](const Error& e, bool download) { failure = std::make_pair(e, download); },
            [this](std::vector<core::DownloadedUpdate> d) { downloaded = std::move(d); },
            [this] { stopped = true; },
        },
        backend()};

    void run() {
        controller.drain();
        auto jobs = std::move(posted);
        posted.clear();
        for (auto& job : jobs) {
            job();
        }
    }
};

} // namespace

TEST_CASE("update catalog controller: the mounted image names the target; search needs a mount") {
    Fixture f;
    CHECK_FALSE(UpdateCatalogController::targetFor(f.state));
    f.controller.search();
    CHECK_FALSE(f.state.updateFetch());

    f.state.setSource(source(26200, 8037));
    f.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    const auto target = UpdateCatalogController::targetFor(f.state);
    REQUIRE(target);
    CHECK(target->release == L"25H2");
    CHECK(target->revision == 8037);

    f.controller.search();
    REQUIRE(f.state.updateFetch());
    CHECK(f.state.updateFetch()->stage == AppState::UpdateFetch::Stage::Searching);
    f.run();
    CHECK_FALSE(f.state.updateFetch());
    REQUIRE(f.offered.size() == 1);
    CHECK(f.searchedFor->build == 26200);
}

TEST_CASE("update catalog controller: download reports progress, then hands over the files") {
    Fixture f;
    f.state.setSource(source(26200, 8037));
    f.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    f.controller.download({entry(L"KB5129195", 1000), entry(L"KB5126052", 200)});
    REQUIRE(f.state.updateFetch());
    CHECK(f.state.updateFetch()->totalBytes == 1200);
    f.controller.search(); // busy: ignored
    f.controller.drain();
    // The progress posts arrive before the result; the last one says the second update, half done.
    bool sawSecond = false;
    auto jobs = std::move(f.posted);
    f.posted.clear();
    for (std::size_t i = 0; i + 1 < jobs.size(); ++i) {
        jobs[i]();
        if (f.state.updateFetch() && f.state.updateFetch()->kb == L"KB5126052") {
            sawSecond = true;
            CHECK(f.state.updateFetch()->doneBytes == 1100);
        }
    }
    CHECK(sawSecond);
    jobs.back()();
    CHECK_FALSE(f.state.updateFetch());
    REQUIRE(f.downloaded.size() == 2);
    CHECK(f.downloaded[0].main.filename() == L"windows11.0-KB5129195-x64.msu");
    CHECK(f.downloaded[0].main.parent_path() == f.state.settings().workRoot / L"updates");
    CHECK_FALSE(f.failure);
}

TEST_CASE("update catalog controller: stop ends the download as 'stopped', not as a failure") {
    Fixture f;
    f.waitInDownload = true;
    f.controller.download({entry(L"KB5129195", 1000)});
    REQUIRE(f.state.updateFetch());
    f.controller.cancel();
    f.run();
    CHECK(f.stopped);
    CHECK_FALSE(f.failure);
    CHECK(f.downloaded.empty());
    CHECK_FALSE(f.state.updateFetch());
}
