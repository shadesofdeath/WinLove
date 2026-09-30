#pragma once
// P12 logic (docs/pages/12-tweaks.md): form settings ↔ ChangeSet operations. Nothing is stored
// here: the selected option of a setting is read back from the queue — the option whose registry
// writes and service start types are all queued with its values; none → the Windows default.
// So a tweak checked on the Registry page (same writes) shows here, and presets restore the form.
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/state/AppState.h"

#include <vector>

namespace wl::app {

class ImageSettingsController {
public:
    ImageSettingsController(AppState& state, ImageSettingsCatalog catalog);

    [[nodiscard]] const ImageSettingsCatalog& catalog() const noexcept { return m_catalog; }

    // Index into setting.options.
    [[nodiscard]] int current(const ImageSetting& setting) const { return optionIn(m_state.changes(), setting); }
    // The same reading for any change set (a preset being listed or compared, P15).
    [[nodiscard]] static int optionIn(const core::ops::ChangeSet& changes, const ImageSetting& setting);
    // Queues `option` (the default option just removes the setting's operations).
    void select(const ImageSetting& setting, int option);
    // "Önerilenleri uygula": every setting with a recommended option, as one queue edit.
    // Returns how many settings changed.
    int applyRecommended();
    [[nodiscard]] int changedCount() const; // nav badge: settings not at the Windows default

    // Pure mapping, unit-tested.
    [[nodiscard]] static std::vector<core::ops::Operation> operationsFor(const ImageSetting& setting, int option);

private:
    // Queue slots held by any option of `setting` with that option's value (what select() replaces).
    void collectQueued(const ImageSetting& setting, std::vector<std::pair<core::ops::OpKind, std::wstring>>& slots) const;

    AppState& m_state;
    ImageSettingsCatalog m_catalog;
};

} // namespace wl::app
