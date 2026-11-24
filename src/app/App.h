#pragma once
// Application object: owns graphics, the main window, the widget host and the shell. Two modes:
//  - windowed (normal run): `WinLove.exe [source-path]` — a path (ISO/WIM/ESD/SWM/folder) opens
//    right away (file associations, "Open with", drag onto the exe)
//  - offscreen: WinLove.exe --render=<file.png> [options] draws one frame without a window,
//    for visual checks against WinLove-UI-Handoff/04_screens. Options:
//      --theme=dark|light|hc|black  --accent=blue|copper|sea|pomegranate|olive|#RRGGBB (D-143)  --lang=tr|en
//      --scale=1.5  --size=1440x900
//      --page=<key>           (source, images, …, settings, about, gallery)
//      --nav-collapsed        --maximized (restore glyph)
//      --hover-at=x,y         --press-at=x,y   --tooltip-at=x,y   (DIPs; simulate the pointer)
//      --click-at=x,y         (repeatable: a full click, e.g. open a dropdown or tick a row)
//      --context-at=x,y       (right click: context menu)
//      --tab=N                (press Tab N times: keyboard focus ring)
//      --recent-file=<json>   (recent-sources file to show; default %LOCALAPPDATA%\WinLove\recent.json)
//      --profile=<folder>     (windowed: recent.json, settings.json and answers.dat from this folder instead of
//                              %LOCALAPPDATA%\WinLove — test runs, tools\gui.py; its settings.json sets the work folder)
//      --dialog=admin         (open the administrator dialog, s4)
//      --drag=valid|invalid   (Source page drop zone drag state)
//      --mount=N              (windowed: after opening the source, mount edition N — UAC relaunch)
//      --no-elevate           (windowed: skip the elevated relaunch at startup; main.cpp)
//      --font=inter|geist|segoe   (the type family, as in the settings; D-092)
//      --test-device-lost     (windowed: the first frame takes the device-loss path a GPU reset takes)
//      --select=N[,M…]        (select edition N on the Images page; more: marked as well)
//      --operation=mount|prepare|read|verify|delete|export|rename|duplicate|move|unmount|prepare-delete --progress=0.38
//                             (render: the operation strip and the rows' Durum words; --select=N,M: the rows of a
//                              delete / export of several editions)
//      --verified=sound|damaged   (render: the result of "Doğrula" on the Images page)
//      --demo-upgrade=dialog|queued   (render, with a source: the edition upgrade of the Images page)
//      --demo-<page>          (render: sample state for a page; see LaunchOptions)
//      --demo-no-winre        (render, with a source: the ISO page as when an edition has no WinRE, D-074)
//      --demo-aio             (render, with a source: the ISO page as when the media cannot install an edition, D-077)
//      --demo-catalog=dialog|download   (render, with --demo-updates: the update catalog, D-046)
//      --demo-overview[=unread|running|paused|done|held]   (render, with or without a source: a mounted
//                             image with its lists read and a rich queue — Görüntü › Özet, or --page=apply; D-138)
//      --switch-lang=tr|en    (render: rebuild the UI in another language, as the settings page does)
//      --palette[=query]      (render: open the command palette, optionally with text typed)
//      --keys=down,enter,…    (render: press these keys in order — up down left right enter esc del tab)
//      --toast-timeout        (render: the toast's own timer fires before the frame — a demo's toast has gone, D-139)
//      --click-after-load=x,y (render: a click after the workers' results landed — a row of Programlar's index, D-139)
//      --settle=<ms>[,<quiet>] (render: wait up to that long for the workers — network included — before each
//                             click-after-load and the frame; done when nothing was posted for <quiet> ms, 3000)
//      --demo-vhd[=closed|confirm|mounted|unmount|done|iso]   (render: a VHDX source, D-141; nothing is attached)
//      --demo-update[=available|current|failed|checking]   (render: the update check's answer, D-140 — About's card;
//                             "available" also the notice: one toast and the status bar segment on any other page;
//                             D-145: downloading|verifying|ready|install-failed — "Yükle" under way / done / refused;
//                             updated — the instance a swap started, its "updated" toast and card)
//      --demo-changelog[=<releases.json>]   (render, D-145: the changelog window alone, from a saved GitHub answer;
//                             default tests\integration\fixtures\changelog-sample.json next to the build)
//      --auto-iso             (render, D-144: "Bitince ISO oluştur" on, ISO folder D:\ISO; with --demo-overview=iso-running|
//                             iso-done|iso-failed|iso-cancelled|iso-skipped the run's ISO in that state)
//      --iso-choices          (render, D-144 addendum: ISO Oluştur's options changed — UEFI only, label WINLOVE_TR, ESD,
//                             no key prompt, UEFI CA 2023 — on the ISO page and the Apply row alike)
//      --iso-options          (render, D-144 addendum: the Apply row's "ISO seçenekleri…" dialog open)
//      --updated-from=<version>   (windowed, D-145: started by a self-update — the old exe goes, one toast)
//      --keys=…               also pgdn, pgup, home, end
//      --anim-time=<ms>       (render, D-142: every progress ring shows its pose that many ms after it appeared —
//                             frames of the animation without a window; default: the still ring)
//      --demo-accent-picker   (render, D-143: Ayarlar with the accent colour picker open)
#include "app/ChangelogWindow.h"
#include "app/Localization.h"
#include "app/controllers/UpdateController.h"
#include "app/pages/PageInfo.h"
#include "app/shell/Shell.h"
#include "app/state/AppState.h"
#include "ui/platform/DropTarget.h"
#include "ui/platform/Window.h"
#include "ui/render/Graphics.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/render/SwapChainTarget.h"
#include "ui/widget/Host.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace wl::app {

struct LaunchOptions {
    ui::ThemeKind theme = ui::ThemeKind::Dark;
    Language language = Language::Turkish;
    bool themeGiven = false;    // --theme= / --lang= on the command line win over settings.json
    std::optional<UiFont> font; // --font=inter|geist|segoe (D-092); else settings.json
    std::optional<std::uint32_t> accent; // --accent= (0xRRGGBB, D-143)
    std::optional<double> animTime;      // --anim-time=<ms> (D-142)
    bool demoAccentPicker = false;       // --demo-accent-picker (D-143)
    bool languageGiven = false;
    std::optional<std::filesystem::path> renderTo;
    float scale = 1.0f;
    ui::SizeF size{1440, 900};
    bool sizeGiven = false; // --size=: the window opens at that size, not filling the screen
    std::optional<PageId> page;
    bool navCollapsed = false;
    bool demoLogs = false;     // render: fill the log with the design's sample lines (screen 18)
    bool demoFeatures = false; // render: fake mount + screen 05 sample features
    bool demoComponents = false; // render: fake mount + sample provisioned apps (screen 04)
    std::wstring demoInventory;  // D-112, with --demo-components: a `wlcli inventory --json` file's rows
    std::wstring demoReveal;     // D-113, with --demo-components: a component to open the tree at (its catalog id)
    std::wstring demoStoreShare; // D-113, with --demo-components: a `wlcli store-share` file (a real image's split)
    bool demoCompat = false;     // with --demo-components: the Uyumluluk dialog open (D-082)
    std::wstring demoToast;      // --demo-toast=title|message: a toast over the page (wrapping, 2026-11-09)
    bool toastTimeout = false;   // --toast-timeout: the toast's timer fires before the frame (D-139: the card's foot shows)
    bool demoAppMenu = false;    // --demo-app-menu: the ≡ menu open (its icons, 2026-11-09)
    bool demoRegistry = false;   // render: fake mount + typed values and an imported .reg (D-067)
    bool demoRegistryDialog = false; // --demo-registry=dialog: the first value open in "Değeri düzenle"
    bool demoServices = false;   // render: fake mount + sample services (09)
    bool demoTasks = false;      // render: fake mount, two tasks off in the image, the recommended queued (D-048)
    bool demoTaskCreate = false; // render: the "Görev oluştur" dialog open on the Tasks page (D-102)
    std::optional<std::wstring> demoFiles; // render: fake mount + queued files ("where": the destination dialog) (D-051)
    int demoDownload = -1;     // render: D-093 Windows indir — 0 list, 1 a build picked, 2 downloading, 3 converting,
                               // 4 the app picker, 5 a Windows 10 build (apps in the image), 6 the last run
                               // failed, 7 the ISO came with warnings, 8 with a hotpatch LCU's baseline;
                               // D-149 (official ESD): 9 esd (chosen), 10 uup-only (not in the catalog),
                               // 11 esd-info ("Fark ne?" open), 12 esd-downloading, 13 esd-verifying,
                               // 14 esd-converting, 15 esd-failed
    bool demoHosts = false;    // render: fake mount, telemetry list in the image, ads queued, imported entries (D-049)
    bool demoBranding = false; // render: Kişiselleştirme with OEM text, pictures and fonts queued (D-056)
    std::wstring demoTool; // render, with a source: recompress | split | duplicate | capture | hash | append (D-058)
    bool demoWifi = false; // render: Kurulum Sonrası › Wi-Fi ağı ekle dialog (D-056)
    // render: Sürücüler › Kurulum ortamı (D-056, D-134). "" (with --demo-drivers): a selection, the medium's
    // boot.wim packages, an ARM64 row dimmed; "host": "Bu bilgisayardan al" from that tab (storage + wired
    // network ticked); "storage-only" (with --demo-drivers): a storage driver ticked for boot.wim only (the
    // warning); "iso": the ISO page with boot drivers, one storage driver only in boot.wim; "iso-done": the
    // ISO built, two drivers refused; "iso-running" (D-142): the build under way (its progress ring).
    std::optional<std::wstring> demoBootDrivers;
    bool demoEditions = false; // render, with a source: Apply summary with two other editions ticked (D-055)
    // render: fake 25H2 Pro mount with Turkish, English queued from Windows Update and a display language (D-053 / D-061);
    // "dialog": the "Dil ekle" check list; "fetch": a download running.
    std::optional<std::wstring> demoLanguages;
    std::optional<std::wstring> demoStore; // render: "dialog" (search results) | "fetch" (a download on Uygulamalar), D-066
    bool demoStartMenu = false;         // render: this PC as the image, a custom Start pin list (D-069)
    bool demoStartMenuSettings = false; // --demo-startmenu=settings: the "Başlat ayarları" tab
    bool demoTaskbar = false;           // --demo-startmenu=taskbar: the "Görev çubuğu" tab with a list (D-083)
    bool demoIconsRedirect = false; // --demo-icons=redirect: the "Masaüstü ve Gezgin" tab
    bool demoIcons = false; // render: the Simgeler page with a few icons assigned (D-065)
    bool demoMounts = false; // render: images mounted on this PC by other tools, on the Kaynak page (D-064)
    // 2026-11-11, --demo-mounts=<variant>: taking one over — "adopting" | "remounting" (the work on Kaynak),
    // "failed" (DISM marks it invalid: the reason, nothing touched), "adopted" (Sürümler: whose folder it is),
    // "overview" (Özet), "unmount" (the unmount question for another tool's folder). "" = the list.
    std::wstring demoMountsVariant;
    std::optional<std::wstring> demoApps; // render: fake mount + the lab's Terminal package ("defaults": second tab) (D-050)
    bool demoImageDrivers = false; // render: fake mount + the image's third-party drivers, one queued for removal (D-052)
    bool demoTweaks = false;     // render: fake mount + the selections of screen 10
    bool demoImageValues = false; // render (with --demo-tweaks): the image already has a few (D-045)
    // D-146, with --demo-tweaks: --demo-tweak-template=a,b applies these templates in order (as Şablon ▾ does:
    // the toast of the last one stays); --demo-tweak-filter=id shows one in the filter ("Şablon (…)").
    std::vector<std::string> demoTweakTemplates;
    std::string demoTweakFilter;
    bool demoUnattended = false; // render: the answers of screen 11
    bool demoWelcome = false;    // --demo-unattended=welcome: with WinLove's welcome on (D-084)
    bool demoPostSetup = false;  // render: fake mount + the steps of screen 12
    bool demoPresets = false;    // render: an in-memory library like screen 17
    std::optional<Language> switchLanguage; // render: rebuild the UI in this language after the demo setup (P16)
    bool demoDrivers = false;    // render: fake mount + sample driver INFs (07)
    bool demoUpdates = false;    // render: with a source path — fake mount + sample update packages (06)
    std::wstring demoUsb;         // render (with a source): "" | "confirm" — the USB tab with a sample drive (D-047)
    bool demoUsbGiven = false;
    bool demoMediaUpdate = false; // render (with a source): ISO page with downloaded media updates (D-080)
    bool demoHostDism = false;    // render: Source page warns that this PC's DISM is not whole (D-081)
    bool demoNoWinre = false;     // render (with a source): ISO page, edition 1 has no WinRE (D-074)
    bool demoAio = false;         // render (with a source): ISO page, edition 2 cannot be installed by the media (D-077)
    std::wstring demoCatalog;     // render (with --demo-updates): "dialog" | "download" — the update catalog (D-046)
    // render (D-138): "ready" — the source's first edition (or a sample) mounted, every list read, a queue
    // across the navigation; "unread" — mounted, the lists being read; "running" / "paused" / "done" / "held" —
    // that queue's Uygula run in that state. Shown on Özet (or --page=).
    std::optional<std::wstring> demoOverview;
    std::optional<std::wstring> demoUpdate; // D-140: "available" | "current" | "failed" | "checking"; D-145: below
    std::optional<std::wstring> demoChangelog; // D-145: a saved releases answer ("": the test fixture)
    bool autoIso = false;                      // D-144 (render): "Bitince ISO oluştur" on
    bool isoChoices = false;                   // D-144 addendum (render): the ISO options changed
    bool isoOptions = false;                   // D-144 addendum (render): their dialog open
    std::wstring updatedFrom;                  // D-145 (windowed): the version a self-update replaced
    // D-141: a VHDX source (made up, nothing is attached): "closed" (Sürümler) | "confirm" (the mount question) |
    // "mounted" (Özet, lists read) | "unmount" (the unmount question) | "done" (a saved run) | "iso" (ISO Oluştur).
    std::optional<std::wstring> demoVhd;
    std::wstring demoApply;    // render (with --demo-features): "running" | "paused" | "done" | "skipped" | "held" — fake Uygula run; "confirm" — the risk dialog, 40 rows
                               // 2026-11-11: "stopped" (stopped half way, still mounted), then a save / discard of it started
                               // from anywhere: "discarding" | "saving" (running, 40 %), "discarded" | "saved" (done), "closefailed"
    bool maximized = false;
    bool testDeviceLost = false; // windowed: the first frame is treated as a lost device (audit A6)
    std::optional<ui::PointF> hoverAt;
    std::optional<ui::PointF> contextAt; // --context-at=x,y: right click (context menu)
    std::optional<ui::PointF> pressAt;
    std::vector<ui::PointF> clickAt; // --click-at=x,y (repeatable): full click, e.g. open a dropdown
    std::vector<ui::PointF> clickAfterLoad; // --click-after-load=x,y: the same, after the workers' results (D-139)
    int settleMs = 0;                       // --settle=<ms>[,<quiet ms>]: render waits for the workers (live pages)
    int settleQuietMs = 3000;               // …until nothing was posted for this long
    std::optional<ui::PointF> tooltipAt;
    int tabPresses = 0;
    std::optional<std::filesystem::path> recentFile;
    std::optional<std::filesystem::path> profile; // --profile=<folder>: recent / settings / answers of a test run
    bool adminDialog = false;
    std::optional<bool> dragValid;
    std::optional<std::filesystem::path> openPath; // positional argument
    std::optional<int> mountIndex;
    std::optional<int> selectIndex;
    std::vector<int> selectMarked; // --select=4,2,3: every edition named (the first is selectIndex)
    std::optional<std::wstring> fakeOperation; // render only
    std::optional<std::wstring> verified;      // render only: --verified=sound|damaged, what "Doğrula" found
    // render only, with a source path: the selected edition mounted, as Home with the usual targets.
    // "dialog": the "Sürümü yükselt…" dialog open; "queued": Pro already in the queue.
    std::optional<std::wstring> demoUpgrade;
    std::optional<std::wstring> palette;       // render only: the command palette with this query
    std::vector<UINT> keys;                    // render only: virtual keys pressed after the setup
    float fakeProgress = 0.38f;
};

[[nodiscard]] Result<LaunchOptions> parseLaunchOptions(std::span<const std::wstring> args);

class App {
public:
    explicit App(LaunchOptions options) : m_options(std::move(options)) {}
    ~App();
    [[nodiscard]] int run();

private:
    bool m_forceClose = false; // graphics could not be recreated: close without asking
    [[nodiscard]] Result<void> initialize();
    void buildUi(ui::HostServices services);
    [[nodiscard]] int runWindowed();
    [[nodiscard]] int renderOffscreen();
    void paint();
    void applyTheme();
    void demoOverview(); // --demo-overview (D-138)
    // The same over `imagePath` mounted on `mountDir` (D-141: a VHD's demo uses it too).
    void demoOverview(const std::wstring& variant, const std::filesystem::path& imagePath, const std::filesystem::path& mountDir);
    void demoVhd();      // --demo-vhd (D-141)
    void demoAdoption(const std::wstring& variant); // --demo-mounts=<variant> (2026-11-11)
    void demoRunClose(const std::wstring& variant); // --demo-apply=stopped|discarding|… (2026-11-11)
    void demoUpdate();   // --demo-update (D-140)
    [[nodiscard]] int renderChangelog(ui::OffscreenTarget& target); // --demo-changelog (D-145)
    void demoAutoIso(AppState::ApplyRun& run, const std::wstring& variant); // --demo-overview=iso-* (D-144)
    void openChangelog(); // D-145: the changelog window (one; brought to the front when open)
    void removeOldExe();  // D-145: "<exe>.old" a self-update left, once the old process is gone
    static constexpr UINT kOldExeTimer = 10;
    int m_oldExeTries = 0;
    static constexpr UINT kUpdateCheckTimer = 9;       // D-140: the startup check, once
    static constexpr UINT kUpdateCheckDelayMs = 4000;
    // P16: settings.json changed (or Windows' own theme did): theme, motion, language.
    void applySettings();
    void rebuildUi(bool newFamilies = false); // a new language: strings and every widget again; AppState stays
    // Keeps the page and the nav state in m_options, then destroys the widget tree (it measures with
    // the current graphics / strings, which the caller is about to replace).
    void releaseShell();
    [[nodiscard]] ui::HostServices windowHostServices();
    [[nodiscard]] Result<void> recreateGraphics();
    // Headless: functions posted from another thread wait here and run on the main thread before
    // the frame is drawn (a worker must never touch widgets the main thread is laying out).
    void post(std::function<void()> fn);
    void drainPosted();

    LaunchOptions m_options;
    std::optional<core::ImageInventory> m_demoInventory; // --demo-inventory, until the demo mount takes it
    std::unique_ptr<ui::Graphics> m_graphics;
    std::optional<Localization> m_strings;
    ui::Window m_window;
    // Before m_state: its worker threads may still post while it shuts down.
    const std::thread::id m_mainThread = std::this_thread::get_id();
    std::mutex m_postedMutex;
    std::vector<std::function<void()>> m_posted;
    // After m_window: destroyed first, so the engine thread's last posts still find the window.
    std::unique_ptr<AppState> m_state;
    // D-140, windowed only (renders and tests never check): after m_state, so it goes first.
    std::unique_ptr<UpdateController> m_updates;
    // D-145: after m_state (it listens to it) and m_graphics (its text styles): it goes before them.
    std::unique_ptr<ChangelogWindow> m_changelog;
    ui::DropTarget* m_dropTarget = nullptr;
    std::unique_ptr<ui::SwapChainTarget> m_target;
    std::unique_ptr<ui::Host> m_host;
    Shell* m_shell = nullptr;
    bool m_simulateDeviceLoss = false; // --test-device-lost: the next frame takes the GPU-reset path
    std::uint32_t m_accentShown = ui::kDefaultAccent; // D-143: the saved accent the window's frame shows
};

} // namespace wl::app
