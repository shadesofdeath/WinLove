#pragma once
// P12 logic (docs/pages/12-tweaks.md): form settings ↔ ChangeSet operations. Nothing is stored
// here: the selected option of a setting is read back from the queue — the option whose registry
// writes, service start types and files are all queued with its values; none → the Windows default.
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

    // Text and file settings: what the user typed / picked; empty = the Windows default.
    [[nodiscard]] static bool takesValue(const ImageSetting& setting) noexcept {
        return setting.control == ImageSetting::Control::Text || setting.control == ImageSetting::Control::File;
    }
    [[nodiscard]] std::wstring value(const ImageSetting& setting) const { return valueIn(m_state.changes(), setting); }
    [[nodiscard]] static std::wstring valueIn(const core::ops::ChangeSet& changes, const ImageSetting& setting);
    // Queues the value (an empty one removes the setting's operations). False when a file setting
    // is given something that is not a JPEG file of this PC: nothing is queued for it then.
    bool setValue(const ImageSetting& setting, std::wstring value);
    static constexpr std::size_t kTextLimit = 200;

    // Pure mapping, unit-tested. `value`: for text / file settings (option 1).
    [[nodiscard]] static std::vector<core::ops::Operation> operationsFor(const ImageSetting& setting, int option,
                                                                         std::wstring_view value = {});

private:
    // Queue slots held by any option of `setting` with that option's value (what select() replaces).
    void collectQueued(const ImageSetting& setting, std::vector<std::pair<core::ops::OpKind, std::wstring>>& slots) const;

    AppState& m_state;
    ImageSettingsCatalog m_catalog;
};

} // namespace wl::app
