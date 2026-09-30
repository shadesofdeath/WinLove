// D-027: after a mount the page lists are read in one engine job, shown as a second progress.
#include "app/controllers/PreloadController.h"

#include <doctest.h>

#include <filesystem>
#include <mutex>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"preload";
    std::filesystem::create_directories(dir);
    return dir / name;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::mutex mutex;
    std::vector<std::function<void()>> posted; // engine thread → "UI thread" (the test)
    std::vector<int> calls;                    // reader order
    bool failApps = false;
    bool cancelInFeatures = false;
    int cancelled = 0;
    std::vector<std::pair<int, double>> progress; // (stage, fraction) seen by the UI

    PreloadController::Readers readers() {
        return {
            [this](const std::filesystem::path&, const core::TaskContext& task)
                -> Result<std::vector<core::OptionalFeature>> {
                calls.push_back(0);
                if (cancelInFeatures) {
                    task.cancel.cancel();
                    return fail(ErrorCode::Cancelled, L"cancelled");
                }
                task.report(0.5);
                task.report(1.0);
                return std::vector<core::OptionalFeature>(2);
            },
            [this](const std::filesystem::path&, const core::TaskContext& task)
                -> Result<std::vector<core::AppxComponent>> {
                calls.push_back(1);
                if (failApps) {
                    return fail(ErrorCode::Unknown, L"no apps today");
                }
                task.report(1.0);
                return std::vector<core::AppxComponent>(3);
            },
            [this](const std::filesystem::path&, const core::TaskContext&) -> Result<std::vector<core::ServiceEntry>> {
                calls.push_back(2);
                return std::vector<core::ServiceEntry>(4);
            },
        };
    }

    PreloadController controller{state,
                                 [this](std::function<void()> fn) {
                                     std::scoped_lock lock(mutex);
                                     posted.push_back(std::move(fn));
                                 },
                                 readers()};

    Fixture() {
        controller.onCancelled = [this] { ++cancelled; };
        state.subscribe([this](AppState::Change change) {
            if (change == AppState::Change::Operation && state.operation()) {
                progress.emplace_back(state.operation()->stage, state.operation()->fraction);
            }
        });
    }

    void mount() { state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"}); }

    // Lets the engine job finish, then runs what it posted, in order.
    void settle() {
        state.engine().drain();
        std::vector<std::function<void()>> run;
        {
            std::scoped_lock lock(mutex);
            run.swap(posted);
        }
        for (auto& fn : run) {
            fn();
        }
    }
};

} // namespace

TEST_CASE("preload reads features, apps and services in order under one Reading operation") {
    Fixture f;
    f.mount();
    f.controller.start();

    // Until the engine answers: a Reading operation, and every list already says "loading".
    REQUIRE(f.state.operation());
    CHECK(f.state.operation()->kind == EngineOperation::Kind::Reading);
    CHECK(f.state.operation()->edition == L"Pro");
    CHECK(f.state.optionalFeatures()->status == AppState::OptionalFeatures::Status::Loading);
    CHECK(f.state.appxList()->status == AppState::AppxList::Status::Loading);
    CHECK(f.state.serviceList()->status == AppState::ServiceList::Status::Loading);

    f.settle();
    CHECK_FALSE(f.state.operation());
    CHECK(f.calls == std::vector<int>{0, 1, 2});
    CHECK(f.state.optionalFeatures()->status == AppState::OptionalFeatures::Status::Ready);
    CHECK(f.state.optionalFeatures()->items.size() == 2);
    CHECK(f.state.appxList()->items.size() == 3);
    CHECK(f.state.serviceList()->items.size() == 4);
    CHECK(f.cancelled == 0);

    // One bar across the three reads: it never goes back, and the stages come in order.
    REQUIRE(f.progress.size() >= 3);
    for (std::size_t i = 1; i < f.progress.size(); ++i) {
        CHECK(f.progress[i].first >= f.progress[i - 1].first);
        CHECK(f.progress[i].second >= f.progress[i - 1].second);
    }
    CHECK(f.progress.back().first == 2);
    CHECK(f.progress.back().second <= 1.0);
}

TEST_CASE("preload: a failed read marks only its own list and the rest is still read") {
    Fixture f;
    f.failApps = true;
    f.mount();
    f.controller.start();
    f.settle();
    CHECK(f.calls == std::vector<int>{0, 1, 2});
    CHECK(f.state.optionalFeatures()->status == AppState::OptionalFeatures::Status::Ready);
    REQUIRE(f.state.appxList());
    CHECK(f.state.appxList()->status == AppState::AppxList::Status::Failed);
    CHECK(f.state.appxList()->error.message == L"no apps today");
    CHECK(f.state.serviceList()->status == AppState::ServiceList::Status::Ready);
    CHECK_FALSE(f.state.operation());
    CHECK(f.cancelled == 0);
}

TEST_CASE("preload: cancelling stops the remaining reads and leaves the lists for the pages") {
    Fixture f;
    f.cancelInFeatures = true;
    f.mount();
    f.controller.start();
    f.settle();
    CHECK(f.calls == std::vector<int>{0});
    CHECK_FALSE(f.state.operation());
    // Not "loading" forever: empty, so each page's own load() runs when it is opened.
    CHECK_FALSE(f.state.optionalFeatures());
    CHECK_FALSE(f.state.appxList());
    CHECK_FALSE(f.state.serviceList());
    CHECK(f.cancelled == 1);
}

TEST_CASE("preload: nothing to do without a mount, and lists that are there are not read again") {
    Fixture f;
    f.controller.start();
    CHECK_FALSE(f.state.operation());

    f.mount();
    f.state.setOptionalFeatures(
        AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Ready, L"C:\\m", {}, {}});
    f.state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Ready, L"C:\\m", {}, {}});
    f.controller.start();
    REQUIRE(f.state.operation());
    CHECK(f.state.operation()->stage == 1); // only the apps are missing
    f.settle();
    CHECK(f.calls == std::vector<int>{1});
    CHECK(f.state.appxList()->status == AppState::AppxList::Status::Ready);

    f.controller.start(); // everything is there now
    CHECK_FALSE(f.state.operation());
    f.settle();
    CHECK(f.calls == std::vector<int>{1});
}

TEST_CASE("preload: results for an image that was unmounted meanwhile are dropped") {
    Fixture f;
    f.mount();
    f.controller.start();
    f.state.engine().drain();
    f.state.setMounted(std::nullopt); // before the posted results reach the UI thread
    f.settle();
    CHECK_FALSE(f.state.operation());
    CHECK_FALSE(f.state.optionalFeatures());
    CHECK_FALSE(f.state.appxList());
    CHECK_FALSE(f.state.serviceList());
}
