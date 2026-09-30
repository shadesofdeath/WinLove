#include "app/controllers/PreloadController.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"
#include "ui/anim/Tween.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace wl::app {

namespace {

// Share of the bar per stage: feature details are one DISM call each, app sizes walk
// WindowsApps, services are a single hive read.
constexpr std::array<double, PreloadController::kStages> kWeight{0.6, 0.3, 0.1};

using Publish = std::function<void(std::function<void(AppState&)>)>;

// Same rule as the controllers' load(): a list that is ready or already loading stays.
template <class List>
bool missing(const std::optional<List>& list, const std::filesystem::path& mountDir) {
    return !list || list->mountDir != mountDir || list->status == List::Status::Failed;
}

template <class List>
bool loadingFor(const std::optional<List>& list, const std::filesystem::path& mountDir) {
    return list && list->mountDir == mountDir && list->status == List::Status::Loading;
}

// Hands a finished read to the UI thread as Ready / Failed. A cancelled read publishes nothing:
// the list stays "loading" until the job ends and is dropped there.
template <class List, class Items>
void deliver(const Publish& publish, const std::filesystem::path& mountDir, Result<Items> result,
             void (AppState::*set)(std::optional<List>)) {
    List list{List::Status::Ready, mountDir, {}, {}};
    if (result) {
        list.items = std::move(*result);
    } else if (result.error().code == ErrorCode::Cancelled) {
        return;
    } else {
        log::error("app", describe(result.error()));
        list.status = List::Status::Failed;
        list.error = result.error();
    }
    publish([set, list = std::move(list)](AppState& state) mutable { (state.*set)(std::move(list)); });
}

} // namespace

PreloadController::Readers PreloadController::engineReaders() {
    return Readers{
        [](const std::filesystem::path& mountDir,
           const core::TaskContext& task) -> Result<std::vector<core::OptionalFeature>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::readOptionalFeatures(**dism, mountDir, task);
        },
        [](const std::filesystem::path& mountDir,
           const core::TaskContext& task) -> Result<std::vector<core::AppxComponent>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::readAppx(**dism, mountDir, task);
        },
        [](const std::filesystem::path& mountDir, const core::TaskContext&) { return core::readServices(mountDir); },
    };
}

PreloadController::PreloadController(AppState& state, std::function<void(std::function<void()>)> postToUi,
                                     Readers readers)
    : m_state(state), m_post(std::move(postToUi)), m_readers(std::move(readers)) {}

PreloadController::~PreloadController() {
    *m_alive = false;
}

void PreloadController::start() {
    const auto& mounted = m_state.mounted();
    if (!mounted || m_state.operation()) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    const std::array<bool, kStages> need{missing(m_state.optionalFeatures(), mountDir),
                                         missing(m_state.appxList(), mountDir),
                                         missing(m_state.serviceList(), mountDir)};
    const auto first = std::ranges::find(need, true);
    if (first == need.end()) {
        return;
    }
    EngineOperation op{EngineOperation::Kind::Reading, mounted->edition, mountDir, mounted->index};
    op.startedMs = ui::nowMs();
    op.stage = static_cast<int>(first - need.begin());
    const core::CancelToken cancel = op.cancel;
    m_state.beginOperation(std::move(op));
    // Pages opened meanwhile show "loading"; their own load() sees it and stays out of the way.
    if (need[0]) {
        m_state.setOptionalFeatures(
            AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Loading, mountDir, {}, {}});
    }
    if (need[1]) {
        m_state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Loading, mountDir, {}, {}});
    }
    if (need[2]) {
        m_state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Loading, mountDir, {}, {}});
    }

    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    // Progress: engine thread → UI thread, only when the stage or the whole percent changes.
    auto lastStep = std::make_shared<std::atomic<int>>(-1);
    auto report = [this, post, alive, lastStep](int stage, double fraction) {
        const int step = stage * 1000 + static_cast<int>(std::floor(fraction * 100));
        if (lastStep->exchange(step) == step) {
            return;
        }
        post([this, alive, stage, fraction] {
            if (const auto a = alive.lock(); a && *a) {
                m_state.updateOperation(fraction, stage);
            }
        });
    };
    const Publish publish = [this, post, alive, mountDir](std::function<void(AppState&)> set) {
        post([this, alive, mountDir, set = std::move(set)] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            const auto& mounted = m_state.mounted();
            if (mounted && mounted->mountDir == mountDir) { // not unmounted meanwhile
                set(m_state);
            }
        });
    };
    m_state.engine().run<bool>(
        [readers = m_readers, mountDir, need, cancel, report, publish](const core::TaskContext&) -> Result<bool> {
            double total = 0;
            for (int stage = 0; stage < kStages; ++stage) {
                total += need[static_cast<std::size_t>(stage)] ? kWeight[static_cast<std::size_t>(stage)] : 0;
            }
            double base = 0;
            for (int stage = 0; stage < kStages; ++stage) {
                if (!need[static_cast<std::size_t>(stage)]) {
                    continue;
                }
                if (auto r = cancel.check(mountDir.wstring()); !r) {
                    return std::unexpected(r.error());
                }
                const double share = kWeight[static_cast<std::size_t>(stage)] / total;
                report(stage, base);
                const core::TaskContext task{cancel, [&](double fraction, std::wstring_view) {
                                                 report(stage, base + share * std::clamp(fraction, 0.0, 1.0));
                                             }};
                switch (stage) {
                case 0: deliver(publish, mountDir, readers.features(mountDir, task), &AppState::setOptionalFeatures); break;
                case 1: deliver(publish, mountDir, readers.apps(mountDir, task), &AppState::setAppxList); break;
                default: deliver(publish, mountDir, readers.services(mountDir, task), &AppState::setServiceList); break;
                }
                base += share;
            }
            return true;
        },
        [this, post, alive, mountDir, need](Result<bool> result) {
            post([this, alive, mountDir, need, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const bool broken = !result && result.error().code != ErrorCode::Cancelled;
                if (broken) {
                    log::error("app", describe(result.error()));
                }
                m_state.endOperation();
                // Stopped early: what is still "loading" will never arrive. Drop it, so the page
                // reads its own list on entry.
                bool dropped = false;
                if (need[0] && loadingFor(m_state.optionalFeatures(), mountDir)) {
                    m_state.setOptionalFeatures(std::nullopt);
                    dropped = true;
                }
                if (need[1] && loadingFor(m_state.appxList(), mountDir)) {
                    m_state.setAppxList(std::nullopt);
                    dropped = true;
                }
                if (need[2] && loadingFor(m_state.serviceList(), mountDir)) {
                    m_state.setServiceList(std::nullopt);
                    dropped = true;
                }
                if (dropped && !broken && onCancelled) {
                    onCancelled();
                }
            });
        },
        {});
}

} // namespace wl::app
