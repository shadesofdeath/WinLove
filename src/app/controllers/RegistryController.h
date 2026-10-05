#pragma once
// P11 logic (docs/pages/11-registry.md, D-067): the user's own registry entries ↔ registry
// operations (target "<key>::<name>", value in .reg syntax). An entry is an imported .reg file
// (queued whole on import) or one value typed in the page's dialog; either can be switched off
// (its operations leave the queue), edited (a typed value) or removed. Ready-made tweaks live on
// the Ayarlar page (settings.json).
// Imported values are always SetRegistryFirstLogon (written offline and re-imported after setup,
// D-026): nobody knows which values of an arbitrary file Windows resets during OOBE / first logon.
// A typed value chooses: re-applied after setup (the default) or offline only.
#include "app/state/AppState.h"

namespace wl::app {

class RegistryController {
public:
    explicit RegistryController(AppState& state);

    [[nodiscard]] bool checked(const std::vector<core::RegistryWrite>& writes,
                               core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue) const;
    void setChecked(const std::vector<core::RegistryWrite>& writes, bool on, core::ops::Risk risk,
                    core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue);

    static constexpr core::ops::OpKind kImportKind = core::ops::OpKind::SetRegistryFirstLogon;
    [[nodiscard]] static core::ops::OpKind kindOf(const AppState::RegImport& entry) {
        return entry.afterSetup ? core::ops::OpKind::SetRegistryFirstLogon : core::ops::OpKind::SetRegistryValue;
    }
    // Can the image take this write: offline hive, or (no hive) at least the post-setup import.
    [[nodiscard]] static bool importable(const core::RegistryWrite& write);

    [[nodiscard]] bool checked(std::size_t entry) const;
    [[nodiscard]] std::pair<int, int> selection() const; // checked / all entries
    [[nodiscard]] int checkedCount() const;              // nav badge

    // Imports: parsed on the reader thread by the caller, then added here (queued immediately).
    void addImport(const std::filesystem::path& file, std::vector<core::RegistryWrite> writes);
    // A typed value (queued immediately). False when the image cannot take it.
    bool addValue(core::RegistryWrite write, bool afterSetup);
    bool replaceValue(std::size_t entry, core::RegistryWrite write, bool afterSetup);
    void toggle(std::size_t entry);
    void remove(std::size_t entry);

    [[nodiscard]] static core::ops::Operation operationFor(const core::RegistryWrite& write, core::ops::Risk risk,
                                                           core::ops::OpKind kind = core::ops::OpKind::SetRegistryValue);

private:
    // Slots the new writes take move to the end of the queue (an earlier entry or preset may hold them).
    void requeue(const std::vector<core::RegistryWrite>& writes, core::ops::OpKind kind);

    AppState& m_state;
};

} // namespace wl::app
