#pragma once
// P07 logic (docs/pages/07-components.md): reads the mounted image's provisioned apps once per
// mount, groups them with the AppX catalog (friendly names, risk, notes) and turns checkbox
// clicks into RemoveAppx operations. A group checkbox queues / unqueues all of its apps.
#include "app/catalog/AppxCatalog.h"
#include "app/state/AppState.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class ComponentController {
public:
    struct Item {
        int source = 0;                          // index into AppState::appxList()->items
        std::wstring name;                       // friendly (catalog) or the package identity
        const AppxCatalogEntry* entry = nullptr; // may be null (unknown app)
        core::ops::Risk risk = core::ops::Risk::Medium;
        std::uint64_t size = 0;
        std::wstring packageName;
        std::wstring identity;
    };
    struct Group {
        int catalogIndex = 0;
        std::wstring name;
        std::vector<Item> items;
        std::uint64_t size = 0;
    };
    enum class Check : std::uint8_t { Off, Partial, On };

    ComponentController(AppState& state, AppxCatalog catalog, Language language,
                        std::function<void(std::function<void()>)> postToUi);
    ~ComponentController();

    void load(bool force = false);
    // Groups for the current list (rebuilt on Change::Components).
    [[nodiscard]] std::vector<Group> groups() const;
    [[nodiscard]] const AppxCatalog& catalog() const noexcept { return m_catalog; }

    [[nodiscard]] bool queued(const Item& item) const;
    [[nodiscard]] Check check(const Group& group) const;
    void toggle(const Item& item);
    void toggleGroup(const Group& group);
    void resetChanges();
    [[nodiscard]] std::size_t queuedCount() const;   // RemoveAppx operations
    [[nodiscard]] std::uint64_t queuedBytes() const;  // their size

    [[nodiscard]] static core::ops::Operation operationFor(const Item& item);

private:
    AppState& m_state;
    AppxCatalog m_catalog;
    Language m_language;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
