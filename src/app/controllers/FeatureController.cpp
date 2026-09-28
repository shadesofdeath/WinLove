#include "app/controllers/FeatureController.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"

namespace wl::app {

using core::OptionalFeature;
using core::ops::OpKind;
using core::ops::Operation;

FeatureController::FeatureController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {}

FeatureController::~FeatureController() {
    *m_alive = false;
}

void FeatureController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.optionalFeatures();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::OptionalFeatures::Status::Failed) {
        return; // ready or already loading for this mount
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Loading, mountDir, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<std::vector<OptionalFeature>>(
        [mountDir](const core::TaskContext& task) -> Result<std::vector<OptionalFeature>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::readOptionalFeatures(**dism, mountDir, task);
        },
        [this, post, alive, mountDir](Result<std::vector<OptionalFeature>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& mounted = m_state.mounted();
                if (!mounted || mounted->mountDir != mountDir) {
                    return; // unmounted meanwhile
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setOptionalFeatures(AppState::OptionalFeatures{
                        AppState::OptionalFeatures::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                m_state.setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Ready,
                                                                       mountDir, std::move(*result), {}});
            });
        },
        {});
}

Operation FeatureController::operationFor(const OptionalFeature& item) {
    Operation op{};
    op.target = item.name;
    if (item.kind == OptionalFeature::Kind::Capability) {
        op.kind = OpKind::RemoveCapability;
        op.risk = core::ops::Risk::Medium;
        op.sizeDelta = -static_cast<std::int64_t>(item.size);
    } else {
        op.kind = item.isOn() ? OpKind::DisableFeature : OpKind::EnableFeature;
        op.risk = core::ops::Risk::Low;
    }
    return op;
}

const Operation* FeatureController::queued(const OptionalFeature& item) const {
    return m_state.changes().find(operationFor(item).kind, item.name);
}

bool FeatureController::targetOn(const OptionalFeature& item) const {
    return queued(item) ? !item.isOn() : item.isOn();
}

bool FeatureController::canToggle(const OptionalFeature& item) const {
    return item.kind == OptionalFeature::Kind::Feature || item.isOn();
}

FeatureController::Status FeatureController::status(const OptionalFeature& item) const {
    if (const auto* op = queued(item)) {
        switch (op->kind) {
        case OpKind::EnableFeature: return Status::WillEnable;
        case OpKind::DisableFeature: return Status::WillDisable;
        default: return Status::WillRemove;
        }
    }
    switch (item.state) {
    case core::ServicingState::Installed:
        return item.kind == OptionalFeature::Kind::Capability ? Status::Installed : Status::Enabled;
    case core::ServicingState::Removed: return Status::Removed;
    case core::ServicingState::InstallPending:
    case core::ServicingState::UninstallPending:
    case core::ServicingState::PartiallyInstalled: return Status::Pending;
    default: return Status::Disabled;
    }
}

void FeatureController::toggle(const OptionalFeature& item) {
    if (!canToggle(item)) {
        return;
    }
    const Operation op = operationFor(item);
    if (!m_state.unqueue(op.kind, op.target)) {
        m_state.queue(op);
    }
}

namespace {
bool isFeatureOp(const Operation& op) {
    return op.kind == OpKind::EnableFeature || op.kind == OpKind::DisableFeature || op.kind == OpKind::RemoveCapability;
}
} // namespace

void FeatureController::resetChanges() {
    m_state.unqueueIf(isFeatureOp);
}

std::size_t FeatureController::queuedCount() const {
    std::size_t n = 0;
    for (const auto& op : m_state.changes().operations()) {
        n += isFeatureOp(op) ? 1 : 0;
    }
    return n;
}

} // namespace wl::app
