#pragma once
// Root widget of the main window (layout-system.md): TitleBar 32 on top, StatusBar 24 at the
// bottom, NavRail 200/44 on the left, the current page, and the Inspector column (280) on the
// right when the page has one. Owns page switching, nav collapse, app-level shortcuts
// (interaction.md "Kısayollar"), the source-opening flow, P02 image operations (through
// ImageController), dialogs and the toast.
#include "app/Localization.h"
#include "app/controllers/ApplyController.h"
#include "app/controllers/FeatureController.h"
#include "app/controllers/ImageController.h"
#include "app/pages/PageInfo.h"
#include "app/shell/NavRail.h"
#include "app/shell/PageView.h"
#include "app/shell/StatusBar.h"
#include "app/shell/TitleBar.h"
#include "app/state/AppState.h"
#include "ui/anim/Tween.h"
#include "ui/widgets/Toast.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace wl::ui {
class Dialog;
}

namespace wl::app {

class SourcePage;
class ImagesPage;
class ImageInspector;
class LogsPage;
class FeaturesPage;
class ApplyPage;

class Shell : public ui::Widget {
public:
    // Everything the shell needs from the window/app, as plain callbacks (keeps it testable and
    // usable from the headless --render path).
    struct Services {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> close;
        std::function<void()> toggleTheme;
        std::function<void(std::function<void()>)> postToUi;     // run on the UI thread later
        std::function<HWND()> ownerWindow;                       // for system dialogs (may be null)
        std::function<bool(const std::wstring& args)> relaunchElevated; // true = new process started
        std::function<void(UINT id, UINT ms)> startTimer;
        std::function<void(UINT id)> stopTimer;
    };
    static constexpr UINT kToastTimer = 2;
    static constexpr UINT kLogTimer = 3; // Loglar: poll the log buffer while the page is shown

    Shell(const Localization& strings, Language language, AppState& state, Services services);
    ~Shell() override;

    TitleBar& titleBar() { return *m_titleBar; }
    NavRail& nav() { return *m_nav; }
    ImageController& images() { return *m_images; }
    FeatureController& features() { return *m_features; }
    ApplyController& apply() { return *m_apply; }
    // "Uygula": straight to the run, or through the 13b confirmation when something is irreversible.
    void requestApply();
    [[nodiscard]] PageId currentPage() const noexcept { return m_page; }

    void showPage(PageId page);
    void setNavCollapsed(bool collapsed, bool animated);
    [[nodiscard]] bool navCollapsed() const noexcept { return m_navTarget < 0.5f; }
    // Shortcuts not consumed by the focused widget. Returns true if handled.
    bool handleShortcut(const ui::KeyEvent& key);
    void onTimer(UINT id);
    void showToast(ui::InfoKind kind, std::wstring title, std::wstring message);

    // ---- sources --------------------------------------------------------------------------
    // `then` runs after a successful open (e.g. --mount=N after a UAC relaunch).
    void openSource(const std::filesystem::path& path, std::function<void()> then = {});
    // A mount from a previous run (ImageController::adoptExistingMount): reopen its source and
    // show it as mounted again.
    void restoreMount(const std::filesystem::path& source, MountedImage mounted);
    // Restores AppState::mountFolder's healthy mount (Images page "Devam et", and automatically).
    void continueFolderMount();
    void pickSourceFile();
    void pickSourceFolder();
    // s4: explains why admin is needed; "Yönetici olarak yeniden başlat" relaunches with `args`.
    void showAdminRequired(std::wstring relaunchArgs = L"--page=source");
    // Window-wide drag & drop (interaction.md): returns true when the drop would be accepted.
    bool dragEnter(const std::vector<std::filesystem::path>& files);
    void dragLeave();
    void drop(const std::vector<std::filesystem::path>& files);

    // ---- images (P02) ---------------------------------------------------------------------
    void askUnmount();
    void askDeleteSelected();
    void exportSelected();
    void convertEsd();
    void exportLog();

    void layout() override;
    bool tick(double now) override;

private:
    void updateBreadcrumb();
    void updateImagesChrome();
    void updateStatus();
    void onImageFailure(ImageController::Failure failure, const Error& error, int index);
    [[nodiscard]] SourcePage* sourcePage() const;
    [[nodiscard]] ImagesPage* imagesPage() const;
    [[nodiscard]] LogsPage* logsPage() const;
    [[nodiscard]] FeaturesPage* featuresPage() const;
    [[nodiscard]] ApplyPage* applyPage() const;
    void updateApplyChrome();                         // CTA label, Apply page mode/header
    void savePreset(const core::ops::ChangeSet& changes);
    void saveApplyLog();
    void showApplyConfirm();
    void updateQueue(); // CTA count, nav badges, page actions that depend on the queue
    [[nodiscard]] bool inspectorVisible() const;
    ui::Dialog& pushDialog(std::unique_ptr<ui::Dialog> dialog);

    const Localization& m_strings;
    Language m_language;
    AppState& m_state;
    Services m_services;
    std::unique_ptr<ImageController> m_images;
    std::unique_ptr<FeatureController> m_features;
    std::unique_ptr<ApplyController> m_apply;
    int m_applyMode = -1; // ApplyPage::Mode the Apply page was built for
    ui::Button* m_actionReset = nullptr; // Özellikler: "Değişiklikleri sıfırla"
    std::size_t m_subscription = 0;
    // Engine results arrive later on the UI thread; they check this before touching the shell
    // (it is rebuilt on device loss and destroyed at exit).
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    TitleBar* m_titleBar = nullptr;
    NavRail* m_nav = nullptr;
    StatusBar* m_status = nullptr;
    PageView* m_pageView = nullptr;
    ui::Widget* m_pageBody = nullptr;
    ImageInspector* m_inspector = nullptr;
    ui::Toast* m_toast = nullptr;
    // Images page header actions (kept to update enabled/text as state changes).
    ui::Button* m_actionMount = nullptr;
    ui::Button* m_actionExport = nullptr;
    ui::Button* m_actionEsd = nullptr;
    PageId m_page = PageId::Source;
    std::uint64_t m_openSerial = 0; // latest openSource request; older results are dropped
    bool m_autoRestoreTried = false; // one automatic restore per session; then the page offers it
    ui::Tween m_navExpansion{1.0f};
    float m_navTarget = 1.0f;
    bool m_userCollapsed = false; // the user's choice; narrow windows collapse on top of it
    bool m_narrow = false;        // width < 1200 (layout-system.md breakpoint)
};

} // namespace wl::app
