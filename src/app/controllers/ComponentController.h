#pragma once
// P07 logic (docs/pages/07-components.md): reads the mounted image's provisioned apps once per
// mount, groups them with the AppX catalog (friendly names, risk, notes) and turns checkbox
// clicks into RemoveAppx operations. A group checkbox queues / unqueues all of its items.
// After the apps come the system components of resources/catalog/components.json that this image
// has (RemoveComponent, the recipe travels in the operation) and the component store cleanup
// (CleanupImage).
#include "app/catalog/AppxCatalog.h"
#include "app/catalog/ComponentCatalog.h"
#include "app/catalog/Tier.h"
#include "app/state/AppState.h"
#include "core/ops/Compat.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class ComponentController {
public:
    struct Item {
        // D-112: Driver · Language · Keyboard · Font · SystemApp — read from the image itself
        // (ImageInventory), removed by the recipe they carry (RemoveComponent).
        enum class Kind : std::uint8_t { Appx, System, Cleanup, Shrink, Driver, Language, Keyboard, Font, SystemApp };
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
        std::string category = "uncategorized";  // D-110: the reference tool's tree category
        Tier tier = Tier::None;                  // D-110: the template that takes it (the arc color)
        std::vector<std::wstring> path;          // D-112: nodes under the category ("Dil", a driver class …)
        std::vector<std::wstring> children;      // D-112: shown under it, not removable alone (a driver's devices)
        std::wstring detail;                     // D-112: a line for the tooltip (DriverVer, the tag, the file)
        std::wstring lockReason;                 // D-112: why it can never be unticked (empty: it can)
        core::ComponentRecipe recipe;            // D-112: the inventory kinds' removal (D-113: System too)
        // D-113 (the reference tool's model): the item it depends on (its packageName) — shown under it, removed
        // with it; keeping it keeps that one too. Empty: it hangs on its category / nodes.
        std::wstring parentKey;
    };
    struct Group {
        int catalogIndex = 0; // D-110: the category's position in components.json "categories"
        std::wstring name;
        std::vector<Item> items;
        std::uint64_t size = 0;
    };
    enum class Check : std::uint8_t { Off, Partial, On };
    // D-110: the reference tool's removal levels (Kaldırma modu). Varsayılan+ keeps update support (no store
    // cleanup), Derin cleans the component store (ResetBase), Maksimum also shrinks WinSxS.
    enum class Level : std::uint8_t { DefaultPlus, Deep, Maximum };

    ComponentController(AppState& state, AppxCatalog catalog, Language language,
                        std::function<void(std::function<void()>)> postToUi, ComponentCatalog systemCatalog = {});
    ~ComponentController();

    void load(bool force = false);

    // Groups for the current list (rebuilt on Change::Components): the reference tool's categories in their
    // order, items by name. The store cleanup and the shrink are not in them: the level is theirs.
    [[nodiscard]] std::vector<Group> groups() const;
    // The store cleanup and the shrink (empty before the list is read).
    [[nodiscard]] std::vector<Item> modeItems() const;
    [[nodiscard]] Level level() const;
    void setLevel(Level level);
    // The reference tool's Şablon: queues what the template takes (privacy ⊂ general ⊂ light), adds to the
    // queue, skips what a guard holds. Returns how many were queued.
    int applyTemplate(Tier chosen);
    [[nodiscard]] const AppxCatalog& catalog() const noexcept { return m_catalog; }
    [[nodiscard]] const ComponentCatalog& systemCatalog() const noexcept { return m_systemCatalog; }
    // A preset carries the recipes it was saved with; each system component the catalog still has
    // gets its current recipe (D-076: fixes reach old presets — D-075's telemetry, the InboxApps
    // spare packages of the taskbar pins). Unknown ids keep their own recipe. App removals that name
    // another version of an app this image has get this image's package name (audit A4).
    [[nodiscard]] core::ops::ChangeSet withCurrentRecipes(core::ops::ChangeSet changes) const;

    [[nodiscard]] bool queued(const Item& item) const;
    [[nodiscard]] Check check(const Group& group) const;
    // A kept item goes with everything that depends on it; a removed one comes back with what it
    // depends on (D-113).
    void toggle(const Item& item);
    // D-113: the items under / above one (by parentKey), from the current list.
    [[nodiscard]] std::vector<Item> dependents(const Item& item) const;
    [[nodiscard]] std::vector<Item> dependencies(const Item& item) const;
    void toggleGroup(const Group& group);
    // D-112: a tree node's items together (one queue edit); locked and held items are left alone.
    void setRemoved(const std::vector<Item>& items, bool removed);
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
    [[nodiscard]] std::vector<Item> allItems() const; // apps, system components, cleanup and shrink
    void readSystem(); // once per mount, after the app list is there
    void readInventory(); // D-112: drivers, languages, keyboards, fonts, system apps; once per mount
    void addInventory(std::vector<Item>& items) const;
    void placeDependents(std::vector<Item>& items) const; // D-113
    std::filesystem::path m_inventoryReading;
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
