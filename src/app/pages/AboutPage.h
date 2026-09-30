#pragma once
// P17 Hakkında (docs/pages/17-about.md, screen 20): the mark, name and version line, then
// key / value rows (DISM, work folder, fonts, third party) and two actions: the licenses of what
// is bundled, and the log folder. Nothing here can be queued or applied.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wl::app {

class AboutPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> showLicenses;
        std::function<void()> openLogFolder;
    };
    AboutPage(AppState& state, const Localization& strings, Intents intents);
    ~AboutPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();

    AppState& m_state;
    const Localization& m_strings;
    std::size_t m_subscription = 0;
    std::vector<std::pair<std::wstring, std::pair<std::wstring, bool>>> m_rows; // label → (value, mono)
    std::wstring m_version;
    ui::Button* m_licenses = nullptr;
    ui::Button* m_logs = nullptr;
};

} // namespace wl::app
