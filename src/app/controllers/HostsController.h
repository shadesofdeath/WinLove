#pragma once
// D-049 logic of the Hosts page: the block lists of resources/catalog/hosts.json and the entries
// the user imports ("custom"), each one section of the image's hosts file (HostsFile.h) and one
// SetHosts operation. A list is "on" when its section is queued with its entries, or — nothing
// queued — the image's hosts file already has that section as it is. Turning an image section
// off queues an empty one (the section is removed).
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/state/AppState.h"
#include "core/image/HostsFile.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct HostsList {
    std::wstring id;
    LocalizedText name;
    LocalizedText description;
    core::ops::Risk risk = core::ops::Risk::Medium;
    bool recommended = false;
    std::vector<core::HostEntry> entries;
    [[nodiscard]] std::wstring text() const { return core::formatHostEntries(entries); }
};

class HostsController {
public:
    static constexpr wchar_t kCustom[] = L"custom";

    HostsController(AppState& state, std::string_view catalogJson);
    [[nodiscard]] static std::vector<HostsList> parseCatalog(std::string_view json);

    [[nodiscard]] const std::vector<HostsList>& lists() const noexcept { return m_lists; }
    [[nodiscard]] bool on(const HostsList& list) const;
    [[nodiscard]] bool inImage(const HostsList& list) const;
    void toggle(const HostsList& list);
    int applyRecommended();

    // The user's own entries: queued, else the image's "custom" section.
    [[nodiscard]] std::vector<core::HostEntry> customEntries() const;
    [[nodiscard]] bool customInImage() const;
    // Adds the entries of a hosts-format text to the custom section (duplicates by name dropped;
    // localhost lines never). Returns how many were new.
    std::size_t importText(std::wstring_view text);
    void clearCustom();

    [[nodiscard]] int changedCount() const; // nav badge: SetHosts operations
    [[nodiscard]] std::size_t totalEntries() const; // what the image will block after Uygula

private:
    [[nodiscard]] const core::ops::Operation* queuedSection(std::wstring_view id) const;
    void setSection(std::wstring_view id, std::wstring entries, core::ops::Risk risk);

    AppState& m_state;
    std::vector<HostsList> m_lists;
};

} // namespace wl::app
