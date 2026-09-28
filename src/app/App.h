#pragma once
// Application object: owns graphics, the main window, the widget host and the shell. Two modes:
//  - windowed (normal run): `WinLove.exe [source-path]` — a path (ISO/WIM/ESD/SWM/folder) opens
//    right away (file associations, "Open with", drag onto the exe)
//  - offscreen: WinLove.exe --render=<file.png> [options] draws one frame without a window,
//    for visual checks against WinLove-UI-Handoff/04_screens. Options:
//      --theme=dark|light|hc  --lang=tr|en  --scale=1.5  --size=1440x900
//      --page=<key>           (source, images, …, settings, about, gallery)
//      --nav-collapsed        --maximized (restore glyph)
//      --hover-at=x,y         --press-at=x,y   --tooltip-at=x,y   (DIPs; simulate the pointer)
//      --tab=N                (press Tab N times: keyboard focus ring)
//      --recent-file=<json>   (recent-sources file to show; default %LOCALAPPDATA%\WinLove\recent.json)
//      --dialog=admin         (open the administrator dialog, s4)
//      --drag=valid|invalid   (Source page drop zone drag state)
//      --mount=N              (windowed: after opening the source, mount edition N — UAC relaunch)
//      --select=N             (select edition N on the Images page)
//      --operation=mount|prepare --progress=0.38   (render: show the operation strip)
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
    std::optional<std::filesystem::path> renderTo;
    float scale = 1.0f;
    ui::SizeF size{1440, 900};
    std::optional<PageId> page;
    bool navCollapsed = false;
    bool demoLogs = false;
    bool demoFeatures = false; // render: fake mount + screen 05 sample features
    bool demoComponents = false; // render: fake mount + sample provisioned apps (screen 04)
    std::wstring demoApply;    // render (with --demo-features): "running" | "done" — fake Uygula run // render: fill the log with the design's sample lines (screen 18)
    bool maximized = false;
    std::optional<ui::PointF> hoverAt;
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
    std::optional<std::wstring> fakeOperation; // render only
    float fakeProgress = 0.38f;
};

[[nodiscard]] Result<LaunchOptions> parseLaunchOptions(std::span<const std::wstring> args);

class App {
public:
    explicit App(LaunchOptions options) : m_options(std::move(options)) {}
    ~App();
    [[nodiscard]] int run();

private:
    [[nodiscard]] Result<void> initialize();
    void buildUi(ui::HostServices services);
    [[nodiscard]] int runWindowed();
    [[nodiscard]] int renderOffscreen();
    void paint();
    void applyTheme();
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
