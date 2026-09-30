#pragma once
// Application object: owns graphics, the main window, the widget host and the shell. Two modes:
//  - windowed (normal run): `WinLove.exe [source-path]` — a path (ISO/WIM/ESD/SWM/folder) opens
//    right away (file associations, "Open with", drag onto the exe)
//  - offscreen: WinLove.exe --render=<file.png> [options] draws one frame without a window,
//    for visual checks against WinLove-UI-Handoff/04_screens. Options:
//      --theme=dark|light|hc  --accent=copper|sea|pomegranate|sky|olive  --lang=tr|en  --scale=1.5  --size=1440x900
//      --page=<key>           (source, images, …, settings, about, gallery)
//      --nav-collapsed        --maximized (restore glyph)
//      --hover-at=x,y         --press-at=x,y   --tooltip-at=x,y   (DIPs; simulate the pointer)
//      --context-at=x,y       (right click: context menu)
//      --tab=N                (press Tab N times: keyboard focus ring)
//      --recent-file=<json>   (recent-sources file to show; default %LOCALAPPDATA%\WinLove\recent.json)
//      --dialog=admin         (open the administrator dialog, s4)
//      --drag=valid|invalid   (Source page drop zone drag state)
//      --mount=N              (windowed: after opening the source, mount edition N — UAC relaunch)
//      --select=N[,M…]        (select edition N on the Images page; more: marked as well)
//      --operation=mount|prepare|read|verify --progress=0.38   (render: show the operation strip)
//      --verified=sound|damaged   (render: the result of "Doğrula" on the Images page)
//      --demo-upgrade=dialog|queued   (render, with a source: the edition upgrade of the Images page)
//      --demo-<page>          (render: sample state for a page; see LaunchOptions)
//      --demo-catalog=dialog|download   (render, with --demo-updates: the update catalog, D-046)
//      --switch-lang=tr|en    (render: rebuild the UI in another language, as the settings page does)
//      --palette[=query]      (render: open the command palette, optionally with text typed)
//      --keys=down,enter,…    (render: press these keys in order — up down left right enter esc del tab)
#include "app/Localization.h"
#include "app/pages/PageInfo.h"
#include "app/shell/Shell.h"
#include "app/state/AppState.h"
#include "ui/platform/DropTarget.h"
#include "ui/platform/Window.h"
#include "ui/render/Graphics.h"
#include "ui/render/SwapChainTarget.h"
#include "ui/widget/Host.h"

#include <filesystem>
#include <optional>
#include <vector>
#include <span>
#include <string>

namespace wl::app {

struct LaunchOptions {
    ui::ThemeKind theme = ui::ThemeKind::Dark;
    Language language = Language::Turkish;
    bool themeGiven = false;    // --theme= / --lang= on the command line win over settings.json
    std::optional<ui::Accent> accent; // --accent=
    bool languageGiven = false;
    std::optional<std::filesystem::path> renderTo;
    float scale = 1.0f;
    ui::SizeF size{1440, 900};
    std::optional<PageId> page;
    bool navCollapsed = false;
    bool demoLogs = false;
    bool demoFeatures = false; // render: fake mount + screen 05 sample features
    bool demoComponents = false; // render: fake mount + sample provisioned apps (screen 04)
    bool demoRegistry = false;   // render: fake mount + checked tweaks (08)
    bool demoServices = false;   // render: fake mount + sample services (09)
    bool demoTasks = false;      // render: fake mount, two tasks off in the image, the recommended queued (D-048)
    std::optional<std::wstring> demoFiles; // render: fake mount + queued files ("where": the destination dialog) (D-051)
    bool demoHosts = false;
    bool demoLanguages = false; // render: fake mount, the image's languages, two packs and a display language queued (D-053)
    std::optional<std::wstring> demoApps; // render: fake mount + the lab's Terminal package ("defaults": second tab) (D-050)
    bool demoImageDrivers = false; // render: fake mount + the image's third-party drivers, one queued for removal (D-052)      // render: fake mount, telemetry list in the image, ads queued, imported entries (D-049)
    bool demoTweaks = false;     // render: fake mount + the selections of screen 10
    bool demoImageValues = false; // render (with --demo-tweaks / --demo-registry): the image already has a few (D-045)
    bool demoUnattended = false; // render: the answers of screen 11
    bool demoPostSetup = false;  // render: fake mount + the steps of screen 12
    bool demoPresets = false;    // render: an in-memory library like screen 17
    std::optional<Language> switchLanguage; // render: rebuild the UI in this language after the demo setup (P16)
    bool demoDrivers = false;    // render: fake mount + sample driver INFs (07)
    bool demoUpdates = false;    // render: with a source path — fake mount + sample update packages (06)
    std::wstring demoUsb;         // render (with a source): "" | "confirm" — the USB tab with a sample drive (D-047)
    bool demoUsbGiven = false;
    std::wstring demoCatalog;     // render (with --demo-updates): "dialog" | "download" — the update catalog (D-046)
    std::wstring demoApply;    // render (with --demo-features): "running" | "done" | "skipped" — fake Uygula run // render: fill the log with the design's sample lines (screen 18)
    bool maximized = false;
    std::optional<ui::PointF> hoverAt;
    std::optional<ui::PointF> contextAt; // --context-at=x,y: right click (context menu)
    std::optional<ui::PointF> pressAt;
    std::vector<ui::PointF> clickAt; // --click-at=x,y (repeatable): full click, e.g. open a dropdown
    std::optional<ui::PointF> tooltipAt;
    int tabPresses = 0;
    std::optional<std::filesystem::path> recentFile;
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
    // P16: settings.json changed (or Windows' own theme did): theme, motion, language.
    void applySettings();
    void rebuildUi(); // a new language: strings and every widget again; AppState stays
    [[nodiscard]] ui::HostServices windowHostServices();
    [[nodiscard]] Result<void> recreateGraphics();

    LaunchOptions m_options;
    std::unique_ptr<ui::Graphics> m_graphics;
    std::optional<Localization> m_strings;
    ui::Window m_window;
    // After m_window: destroyed first, so the engine thread's last posts still find the window.
    std::unique_ptr<AppState> m_state;
    ui::DropTarget* m_dropTarget = nullptr;
    std::unique_ptr<ui::SwapChainTarget> m_target;
    std::unique_ptr<ui::Host> m_host;
    Shell* m_shell = nullptr;
};

} // namespace wl::app
