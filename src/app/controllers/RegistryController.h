#pragma once
// P11 logic (docs/pages/11-registry.md): catalog tweaks and imported .reg files ↔ SetRegistryValue
// operations (target "<key>::<name>", value in .reg syntax). A tweak is "checked" when every one
// of its writes is queued with its value; toggling queues or removes all of them. Imported .reg
// files are queued whole on import and can be toggled or removed like a tweak. Their values are
// always SetRegistryFirstLogon (written offline and re-imported after setup, D-026): nobody knows
// which values of an arbitrary file Windows resets during OOBE / first logon.
#include "app/catalog/TweakCatalog.h"
#include "app/state/AppState.h"

namespace wl::app {

class RegistryController {
public:
    RegistryController(AppState& state, TweakCatalog catalog);

    [[nodiscard]] const TweakCatalog& catalog() const noexcept { return m_catalog; }

    [[nodiscard]] bool checked(const std::vector<core::RegistryWrite>& writes,
                               core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue) const;
    [[nodiscard]] bool checked(const Tweak& tweak) const { return checked(tweak.writes, kindOf(tweak)); }
    void toggle(const Tweak& tweak);
    void setChecked(const std::vector<core::RegistryWrite>& writes, bool on, core::ops::Risk risk,
                    core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue);
    [[nodiscard]] static core::ops::OpKind kindOf(const Tweak& tweak) {
        return tweak.firstLogon ? core::ops::OpKind::SetRegistryFirstLogon : core::ops::OpKind::SetRegistryValue;
    }

    static constexpr core::ops::OpKind kImportKind = core::ops::OpKind::SetRegistryFirstLogon;
    // Can the image take this write: offline hive, or (no hive) at least the post-setup import.
    [[nodiscard]] static bool importable(const core::RegistryWrite& write);

    // Checked / total tweaks of a category ("custom" = imported .reg files).
    [[nodiscard]] std::pair<int, int> selection(std::string_view category) const;
    [[nodiscard]] int checkedCount() const; // nav badge: tweaks + imports

    // Imports: parsed on the reader thread by the caller, then added here (queued immediately).
    void addImport(const std::filesystem::path& file, std::vector<core::RegistryWrite> writes);
    void toggleImport(std::size_t index);
    void removeImport(std::size_t index);

    [[nodiscard]] static core::ops::Operation operationFor(const core::RegistryWrite& write, core::ops::Risk risk,
                                                           core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue);

private:
    AppState& m_state;
    TweakCatalog m_catalog;
};

} // namespace wl::app
