#pragma once
// P17 Hakkında (docs/pages/17-about.md, screen 20; D-072): the mark, name and version line; the
// Turkish flag with "made with love", what WinLove promises (free, no ads, no data); the links
// (developer, project, issues, license) as link buttons; then the system rows (DISM, work folder,
// fonts, third party) and two actions: the licenses of what is bundled, and the log folder.
// Nothing here can be queued or applied.
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
        std::function<void(const std::wstring&)> openLink; // a https:// address, in the browser
    };
    AboutPage(AppState& state, const Localization& strings, Intents intents);
    ~AboutPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

    // Where the links go (README and the release use the same).
    static constexpr const wchar_t* kDeveloperUrl = L"https://github.com/shadesofdeath";
    static constexpr const wchar_t* kProjectUrl = L"https://github.com/shadesofdeath/WinLove";
    static constexpr const wchar_t* kIssuesUrl = L"https://github.com/shadesofdeath/WinLove/issues";
    static constexpr const wchar_t* kLicenseUrl = L"https://github.com/shadesofdeath/WinLove/blob/main/LICENSE";

private:
    struct Link {
        Str label;
        ui::Button* button;
    };
    void refresh();
    [[nodiscard]] float freeBodyHeight(float width) const;

    AppState& m_state;
    const Localization& m_strings;
    std::size_t m_subscription = 0;
    std::vector<std::pair<std::wstring, std::pair<std::wstring, bool>>> m_rows; // label → (value, mono)
    std::wstring m_version;
    std::vector<Link> m_links;
    ui::Button* m_licenses = nullptr;
    ui::Button* m_logs = nullptr;
};

} // namespace wl::app
