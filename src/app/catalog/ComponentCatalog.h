#pragma once
// resources/catalog/components.json: what the Components page offers besides the provisioned
// apps — system components (a core::ComponentRecipe each: CBS packages, paths, registry) and the
// component store cleanup. Entries whose recipe does not pass core::validateComponentRecipe are
// skipped and logged, not fatal.
#include "app/catalog/ImageSettingsCatalog.h" // LocalizedText
#include "app/catalog/Tier.h"
#include "core/image/SystemComponents.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct ComponentCatalogGroup {
    std::string id;
    LocalizedText name;
};

struct ComponentCatalogEntry {
    enum class Kind : std::uint8_t { Remove, Cleanup, Shrink }; // Shrink: WinSxS at its smallest (D-079)
    std::string id;
    std::string group;
    std::string category = "uncategorized"; // D-110: the reference tool's tree category
    Tier tier = Tier::None;
    Kind kind = Kind::Remove;
    LocalizedText name;
    LocalizedText notes;
    core::ops::Risk risk = core::ops::Risk::Medium;
    core::ComponentRecipe recipe; // Remove (title left empty: the queue operation gets the shown name)
    bool resetBase = true;        // Cleanup
    // Remove: offered for every image, present or not — a preventive change (the new Outlook's
    // automatic install leaves nothing on disk to find on Windows 11).
    bool always = false;
    // Deep removal (D-060): driverClasses taken out of the image and its component store. The page
    // shows its own warning for these.
    bool deep = false;
    // D-113 (the reference tool's model): the entry it depends on — shown under it, removed with it.
    std::string parent;
    // D-113: store identity name patterns (core::ComponentStoreIndex::shareStore): what it is made
    // of in this image, its size, and — resolved to names — what its removal takes.
    std::vector<std::wstring> store;
    // D-113: shown for the tree's sake, never removable (a core part the others hang on).
    bool locked = false;
    // D-113: a grouping row with nothing of its own: there when something under it is.
    bool container = false;
};

class ComponentCatalog {
public:
    ComponentCatalog() = default;
    [[nodiscard]] static Result<ComponentCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<ComponentCatalogGroup>& groups() const noexcept { return m_groups; }
    // D-110: the reference tool's fourteen tree categories (+ "uncategorized"), in their order.
    [[nodiscard]] const std::vector<ComponentCatalogGroup>& categories() const noexcept { return m_categories; }
    // D-112: the names of the tree's fixed nodes ("languages" → "Dil", "advancedDevices" →
    // "Tek tek cihazlar [Gelişmiş]"); the id itself when missing.
    [[nodiscard]] std::wstring nodeName(std::string_view id, Language language) const;
    [[nodiscard]] const std::vector<ComponentCatalogEntry>& components() const noexcept { return m_components; }
    [[nodiscard]] const ComponentCatalogEntry* find(std::string_view id) const;

private:
    std::vector<ComponentCatalogGroup> m_groups;
    std::vector<ComponentCatalogGroup> m_categories;
    std::vector<ComponentCatalogGroup> m_nodes;
    std::vector<ComponentCatalogEntry> m_components;
};

} // namespace wl::app
