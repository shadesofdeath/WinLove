#pragma once
// D-093 "Windows indir": the builds Microsoft's update servers have (UUP dump's list) in a table —
// search, release / Insider / server filter, architecture — and on the right what to make of the
// selected one: language, editions, updates, Edge, a smaller install.esd, where the ISO goes, the
// download size; "İndir ve ISO oluştur" starts WindowsDownloadController. While it runs the panel
// shows the stage, a bar, bytes and the time left, and "Durdur".
#include "app/Localization.h"
#include "app/controllers/WindowsDownloadController.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <optional>

namespace wl::app {

class DownloadPage : public ui::Widget {
public:
    struct Intents {
        // The ISO's path from a save dialog; nullopt = cancelled.
        std::function<std::optional<std::filesystem::path>(const std::filesystem::path& suggested)> pickOutput;
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

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] float tickIntervalMs() const override { return 250.0f; } // ETA while a job runs
    bool tick(double now) override;

    // --render demos: a list, a selection, a running job.
    void demo(int what);

private:
    class Panel;
    void refreshList();
    void select(int row);
    void requestFiles();
    void loadBuilds();     // the product's list (Windows 11 / 10 / all)
    void savePrefs();      // the panel's choices into settings.json
    void openAppPicker();  // which Store apps go in
    void updatePanel();
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
    bool m_loading = true;
    std::optional<Error> m_listError;

    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_product = nullptr; // Windows 11 / Windows 10 / all
    ui::Dropdown* m_kind = nullptr;
    ui::Dropdown* m_arch = nullptr;
    ui::Button* m_refresh = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
    Panel* m_panel = nullptr;
};

} // namespace wl::app
