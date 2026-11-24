#include "app/controllers/PolicyController.h"

#include "base/Log.h"
#include "core/image/Services.h"

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::policy::PolicyChoice;
using core::policy::PolicyState;

PolicyController::PolicyController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {}

PolicyController::~PolicyController() {
    *m_alive = false;
}

void PolicyController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.policyList();
    if (!force && current && current->mountDir == mounted->mountDir && current->status != AppState::PolicyList::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setPolicyList(AppState::PolicyList{AppState::PolicyList::Status::Loading, mountDir, nullptr, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<std::shared_ptr<const core::policy::PolicyCatalog>>(
        [mountDir](const core::TaskContext&) -> Result<std::shared_ptr<const core::policy::PolicyCatalog>> {
            auto catalog = core::policy::readPolicyDefinitions(mountDir / L"Windows" / L"PolicyDefinitions",
                                                               core::imageUiLanguage(mountDir));
            if (!catalog) {
                return std::unexpected(catalog.error());
            }
            return std::make_shared<const core::policy::PolicyCatalog>(std::move(*catalog));
        },
        [this, post, alive, mountDir](Result<std::shared_ptr<const core::policy::PolicyCatalog>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& now = m_state.policyList();
                if (!m_state.mounted() || m_state.mounted()->mountDir != mountDir || !now ||
                    now->status != AppState::PolicyList::Status::Loading || now->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setPolicyList(AppState::PolicyList{AppState::PolicyList::Status::Failed, mountDir, nullptr, result.error()});
                    return;
                }
                m_state.setPolicyList(AppState::PolicyList{AppState::PolicyList::Status::Ready, mountDir, std::move(*result), {}});
            });
        },
        {});
}

const core::policy::PolicyCatalog* PolicyController::catalog() const {
    const auto& list = m_state.policyList();
    return list && list->status == AppState::PolicyList::Status::Ready ? list->catalog.get() : nullptr;
}

const std::vector<PolicyController::Row>& PolicyController::rows() const {
    const auto* now = catalog();
    if (now != m_rowsOf) {
        m_rowsOf = now;
        m_rows.clear();
        if (now) {
            const auto& policies = now->policies();
            for (std::size_t i = 0; i < policies.size(); ++i) {
                const auto cls = policies[i].policyClass;
                if (cls != core::policy::PolicyClass::User) {
                    m_rows.push_back({i, false});
                }
                if (cls != core::policy::PolicyClass::Machine) {
                    m_rows.push_back({i, true});
                }
            }
        }
    }
    return m_rows;
}

const core::policy::Policy* PolicyController::policyOf(const Row& row) const {
    const auto* now = catalog();
    return now && row.policy < now->policies().size() ? &now->policies()[row.policy] : nullptr;
}

PolicyChoice PolicyController::choice(const Row& row) const {
    const auto* policy = policyOf(row);
    if (!policy) {
        return {};
    }
    if (const auto* op = m_state.changes().find(OpKind::SetPolicy, core::policy::policyTarget(policy->id, row.user))) {
        if (auto parsed = core::policy::parsePolicyOperation(op->value)) {
            return parsed->choice;
        }
    }
    return {};
}

bool PolicyController::changed(const Row& row) const {
    const auto* policy = policyOf(row);
    return policy && m_state.changes().find(OpKind::SetPolicy, core::policy::policyTarget(policy->id, row.user)) != nullptr;
}

Result<Operation> PolicyController::operationFor(const core::policy::Policy& policy, bool user, const PolicyChoice& choice) {
    auto writes = core::policy::policyWrites(policy, user, choice);
    if (!writes) {
        return std::unexpected(writes.error());
    }
    core::policy::PolicyOperation op{policy.displayName, choice, std::move(*writes)};
    if (choice.state == PolicyState::Disabled) {
        op.choice.values.clear(); // nothing of them is written
    }
    Operation out{OpKind::SetPolicy, core::policy::policyTarget(policy.id, user), core::policy::policyOperationJson(op)};
    out.risk = core::ops::Risk::Low;
    return out;
}

Result<void> PolicyController::set(const Row& row, PolicyState state, std::optional<core::policy::PolicyValues> values) {
    const auto* policy = policyOf(row);
    if (!policy) {
        return fail(ErrorCode::NotFound, L"the policy list is not read", L"");
    }
    const std::wstring target = core::policy::policyTarget(policy->id, row.user);
    if (state == PolicyState::NotConfigured) {
        m_state.unqueue(OpKind::SetPolicy, target);
        return {};
    }
    PolicyChoice choice{state, {}};
    if (state == PolicyState::Enabled) {
        if (values) {
            choice.values = std::move(*values);
        } else {
            const PolicyChoice queued = this->choice(row);
            choice.values = queued.state == PolicyState::Enabled ? queued.values : core::policy::defaultValues(*policy);
        }
    }
    auto op = operationFor(*policy, row.user, choice);
    if (!op) {
        return std::unexpected(op.error());
    }
    m_state.queue(std::move(*op));
    return {};
}

void PolicyController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::SetPolicy; });
}

int PolicyController::changedCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::SetPolicy));
}

} // namespace wl::app
