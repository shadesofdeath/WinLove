#pragma once
// D-093 "Windows indir": the builds Microsoft's update servers have (UUP dump's list) in a table —
// search, release / Insider / server filter, architecture — and on the right what to make of the
// selected one: language, editions, updates, Edge, a smaller install.esd, where the ISO goes, the
// download size; "İndir ve ISO oluştur" starts WindowsDownloadController. While it runs the panel
// shows the stage, a bar, bytes and the time left, and "Durdur".
#include "app/Localization.h"
#include "app/controllers/WindowsDownloadController.h"
#include "app/shell/PageView.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PillGroup.h"
#include "ui/widgets/ProgressRing.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <optional>

namespace wl::app {

// 2026-11-10: a body that scrolls when the panel is taller than the room (at 700 DIP the panel's
// "İndir ve ISO oluştur" was below the card's edge, out of reach).
class DownloadPage : public ui::Widget, public ScrollableBody {
public:
    struct Intents {
        // The ISO's path from a save dialog; nullopt = cancelled.
        std::function<std::optional<std::filesystem::path>(const std::filesystem::path& suggested)> pickOutput;
        // Not elevated: the conversion needs DISM, so the job does not start (the download would be wasted).
        std::function<void()> adminRequired;
    };
    DownloadPage(AppState& state, const Localization& strings, Language language, WindowsDownloadController& controller,
                 Intents intents);
    ~DownloadPage() override;

    // From the controller's events (the Shell forwards them).
    void setBuilds(std::vector<core::uup::Build> builds);
    void setListFailed(const Error& error);
    void setLanguages(const std::wstring& id, std::vector<core::uup::Language> languages);
    void setEditions(const std::wstring& id, const std::wstring& language, std::vector<core::uup::Edition> editions);
    void setFiles(const std::wstring& id, const std::wstring& language, const std::vector<std::wstring>& editions,
                  const core::uup::FileSet& files);
    void setApps(const std::wstring& id, const std::wstring& language, const std::vector<std::wstring>& editions,
                 std::vector<core::uup::AppFeature> apps);
    // D-149: Microsoft's official ESD catalog of a product (or why it did not come).
    void setEsdCatalog(core::esd::Product product, Result<core::esd::Catalog> catalog);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] float contentHeight() const override { return m_contentHeight; }
    // While a job runs: the progress ring's pace (D-142), else the ETA's.
    [[nodiscard]] float tickIntervalMs() const override { return m_ring.tickMs(250.0f); }
    bool tick(double now) override;

    // --render demos: a list, a selection, a running job.
    void demo(int what);
    // The list asked for again (the ribbon's "Yenile", and the empty state's after a failure).
    void reload() { loadBuilds(); }

private:
    class Panel;
    void refreshList();
    void select(int row);
    void requestFiles();
    void loadBuilds();     // the product's list (Windows 11 / 10 / all)
    void savePrefs();      // the panel's choices into settings.json
    void openAppPicker();  // which Store apps go in
    void requestApps();    // the picker's list for the selection
    void updateAppList();  // the panel's Store apps state from what arrived
    void updatePanel();
    void updateChoice();   // D-149: which source the selection gets (core::esd::chooseSource)
    void refreshOutput();  // the ISO's name for the source (an ESD has a build of its own)
    [[nodiscard]] bool newestOfRelease(const core::uup::Build& build) const;
    [[nodiscard]] const core::uup::Build* selectedBuild() const;
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    const Localization& m_strings;
    Language m_language;
    WindowsDownloadController& m_controller;
    Intents m_intents;
    std::size_t m_subscription = 0;

    std::vector<core::uup::Build> m_all;   // the API's list
    std::vector<core::uup::Build> m_shown; // after search and filters
    std::vector<core::uup::AppFeature> m_apps; // the selection's Store apps (the picker's list)
    std::optional<bool> m_appSet;              // the selection's files say it has an app set (appxPresent)
    bool m_appsArrived = false;                // the picker's list came back (empty when it failed)
    bool m_loading = true;
    std::optional<core::esd::Catalog> m_esd[2]; // D-149: the official catalogs (index: Product)
    bool m_esdFailed[2] = {false, false};
    std::optional<Error> m_listError;

    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_product = nullptr; // Windows 11 / Windows 10 / all
    ui::Dropdown* m_kind = nullptr;
    ui::Dropdown* m_arch = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
    Panel* m_panel = nullptr;
    float m_contentHeight = 0; // the panel's height (layout()); the list takes what the room gives
    bool m_inLayout = false;
    ui::RingClock m_ring; // D-142: the job line's progress ring
};

} // namespace wl::app
