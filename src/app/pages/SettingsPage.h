#pragma once
// P16 Uygulama ayarları (docs/pages/16-app-settings.md, screen 19): one form —
// GÖRÜNÜM (theme, reduce motion), DİL (interface language), ÇALIŞMA ORTAMI (work folder, mount
// folder, the DISM library in use). Every change is saved to settings.json at once
// (AppState::setSettings); the App applies theme / motion / language from there.
// The folders cannot change while an image is mounted or something is running.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/Toggle.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace wl::app {

class PathField;
class AccentSwatches;

class SettingsPage : public ui::Widget {
public:
    struct Intents {
        std::function<std::optional<std::filesystem::path>()> pickFolder;
        std::function<bool()> busy; // an operation, apply run or ISO build is running
    };
    SettingsPage(AppState& state, const Localization& strings, Intents intents);
    ~SettingsPage() override;

    // "Varsayılanlara dön": appearance and language always; the folders when they may change.
    void resetToDefaults();
    // True while the work folders must stay as they are (mounted image / running job).
    [[nodiscard]] bool foldersLocked() const;

    void layout() override;

private:
    void edit(const std::function<void(AppSettings&)>& change);
    void commitFolder(PathField& field, std::filesystem::path AppSettings::*member, bool allowEmpty);
    void sync();

    AppState& m_state;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::FormView* m_form = nullptr;
    ui::RadioGroup* m_theme = nullptr;
    AccentSwatches* m_accent = nullptr;
    ui::Toggle* m_motion = nullptr;
    ui::Dropdown* m_language = nullptr;
    PathField* m_work = nullptr;
    PathField* m_mount = nullptr;
};

} // namespace wl::app
