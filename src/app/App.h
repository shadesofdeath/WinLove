#pragma once
// Application object: owns graphics, the main window and the shell. Two modes:
//  - windowed (normal run)
//  - offscreen: `WinLove.exe --render=<file.png> [--theme=dark|light|hc] [--scale=1.5]
//               [--size=1440x900] [--lang=tr|en] [--hover=palette|min|max|close] [--maximized]`
//    draws one frame without a window — for visual checks against WinLove-UI-Handoff/04_screens.
#include "app/Localization.h"
#include "app/shell/TitleBar.h"
#include "ui/platform/Window.h"
#include "ui/render/Graphics.h"
#include "ui/render/SwapChainTarget.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace wl::app {

struct LaunchOptions {
    ui::ThemeKind theme = ui::ThemeKind::Dark;
    Language language = Language::Turkish;
    std::optional<std::filesystem::path> renderTo;
    float scale = 1.0f;
    ui::SizeF size{1440, 900};
    std::optional<TitleBar::Part> hover; // offscreen only: simulate hover for state checks
    bool maximized = false;              // offscreen only: draw the restore glyph
};

[[nodiscard]] Result<LaunchOptions> parseLaunchOptions(std::span<const std::wstring> args);

class App {
public:
    explicit App(LaunchOptions options) : m_options(std::move(options)) {}
    [[nodiscard]] int run();

private:
    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] int runWindowed();
    [[nodiscard]] int renderOffscreen();
    void paint();
    void paintFrame(ui::Canvas& canvas, ui::SizeF size);
    void applyTheme();
    [[nodiscard]] Result<void> recreateGraphics();

    LaunchOptions m_options;
    std::unique_ptr<ui::Graphics> m_graphics;
    std::optional<Localization> m_strings;
    ui::Window m_window;
    std::unique_ptr<ui::SwapChainTarget> m_target;
    TitleBar m_titleBar;
};

} // namespace wl::app
