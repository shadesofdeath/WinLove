#pragma once
// D-154 Grup İlkesi (docs/pages/23-policies.md): reads the mounted image's Administrative Templates once
// per mount (engine thread: Windows\PolicyDefinitions with the image's language) and turns a policy's
// state into one SetPolicy operation per policy and scope (core/policy/PolicyWrites.h). "Not configured"
// takes the operation out: the image has no policy set by itself that WinLove would know of.
#include "app/state/AppState.h"
#include "core/policy/PolicyWrites.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace wl::app {

class PolicyController {
public:
    PolicyController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~PolicyController();

    void load(bool force = false);

    // A policy in one scope: a "Both" policy is two rows (Bilgisayar, Kullanıcı), as in gpedit.
    struct Row {
        std::size_t policy = 0; // index into the catalog's policies()
        bool user = false;
    };
    // The catalog's rows (empty while it is read); rebuilt when the catalog changes.
    [[nodiscard]] const std::vector<Row>& rows() const;
    [[nodiscard]] const core::policy::PolicyCatalog* catalog() const;
    [[nodiscard]] const core::policy::Policy* policyOf(const Row& row) const;

    // What Uygula will set (the queue's), Not configured when nothing is queued.
    [[nodiscard]] core::policy::PolicyChoice choice(const Row& row) const;
    [[nodiscard]] bool changed(const Row& row) const;
    // Queues the state. Enabled: `values` (nullopt: the queued ones, else the presentation's
    // defaults); an error (out of range, a required value missing) queues nothing.
    Result<void> set(const Row& row, core::policy::PolicyState state, std::optional<core::policy::PolicyValues> values = {});
    void resetChanges();
    [[nodiscard]] int changedCount() const;

    // Pure mapping, unit-tested.
    [[nodiscard]] static Result<core::ops::Operation> operationFor(const core::policy::Policy& policy, bool user,
                                                                   const core::policy::PolicyChoice& choice);

private:
    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    mutable const core::policy::PolicyCatalog* m_rowsOf = nullptr;
    mutable std::vector<Row> m_rows;
};

} // namespace wl::app
