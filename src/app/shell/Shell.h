#pragma once
// Root widget of the main window (layout-system.md): TitleBar 32 on top, StatusBar 24 at the
// bottom, NavRail 200/44 on the left, the current page, and the Inspector column (280) on the
// right when the page has one. Owns page switching, nav collapse, app-level shortcuts
// (interaction.md "Kısayollar"), the source-opening flow, P02 image operations (through
// ImageController), dialogs and the toast.
#include "app/Localization.h"
#include "app/controllers/ApplyController.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/RegistryController.h"
#include "app/controllers/ServiceController.h"
#include "app/controllers/UnattendController.h"
#include "app/controllers/FeatureController.h"
#include "app/controllers/IsoController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/ImageController.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/AppsController.h"
#include "app/controllers/FilesController.h"
#include "app/controllers/HostsController.h"
#include "app/controllers/BrandingController.h"
#include "app/controllers/ImageDriverController.h"
#include "app/controllers/LanguageController.h"
#include "app/controllers/LanguageFetchController.h"
#include "app/controllers/ImageValuesController.h"
#include "app/controllers/TaskController.h"
#include "app/controllers/PreloadController.h"
#include "app/controllers/UpdateCatalogController.h"
#include "app/controllers/PresetController.h"
#include "app/pages/PageInfo.h"
#include "app/shell/NavRail.h"
#include "app/shell/PageView.h"
#include "app/shell/PaletteIndex.h"
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
class IsoPage;
class ComponentsPage;
class UpdatesPage;
class DriversPage;
class AppsPage;
class FilesPage;
class ServicesPage;
class RegistryPage;
class UnattendedPage;
class PresetsPage;
class SettingsPage;
class AboutPage;

class Shell : public ui::Widget {
public:
    // Everything the shell needs from the window/app, as plain callbacks (keeps it testable and
    // usable from the headless --render path).
    struct Services {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> close;
        std::function<void()> toggleTheme;
        std::function<void()> settingsChanged; // AppState::Change::Settings: theme, motion, language
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
    ImageController& images() { return *m_images; }
    ComponentController& components() { return *m_components; }
    ServiceController& services() { return *m_serviceCtl; }
    IsoPage* isoPageForDemo() const { return isoPage(); } // render: --demo-usb
    DriversPage* driversPageForDemo() const { return driversPage(); } // render: --demo-image-drivers
    // D-051: "Dosya ekle…" / "Klasör ekle…" / a drop on the Dosyalar page → "Nereye?" → queue.
    void addFilesTo(std::vector<std::filesystem::path> sources);
    void startIsoForDemo() { startIso(); }
    // D-046: the catalog's offers as a check list (also the render demo).
    void showUpdateOffers(const core::CatalogTarget& target, std::vector<core::CatalogOffer> offers);
    RegistryController& registry() { return *m_registry; }
    TaskController& tasks() { return *m_tasks; }
    ApplyController& applyForDemo() { return *m_apply; }
    FilesController& filesForDemo() { return *m_files; }
    AppsController& appsForDemo() { return *m_apps; }
    LanguageController& languagesForDemo() { return *m_languages; }
    void languageOffersForDemo(const LanguageTarget& target, const std::vector<core::UupLanguage>& languages) {
        showLanguageOffers(target, languages);
    }
    AppsPage* appsPageForDemo() const { return appsPage(); }
    HostsController& hosts() { return *m_hosts; }
    BrandingController& branding() { return *m_branding; }
    void addFonts(std::vector<std::filesystem::path> files); // D-056: queue, toast for the refused ones
    ImageSettingsController& imageSettings() { return *m_imageSettings; }
    UnattendController& unattend() { return *m_unattend; }
    PostSetupController& postSetup() { return *m_postSetup; }
    PresetController& presets() { return *m_presets; }
    // "Uygula": straight to the run, or through the 13b confirmation when something is irreversible.
    void requestApply();
    [[nodiscard]] PageId currentPage() const noexcept { return m_page; }

    void showPage(PageId page);
    // P18: Ctrl+K or the title bar box. `query` as if typed (render checks).
    void openPalette(std::wstring query = {});
    void setNavCollapsed(bool collapsed, bool animated);
    [[nodiscard]] bool navCollapsed() const noexcept { return m_navTarget < 0.5f; }
    // Shortcuts not consumed by the focused widget. Returns true if handled.
    bool handleShortcut(const ui::KeyEvent& key);
    void onTimer(UINT id);
    void showToast(ui::InfoKind kind, std::wstring title, std::wstring message);

    // ---- sources --------------------------------------------------------------------------
    // `then` runs after a successful open (e.g. --mount=N after a UAC relaunch).
    void openSource(const std::filesystem::path& path, std::function<void()> then = {});
    void startPreload(); // image values first (fast), then the page lists
    // A mount from a previous run (ImageController::adoptExistingMount): reopen its source and
    // show it as mounted again.
    void restoreMount(const std::filesystem::path& source, MountedImage mounted);
    // Restores AppState::mountFolder's healthy mount (Images page "Devam et", and automatically).
    void continueFolderMount();
    void pickSourceFile();
    void pickSourceFolder();
    // Source page: takes an entry out of the recent list; for a work copy WinLove extracted
    // itself, offers to delete the folder too. The user's own files are never deleted.
    void removeSource(const std::filesystem::path& path);
    // s4: explains why admin is needed; "Yönetici olarak yeniden başlat" relaunches with `args`.
    void showAdminRequired(std::wstring relaunchArgs = L"--page=source");
    // Window-wide drag & drop (interaction.md): returns true when the drop would be accepted.
    bool dragEnter(const std::vector<std::filesystem::path>& files);
    void dragLeave();
    void drop(const std::vector<std::filesystem::path>& files);

    // ---- images (P02) ---------------------------------------------------------------------
    void askUnmount();
    // Confirms, then deletes the selected edition — or, with `keepOnly`, every other one.
    void askDeleteSelected(bool keepOnly = false);
    void askRenameSelected();
    // The mounted edition: reads what it can become, then offers the choice for the queue.
    void askUpgradeEdition();
    // Shortcuts out of the app (P02): Explorer at the mount folder (Ctrl+E, from any page), a
    // command prompt there, Explorer with the image file selected (Ctrl+Shift+E), the details of
    // the marked editions as text on the clipboard (Ctrl+C in the table).
    void exploreMount();
    void openTerminalAtMount();
    void revealImageFile();
    void copyEditionInfo();
    // Reads the open source again (F5 on the Images page): a WIM changed by another tool.
    void refreshSource();
    void onImageVerified(const core::WimVerifyReport& report, const std::wstring& file);
    void exportSelected();
    void convertEsd();
    // D-058
    void showImageTools();
    void askRecompress();
    void askSplitSwm();
    void askMergeSwm();
    void askAppendEditions();
    void askDuplicateEdition();
    void askCapture();
    void verifyHash(const std::filesystem::path& file);
    void exportLog();

    void layout() override;
    bool tick(double now) override;

private:
    void updateBreadcrumb();
    void updateImagesChrome();
    void updateStatus();
    void onImageFailure(ImageController::Failure failure, const Error& error);
    [[nodiscard]] SourcePage* sourcePage() const;
    [[nodiscard]] ImagesPage* imagesPage() const;
    [[nodiscard]] LogsPage* logsPage() const;
    [[nodiscard]] FeaturesPage* featuresPage() const;
    [[nodiscard]] ApplyPage* applyPage() const;
    [[nodiscard]] IsoPage* isoPage() const;
    [[nodiscard]] ComponentsPage* componentsPage() const;
    void updateComponentInspector();
    void loadPreset();
    [[nodiscard]] UpdatesPage* updatesPage() const;
    void addUpdates(const std::vector<std::filesystem::path>& files);
    void findUpdates(); // D-046: Microsoft Update Catalog → check list → download → queue
    [[nodiscard]] DriversPage* driversPage() const;
    [[nodiscard]] ServicesPage* servicesPage() const;
    [[nodiscard]] RegistryPage* registryPage() const;
    void importRegFiles(const std::vector<std::filesystem::path>& files);
    void importAnswerFile();
    // P15: the library actions (each refreshes the Presets page when it is showing).
    [[nodiscard]] PresetsPage* presetsPage() const;
    void newPreset();
    void importPreset();
    void applyPreset(const Preset& preset);
    // P14: the add dialog for `type`, or the edit dialog of step `index`.
    void editPostSetupStep(core::PostSetupStep::Type type, std::optional<std::size_t> index);

public:
    void wifiDialogForDemo() { editPostSetupStep(core::PostSetupStep::Type::Wifi, std::nullopt); }
    void toolDialogForDemo(const std::wstring& which, const std::filesystem::path& file); // D-058 renders

private:
    void pickPostSetupApps(); // "Hazır uygulamalar": the winget catalog as a check list
    void pickPostSetupCommands(); // "Hazır komutlar": power plan, network
    void saveAnswerFile();
    void scanDriverFolder();
    void updateIsoChrome();
    void startIso();
    void confirmUsbWrite(IsoController::Request request); // D-047: "USB belleği sil ve yaz?"
    void addTaskDialog();    // D-048: "Görev ekle…"
    void importHostsFile();  // D-049: "Hosts dosyası içe aktar…"

    [[nodiscard]] class FilesPage* filesPage() const;
    [[nodiscard]] class AppsPage* appsPage() const;
    void pickAppPackages();
    void scanLanguageFolder(); // D-053: folder → the files that fit → check list → queue
    void addLanguages();       // D-061: the build's languages on Windows Update → check list → download → queue
    void showLanguageOffers(const LanguageTarget& target, const std::vector<core::UupLanguage>& languages);
    void updateApplyChrome();                         // CTA label, Apply page mode/header
    void savePreset(const core::ops::ChangeSet& changes);
    void saveApplyLog();
    void saveApplyReport(); // the run as one HTML page (app/ApplyReport.h)
    void showApplyConfirm();
    void updateQueue(); // CTA count, nav badges, page actions that depend on the queue
    [[nodiscard]] bool inspectorVisible() const;
    void runPaletteItem(const PaletteItem& item);
    [[nodiscard]] bool paletteCommandAvailable(PaletteCommand command) const;
    void runPaletteCommand(PaletteCommand command);
    void toggleNav();
    void openLogFolder();
    ui::Dialog& pushDialog(std::unique_ptr<ui::Dialog> dialog);
    // A dialog built by a factory that takes its close action before the dialog exists: the slot
    // holds the dialog once shown; close() pops it once, whichever handler calls it first.
    struct ModalSlot {
        std::shared_ptr<ui::Dialog*> dialog = std::make_shared<ui::Dialog*>(nullptr);
        std::function<void()> close;
    };
    [[nodiscard]] ModalSlot modalSlot();
    void showModal(const ModalSlot& slot, std::unique_ptr<ui::Dialog> dialog, ui::Widget* focus);
    // Pops `dialog` (a handler of the dialog itself: Cancel, Escape).
    [[nodiscard]] std::function<void()> closer(ui::Widget* modal); // pops it
    [[nodiscard]] std::wstring errorText(const Error& error) const; // a toast line for an engine error
    [[nodiscard]] HWND owner() const; // file pickers' owner window
    // The queue belongs to a mounted image: false (and the page's own warning) without one.
    [[nodiscard]] bool requireMount(Str title, Str body);
    // `work` on the reader thread, then `onUi(result)` on the UI thread — unless the shell is gone
    // by then (every reader job used to repeat this post + liveness check).
    template <class T, class Work, class OnUi>
    void readThenUi(Work work, OnUi onUi) {
        m_state.reader().run<T>(std::move(work), [post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive),
                                                  onUi = std::move(onUi)](Result<T> result) {
            post([alive, onUi, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                onUi(std::move(result));
            });
        });
    }
public:
    // WM_CLOSE: while a mount / apply / ISO job runs, closing would leave a windowless process
    // (or a half-committed image). Shows why and keeps the window open.
    [[nodiscard]] bool confirmClose();
private:

    const Localization& m_strings;
    Language m_language;
    AppState& m_state;
    Services m_services;
    std::unique_ptr<ImageController> m_images;
    std::unique_ptr<FeatureController> m_features;
    std::unique_ptr<ApplyController> m_apply;
    std::unique_ptr<IsoController> m_iso;
    std::unique_ptr<UpdateCatalogController> m_updateCatalog;
    std::unique_ptr<TaskController> m_tasks;   // D-048
    std::unique_ptr<HostsController> m_hosts;  // D-049
    std::unique_ptr<BrandingController> m_branding; // D-056
    std::unique_ptr<FilesController> m_files;  // D-051
    std::unique_ptr<ImageDriverController> m_imageDriverCtl; // D-052
    std::unique_ptr<AppsController> m_apps; // D-050 / D-054
    std::unique_ptr<LanguageController> m_languages; // D-053
    std::unique_ptr<LanguageFetchController> m_languageFetch; // D-061
    std::wstring m_pendingUiLanguage; // "Arayüz dili yap" of the running download
    std::unique_ptr<ComponentController> m_components;
    std::unique_ptr<ServiceController> m_serviceCtl;
    std::unique_ptr<RegistryController> m_registry;
    std::unique_ptr<ImageSettingsController> m_imageSettings; // P12 form
    std::unique_ptr<UnattendController> m_unattend; // P13 answer file
    std::unique_ptr<PostSetupController> m_postSetup; // P14 steps
    std::unique_ptr<PresetController> m_presets;      // P15 library
    std::unique_ptr<PreloadController> m_preload; // reads the page lists right after a mount
    std::unique_ptr<ImageValuesController> m_imageValues; // what the image already has (D-045)
    std::unique_ptr<PaletteIndex> m_palette;      // P18: what Ctrl+K searches
    ui::Widget* m_sideInspector = nullptr; // pages other than Images (Components)
    ui::Button* m_actionExpand = nullptr;  // Components: "Tümünü genişlet / daralt"
    ui::Button* m_actionIso = nullptr; // ISO page: "ISO Oluştur" / "İptal"
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
    ui::Button* m_actionTools = nullptr; // D-058 "Araçlar"
    ui::Button* m_actionVerify = nullptr;
    PageId m_page = PageId::Source;
    std::uint64_t m_openSerial = 0; // latest openSource request; older results are dropped
    bool m_autoRestoreTried = false; // one automatic restore per session; then the page offers it
    ui::Tween m_navExpansion{1.0f};
    float m_navTarget = 1.0f;
    bool m_userCollapsed = false; // the user's choice; narrow windows collapse on top of it
    bool m_narrow = false;        // width < 1200 (layout-system.md breakpoint)
};

} // namespace wl::app
