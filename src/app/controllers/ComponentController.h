#pragma once
// P07 logic (docs/pages/07-components.md): reads the mounted image's provisioned apps once per
// mount, groups them with the AppX catalog (friendly names, risk, notes) and turns checkbox
// clicks into RemoveAppx operations. A group checkbox queues / unqueues all of its items.
// After the apps come the system components of resources/catalog/components.json that this image
// has (RemoveComponent, the recipe travels in the operation) and the component store cleanup
// (CleanupImage).
#include "app/catalog/AppxCatalog.h"
#include "app/catalog/ComponentCatalog.h"
#include "app/state/AppState.h"
#include "core/ops/Compat.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class ComponentController {
public:
    struct Item {
        enum class Kind : std::uint8_t { Appx, System, Cleanup, Shrink };
        Kind kind = Kind::Appx;
        int source = 0;                          // Appx: index into AppState::appxList()->items
        std::wstring name;                       // friendly (catalog) or the package identity
        const AppxCatalogEntry* entry = nullptr; // Appx: may be null (unknown app)
        const ComponentCatalogEntry* system = nullptr; // System / Cleanup / Shrink
        core::ops::Risk risk = core::ops::Risk::Medium;
        std::uint64_t size = 0;                  // 0 = unknown
        std::wstring packageName;                // the queue target: package full name / catalog id
        std::wstring identity;                   // technical line under the name
        std::wstring notes;                      // catalog note in the UI language (may be empty)
        std::vector<std::wstring> contents;      // what is removed (package full name; packages + paths)
        bool deep = false;                       // deep removal (D-060): its own warning
    };
    struct Group {
        int catalogIndex = 0; // unique per group: AppX catalog groups first, then the system groups
        std::wstring name;
        std::vector<Item> items;
        std::uint64_t size = 0;
    };
    enum class Check : std::uint8_t { Off, Partial, On };

    ComponentController(AppState& state, AppxCatalog catalog, Language language,
                        std::function<void(std::function<void()>)> postToUi, ComponentCatalog systemCatalog = {});
    ~ComponentController();

    void load(bool force = false);

    // Groups for the current list (rebuilt on Change::Components).
    [[nodiscard]] std::vector<Group> groups() const;
    [[nodiscard]] const AppxCatalog& catalog() const noexcept { return m_catalog; }
    [[nodiscard]] const ComponentCatalog& systemCatalog() const noexcept { return m_systemCatalog; }
    // A preset carries the recipes it was saved with; each system component the catalog still has
    // gets its current recipe (D-076: fixes reach old presets — D-075's telemetry, the InboxApps
    // spare packages of the taskbar pins). Unknown ids keep their own recipe. App removals that name
    // another version of an app this image has get this image's package name (audit A4).
    [[nodiscard]] core::ops::ChangeSet withCurrentRecipes(core::ops::ChangeSet changes) const;

    [[nodiscard]] bool queued(const Item& item) const;
    [[nodiscard]] Check check(const Group& group) const;
    void toggle(const Item& item);
    void toggleGroup(const Group& group);
    // D-082: what keeps an item out of the queue (the CompatController); empty for an item that is
    // queued or when nothing does. A group check box passes over held items; a click on one calls
    // `onBlocked` instead of queueing it.
    std::function<core::ops::CompatBlock(const core::ops::Operation&)> blockOf;
    std::function<void(const Item&, const core::ops::CompatBlock&)> onBlocked;
    [[nodiscard]] core::ops::CompatBlock block(const Item& item) const;
    void resetChanges();
    [[nodiscard]] std::size_t queuedCount() const;   // component operations of every kind
    [[nodiscard]] std::uint64_t queuedBytes() const;  // their size

    [[nodiscard]] static core::ops::Operation operationFor(const Item& item);
    [[nodiscard]] static bool isComponentKind(core::ops::OpKind kind) noexcept;
    // The catalog's system components as this image has them (engine thread: walks their paths).
    [[nodiscard]] static AppState::SystemComponents probeSystem(const ComponentCatalog& catalog,
                                                                const std::filesystem::path& mountDir);

private:
    void readSystem(); // once per mount, after the app list is there
    void queueItem(const Item& item); // the shrink brings automatic updates off with it

    AppState& m_state;
    AppxCatalog m_catalog;
    ComponentCatalog m_systemCatalog;
    Language m_language;
    std::function<void(std::function<void()>)> m_post;
    std::size_t m_subscription = 0;
    std::filesystem::path m_probing; // mount whose system components are being read
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
