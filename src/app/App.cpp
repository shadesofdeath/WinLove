#include "app/App.h"

#include "app/Resources.h"
#include "ui/render/Canvas.h"
#include "ui/render/OffscreenTarget.h"

#include <cwchar>
#include <string_view>

namespace wl::app {

namespace {

using ui::tokens::Color;

constexpr std::int32_t kDeviceRemoved = static_cast<std::int32_t>(0x887A0005); // DXGI_ERROR_DEVICE_REMOVED
constexpr std::int32_t kDeviceReset = static_cast<std::int32_t>(0x887A0007);   // DXGI_ERROR_DEVICE_RESET

COLORREF colorRef(ui::ThemeKind theme, Color token) {
    const std::uint32_t argb = ui::colorArgb(theme, token);
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

bool startsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// Reports to the parent console when started from a terminal (WinLove.exe is a GUI program).
void report(std::wstring_view text) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        DWORD written = 0;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written,
                      nullptr);
        FreeConsole();
    }
}

// Headless runs (--render) must never block on a dialog: tests and AI sessions drive them.
bool g_headless = false;

void showError(const Error& error) {
    const std::wstring text = L"WinLove error: " + describe(error);
    report(text + L"\n");
    if (!g_headless) {
        MessageBoxW(nullptr, text.c_str(), L"WinLove", MB_OK | MB_ICONERROR);
    }
}

} // namespace

Result<LaunchOptions> parseLaunchOptions(std::span<const std::wstring> args) {
    LaunchOptions options;
    for (const auto& arg : args) {
        const std::wstring_view a = arg;
        const auto value = [&](std::wstring_view prefix) { return a.substr(prefix.size()); };
        if (startsWith(a, L"--render=")) {
            options.renderTo = std::filesystem::path(value(L"--render="));
        } else if (startsWith(a, L"--theme=")) {
            const auto v = value(L"--theme=");
            if (v == L"dark") options.theme = ui::ThemeKind::Dark;
            else if (v == L"light") options.theme = ui::ThemeKind::Light;
            else if (v == L"hc") options.theme = ui::ThemeKind::HighContrast;
            else return fail(ErrorCode::InvalidArgument, L"--theme must be dark|light|hc", arg);
        } else if (startsWith(a, L"--lang=")) {
            const auto v = value(L"--lang=");
            if (v == L"tr") options.language = Language::Turkish;
            else if (v == L"en") options.language = Language::English;
            else return fail(ErrorCode::InvalidArgument, L"--lang must be tr|en", arg);
        } else if (startsWith(a, L"--scale=")) {
            options.scale = std::wcstof(std::wstring(value(L"--scale=")).c_str(), nullptr);
            if (options.scale < 0.5f || options.scale > 4.0f) {
                return fail(ErrorCode::InvalidArgument, L"--scale must be between 0.5 and 4", arg);
            }
        } else if (startsWith(a, L"--size=")) {
            float w = 0;
            float h = 0;
            if (swscanf_s(std::wstring(value(L"--size=")).c_str(), L"%fx%f", &w, &h) != 2 || w < 100 || h < 100) {
                return fail(ErrorCode::InvalidArgument, L"--size must look like 1440x900", arg);
            }
            options.size = {w, h};
        } else if (startsWith(a, L"--hover=")) {
            const auto v = value(L"--hover=");
            if (v == L"palette") options.hover = TitleBar::Part::Palette;
            else if (v == L"min") options.hover = TitleBar::Part::Minimize;
            else if (v == L"max") options.hover = TitleBar::Part::Maximize;
            else if (v == L"close") options.hover = TitleBar::Part::Close;
            else return fail(ErrorCode::InvalidArgument, L"--hover must be palette|min|max|close", arg);
        } else if (a == L"--maximized") {
            options.maximized = true;
        } else {
            return fail(ErrorCode::InvalidArgument, L"unknown argument", arg);
        }
    }
    return options;
}

int App::run() {
    g_headless = m_options.renderTo.has_value();
    if (auto ready = initialize(); !ready) {
        showError(ready.error());
        return 1;
    }
    return m_options.renderTo ? renderOffscreen() : runWindowed();
}

Result<void> App::initialize() {
    auto graphics = ui::Graphics::create(embeddedFonts());
    if (!graphics) {
        return std::unexpected(graphics.error());
    }
    m_graphics = std::move(*graphics);

    auto strings = embeddedStrings(m_options.language);
    if (!strings) {
        return std::unexpected(strings.error());
    }
    m_strings = std::move(*strings);

    m_titleBar.setLabels({m_strings->get(Str::AppName), m_strings->get(Str::TitleCmdk), m_strings->get(Str::KbdCtrl)});
    return {};
}

void App::paintFrame(ui::Canvas& canvas, ui::SizeF size) {
    canvas.clear(Color::BgBase);
    m_titleBar.layout(size.width);
    m_titleBar.paint(canvas);
}

// ---- offscreen -----------------------------------------------------------------------------

int App::renderOffscreen() {
    auto target = ui::OffscreenTarget::create(*m_graphics->device, m_options.size, m_options.scale);
    if (!target) {
        showError(target.error());
        return 1;
    }
    m_titleBar.layout(m_options.size.width);
    m_titleBar.setMaximized(m_options.maximized);
    if (m_options.hover) {
        // Drive the real pointer path so the render shows exactly what a hover would.
        const auto zone = *m_options.hover == TitleBar::Part::Minimize ? ui::HitZone::MinimizeButton
                          : *m_options.hover == TitleBar::Part::Maximize ? ui::HitZone::MaximizeButton
                          : *m_options.hover == TitleBar::Part::Close    ? ui::HitZone::CloseButton
                                                                           : ui::HitZone::Client;
        const float button = ui::tokens::size::captionButton;
        const float w = m_options.size.width;
        const ui::PointF at = *m_options.hover == TitleBar::Part::Palette ? ui::PointF{w / 2, 16}
                              : *m_options.hover == TitleBar::Part::Minimize ? ui::PointF{w - 2.5f * button, 16}
                              : *m_options.hover == TitleBar::Part::Maximize ? ui::PointF{w - 1.5f * button, 16}
                                                                               : ui::PointF{w - 0.5f * button, 16};
        TitleBar::Action ignored{};
        m_titleBar.onPointer({ui::PointerAction::Move, at, zone}, ignored);
    }

    ui::Canvas canvas((*target)->beginDraw(), m_options.theme, m_options.scale, *m_graphics->text, *m_graphics->icons);
    paintFrame(canvas, m_options.size);
    if (auto done = (*target)->endDraw(); !done) {
        showError(done.error());
        return 1;
    }
    if (auto saved = (*target)->savePng(*m_options.renderTo); !saved) {
        showError(saved.error());
        return 1;
    }
    report(L"rendered " + m_options.renderTo->wstring() + L"\n");
    return 0;
}

// ---- windowed ------------------------------------------------------------------------------

int App::runWindowed() {
    ui::WindowCallbacks callbacks;
    callbacks.paint = [this] { paint(); };
    callbacks.resized = [this](ui::SizeF size, float scale) {
        if (m_target) {
            if (auto resized = m_target->resize(m_window.clientWidthPx(), m_window.clientHeightPx(), scale * 96.0f);
                !resized) {
                showError(resized.error());
            }
        }
        m_titleBar.layout(size.width);
        paint(); // draw right away: keeps live resizing smooth
    };
    callbacks.hitTest = [this](ui::PointF p) { return m_titleBar.hitTest(p); };
    callbacks.pointer = [this](const ui::PointerEvent& event) {
        TitleBar::Action action{};
        if (m_titleBar.onPointer(event, action)) {
            m_window.invalidate();
        }
        switch (action) {
        case TitleBar::Action::Minimize: m_window.minimize(); break;
        case TitleBar::Action::ToggleMaximize: m_window.toggleMaximize(); break;
        case TitleBar::Action::Close: m_window.close(); break;
        case TitleBar::Action::OpenPalette: // command palette arrives with P18
        case TitleBar::Action::None: break;
        }
    };
    callbacks.activated = [this](bool active) {
        m_titleBar.setActive(active);
        m_window.invalidate();
    };
    callbacks.maximizedChanged = [this](bool maximized) {
        m_titleBar.setMaximized(maximized);
        m_window.invalidate();
    };
    callbacks.keyDown = [this](const ui::KeyEvent& key) {
        if (key.ctrl && key.shift && key.virtualKey == 'T') { // interaction.md: Ctrl Shift T = theme
            m_options.theme = m_options.theme == ui::ThemeKind::Dark ? ui::ThemeKind::Light : ui::ThemeKind::Dark;
            applyTheme();
        }
    };

    const ui::WindowAppearance appearance{appIcon(), m_options.theme != ui::ThemeKind::Light,
                                          colorRef(m_options.theme, Color::LineStrong),
                                          colorRef(m_options.theme, Color::BgBase)};
    const ui::SizeF minimum{ui::tokens::size::minWindowW, ui::tokens::size::minWindowH};
    if (auto created = m_window.create(L"WinLove", m_options.size, minimum, std::move(callbacks), appearance);
        !created) {
        showError(created.error());
        return 1;
    }
    auto target = ui::SwapChainTarget::create(*m_graphics->device, m_window.hwnd(), m_window.clientWidthPx(),
                                              m_window.clientHeightPx(), m_window.scale() * 96.0f);
    if (!target) {
        showError(target.error());
        return 1;
    }
    m_target = std::move(*target);
    m_titleBar.layout(m_window.clientSize().width);
    m_window.show();
    return ui::Window::runMessageLoop();
}

void App::applyTheme() {
    m_window.setFrameColors(m_options.theme != ui::ThemeKind::Light, colorRef(m_options.theme, Color::LineStrong));
    m_window.invalidate();
}

void App::paint() {
    if (!m_target) {
        return;
    }
    ui::Canvas canvas(m_target->beginDraw(), m_options.theme, m_window.scale(), *m_graphics->text,
                      *m_graphics->icons);
    paintFrame(canvas, m_window.clientSize());
    auto presented = m_target->endDrawAndPresent();
    if (!presented) {
        const auto hr = presented.error().hresult;
        if (hr == kDeviceRemoved || hr == kDeviceReset) {
            if (auto rebuilt = recreateGraphics(); !rebuilt) {
                showError(rebuilt.error());
                m_window.close();
                return;
            }
            m_window.invalidate();
        }
    }
}

Result<void> App::recreateGraphics() {
    m_target.reset();
    auto graphics = ui::Graphics::create(embeddedFonts());
    if (!graphics) {
        return std::unexpected(graphics.error());
    }
    m_graphics = std::move(*graphics);
    auto target = ui::SwapChainTarget::create(*m_graphics->device, m_window.hwnd(), m_window.clientWidthPx(),
                                              m_window.clientHeightPx(), m_window.scale() * 96.0f);
    if (!target) {
        return std::unexpected(target.error());
    }
    m_target = std::move(*target);
    return {};
}

} // namespace wl::app
