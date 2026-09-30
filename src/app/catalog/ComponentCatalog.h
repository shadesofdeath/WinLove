#pragma once
// resources/catalog/components.json: what the Components page offers besides the provisioned
// apps — system components (a core::ComponentRecipe each: CBS packages, paths, registry) and the
// component store cleanup. Entries whose recipe does not pass core::validateComponentRecipe are
// skipped and logged, not fatal.
#include "app/catalog/ImageSettingsCatalog.h" // LocalizedText
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
    enum class Kind : std::uint8_t { Remove, Cleanup };
    std::string id;
    std::string group;
    Kind kind = Kind::Remove;
    LocalizedText name;
    LocalizedText notes;
    core::ops::Risk risk = core::ops::Risk::Medium;
    core::ComponentRecipe recipe; // Remove (title left empty: the queue operation gets the shown name)
    bool resetBase = true;        // Cleanup
    // Remove: offered for every image, present or not — a preventive change (the new Outlook's
    // automatic install leaves nothing on disk to find on Windows 11).
    bool always = false;
};

class ComponentCatalog {
public:
    ComponentCatalog() = default;
    [[nodiscard]] static Result<ComponentCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<ComponentCatalogGroup>& groups() const noexcept { return m_groups; }
    [[nodiscard]] const std::vector<ComponentCatalogEntry>& components() const noexcept { return m_components; }
    [[nodiscard]] const ComponentCatalogEntry* find(std::string_view id) const;

private:
    std::vector<ComponentCatalogGroup> m_groups;
    std::vector<ComponentCatalogEntry> m_components;
};

} // namespace wl::app
