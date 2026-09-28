#include "app/App.h"

#include "base/Log.h"

#include "app/Format.h"
#include "app/Resources.h"
#include "base/Utf8.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"
#include "ui/render/Canvas.h"
#include "ui/render/OffscreenTarget.h"

#include <cwchar>
#include <string_view>

namespace wl::app {

namespace {

using ui::tokens::Color;

constexpr std::int32_t kDeviceRemoved = static_cast<std::int32_t>(0x887A0005); // DXGI_ERROR_DEVICE_REMOVED
constexpr std::int32_t kDeviceReset = static_cast<std::int32_t>(0x887A0007);   // DXGI_ERROR_DEVICE_RESET

// Headless runs (--render) must never block on a dialog: tests and AI sessions drive them.
bool g_headless = false;

COLORREF colorRef(ui::ThemeKind theme, Color token) {
    const std::uint32_t argb = ui::colorArgb(theme, token);
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

bool startsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// Reports to redirected stdout (tools, pipes) or the parent console (terminal). WinLove.exe is a
// GUI program, so neither exists by default.
void report(std::wstring_view text) {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out && out != INVALID_HANDLE_VALUE && !GetConsoleMode(out, &mode)) {
        const std::string utf8 = utf8::fromWide(text);
        DWORD written = 0;
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        DWORD written = 0;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written,
                      nullptr);
        FreeConsole();
    }
}

void showError(const Error& error) {
    const std::wstring text = L"WinLove error: " + describe(error);
    report(text + L"\n");
    if (!g_headless) {
        MessageBoxW(nullptr, text.c_str(), L"WinLove", MB_OK | MB_ICONERROR);
    }
}

std::optional<ui::PointF> parsePoint(std::wstring_view value) {
    float x = 0;
    float y = 0;
    if (swscanf_s(std::wstring(value).c_str(), L"%f,%f", &x, &y) != 2) {
        return std::nullopt;
    }
    return ui::PointF{x, y};
}

} // namespace

Result<LaunchOptions> parseLaunchOptions(std::span<const std::wstring> args) {
    LaunchOptions options;
    for (const auto& arg : args) {
        const std::wstring_view a = arg;
        auto value = [&](std::wstring_view prefix) { return a.substr(prefix.size()); };
        auto point = [&](std::wstring_view prefix, std::optional<ui::PointF>& out) -> Result<void> {
            out = parsePoint(value(prefix));
            if (!out) {
                return fail(ErrorCode::InvalidArgument, L"expected x,y in DIPs", arg);
            }
            return {};
        };
        Result<void> ok;
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
        } else if (startsWith(a, L"--page=")) {
            const auto v = value(L"--page=");
            std::string key;
            for (const wchar_t c : v) {
                key.push_back(static_cast<char>(c));
            }
            options.page = pageFromKey(key);
            if (!options.page) {
                return fail(ErrorCode::InvalidArgument, L"unknown page key", arg);
            }
        } else if (a == L"--no-elevate") {
            // handled in main.cpp (skip the startup UAC relaunch)
        } else if (a == L"--demo-features") {
            options.demoFeatures = true;
        } else if (a == L"--demo-logs") {
            options.demoLogs = true;
        } else if (a == L"--nav-collapsed") {
            options.navCollapsed = true;
        } else if (a == L"--maximized") {
            options.maximized = true;
        } else if (startsWith(a, L"--hover-at=")) {
            ok = point(L"--hover-at=", options.hoverAt);
        } else if (startsWith(a, L"--click-at=")) {
            std::optional<ui::PointF> p;
            ok = point(L"--click-at=", p);
            if (p) {
                options.clickAt.push_back(*p);
            }
        } else if (startsWith(a, L"--press-at=")) {
            ok = point(L"--press-at=", options.pressAt);
        } else if (startsWith(a, L"--tooltip-at=")) {
            ok = point(L"--tooltip-at=", options.tooltipAt);
        } else if (startsWith(a, L"--recent-file=")) {
            options.recentFile = std::filesystem::path(value(L"--recent-file="));
        } else if (a == L"--dialog=admin") {
            options.adminDialog = true;
        } else if (startsWith(a, L"--drag=")) {
            options.dragValid = value(L"--drag=") == L"valid";
        } else if (startsWith(a, L"--mount=")) {
            options.mountIndex = static_cast<int>(std::wcstol(std::wstring(value(L"--mount=")).c_str(), nullptr, 10));
        } else if (startsWith(a, L"--select=")) {
            options.selectIndex = static_cast<int>(std::wcstol(std::wstring(value(L"--select=")).c_str(), nullptr, 10));
        } else if (startsWith(a, L"--operation=")) {
            options.fakeOperation = std::wstring(value(L"--operation="));
        } else if (startsWith(a, L"--progress=")) {
            options.fakeProgress = std::wcstof(std::wstring(value(L"--progress=")).c_str(), nullptr);
        } else if (startsWith(a, L"--tab=")) {
            options.tabPresses = static_cast<int>(std::wcstol(std::wstring(value(L"--tab=")).c_str(), nullptr, 10));
        } else if (!a.starts_with(L"--") && !options.openPath) {
            options.openPath = std::filesystem::path(a);
        } else {
            return fail(ErrorCode::InvalidArgument, L"unknown argument", arg);
        }
        if (!ok) {
            return std::unexpected(ok.error());
        }
    }
    return options;
}

App::~App() {
    // Widgets reference graphics-owned text styles: destroy the tree first.
    m_host.reset();
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
    // Headless renders (tests, AI sessions) never touch the user's recent list or settings: they
    // get throw-away files unless a fixture is passed with --recent-file.
    const bool render = m_options.renderTo.has_value();
    const auto scratch = std::filesystem::temp_directory_path() / L"WinLove-render";
    m_state = std::make_unique<AppState>(
        m_options.recentFile.value_or(render ? scratch / L"recent.json" : RecentSources::defaultFile()),
        render ? scratch / L"settings.json" : AppSettings::defaultFile());
    return {};
}

void App::buildUi(ui::HostServices services) {
    services.text = m_graphics->text.get();
    m_graphics->text->setLocale(localeName(m_options.language));
    m_host = std::make_unique<ui::Host>(std::move(services));
    Shell::Services shellServices;
    shellServices.minimize = [this] { m_window.minimize(); };
    shellServices.toggleMaximize = [this] { m_window.toggleMaximize(); };
    shellServices.close = [this] { m_window.close(); };
    shellServices.toggleTheme = [this] {
        m_options.theme = m_options.theme == ui::ThemeKind::Dark ? ui::ThemeKind::Light : ui::ThemeKind::Dark;
        applyTheme();
    };
    shellServices.postToUi = [this](std::function<void()> fn) {
        if (m_options.renderTo) {
            fn(); // headless: no message loop
        } else {
            m_window.post(std::move(fn));
        }
    };
    shellServices.ownerWindow = [this] { return m_window.hwnd(); };
    shellServices.relaunchElevated = [this](const std::wstring& args) {
        auto relaunched = core::relaunchElevated(args);
        if (!relaunched && relaunched.error().code != ErrorCode::Cancelled) {
            showError(relaunched.error());
        }
        return relaunched.has_value();
    };
    shellServices.startTimer = [this](UINT id, UINT ms) {
        if (!m_options.renderTo) {
            m_window.setTimer(id, ms);
        }
    };
    shellServices.stopTimer = [this](UINT id) {
        if (!m_options.renderTo) {
            m_window.stopTimer(id);
        }
    };
    const Language language = m_options.language;
    auto shell = std::make_unique<Shell>(*m_strings, language, *m_state, std::move(shellServices));
    m_shell = shell.get();
    m_host->setRoot(std::move(shell));
    if (m_options.page) {
        m_shell->showPage(*m_options.page);
    }
    m_shell->setNavCollapsed(m_options.navCollapsed, /*animated=*/false);
    m_shell->titleBar().setMaximized(m_options.maximized);
}

// ---- offscreen -----------------------------------------------------------------------------

int App::renderOffscreen() {
    auto target = ui::OffscreenTarget::create(*m_graphics->device, m_options.size, m_options.scale);
    if (!target) {
        showError(target.error());
        return 1;
    }
    ui::forceInstantMotion(true);
    if (m_options.demoLogs) {
        // Screen 18 sample (render only: the lines go to this process's buffer, nowhere else).
        log::setMinimumLevel(log::Level::Debug);
        log::info("core", L"Oturum başladı · WinLove 0.1.0 · DISM 10.0.26100.1");
        log::info("mount", L"Mount-Image /ImageFile:install.wim /Index:2 → C:\\WinLove\\mount");
        log::info("appx", L"Remove-ProvisionedAppxPackage Microsoft.XboxGamingOverlay … ok");
        log::warn("appx", L"Microsoft.OneNote bağımlılığı: Microsoft.Office.Desktop — atlanıyor");
        log::info("pkg", L"Remove-Package Windows-Defender-Client-Package …");
        log::info("pkg", L"↳ SmartScreen bileşenleri (3)");
        log::error("reg", L"HKLM\\SOFTWARE\\Policies\\…\\DataCollection: erişim reddedildi (hive kilitli) — yeniden denenecek");
        log::info("reg", L"Yeniden deneme 1/3 … ok");
        log::info("pkg", L"Cleanup-Image /StartComponentCleanup planlandı");
        log::debug("dism", L"dism.exe exit=0 (1188 ms)");
    }
    buildUi({});
    if (m_options.demoFeatures) {
        using core::OptionalFeature;
        using S = core::ServicingState;
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        auto feature = [](std::wstring name, std::wstring display, S state) {
            return OptionalFeature{OptionalFeature::Kind::Feature, std::move(name), std::move(display), {}, state, 0, false};
        };
        auto capability = [](std::wstring name, std::wstring display, std::uint64_t mb) {
            return OptionalFeature{OptionalFeature::Kind::Capability, std::move(name), std::move(display), {},
                                   S::Installed, mb * 1024 * 1024, false};
        };
        std::vector<OptionalFeature> items{
            feature(L"NetFx3", L".NET Framework 3.5", S::Installed),
            feature(L"Microsoft-Hyper-V-All", L"Hyper-V", S::Staged),
            feature(L"Containers-DisposableClientVM", L"Windows Sandbox", S::Staged),
            capability(L"Browser.InternetExplorer~~~~0.0.11.0", L"Internet Explorer modu", 42),
            capability(L"OpenSSH.Client~~~~0.0.1.0", L"OpenSSH \u0130stemcisi", 6),
            capability(L"Media.WindowsMediaPlayer~~~~0.0.12.0", L"Windows Media Player (eski)", 31),
            capability(L"Microsoft.Windows.WordPad~~~~0.0.1.0", L"Wordpad", 9),
            feature(L"Microsoft-Windows-Subsystem-Linux", L"WSL", S::Staged),
        };
        m_state->setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Ready, mountDir,
                                                                std::move(items), {}});
        m_state->queue(FeatureController::operationFor(m_state->optionalFeatures()->items[4]));
        m_state->queue(FeatureController::operationFor(m_state->optionalFeatures()->items[7]));
        m_shell->showPage(PageId::Features);
    }
    if (m_options.openPath) {
        // Headless: open synchronously so the frame shows the Images page with real data.
        if (auto info = core::openSource(*m_options.openPath)) {
            m_state->setSource(std::move(*info));
            if (m_options.selectIndex) {
                m_state->select(*m_options.selectIndex);
            }
            m_shell->showPage(m_options.page.value_or(PageId::Images));
        }
    }
    if (m_options.fakeOperation && m_state->source()) {
        EngineOperation op{*m_options.fakeOperation == L"prepare" ? EngineOperation::Kind::Preparing
                                                                   : EngineOperation::Kind::Mounting,
                           m_state->selectedImage() ? m_state->selectedImage()->name : L"", L"C:\\WinLove\\mount",
                           m_state->selectedIndex().value_or(1)};
        op.startedMs = ui::nowMs() - 60000.0 * m_options.fakeProgress;
        m_state->beginOperation(op);
        m_state->updateOperation(m_options.fakeProgress);
    }
    m_host->layout(m_options.size);

    // Drive the real input paths so the frame shows exactly what the interaction would.
    for (int i = 0; i < m_options.tabPresses; ++i) {
        m_host->onKeyDown({VK_TAB, false, false, false});
    }
    auto pointerAt = [&](ui::PointF p) {
        m_host->onPointer({ui::PointerAction::Move, p, m_host->windowZone(p)});
    };
    if (m_options.hoverAt) {
        pointerAt(*m_options.hoverAt);
    }
    if (m_options.tooltipAt) {
        pointerAt(*m_options.tooltipAt);
        m_host->onTimer(ui::Host::kTooltipTimer);
    }
    for (const auto& p : m_options.clickAt) {
        pointerAt(p);
        m_host->onPointer({ui::PointerAction::Down, p, m_host->windowZone(p)});
        m_host->onPointer({ui::PointerAction::Up, p, m_host->windowZone(p)});
        m_host->layout(m_options.size); // popups get their bounds from the host size
    }
    if (m_options.pressAt) {
        pointerAt(*m_options.pressAt);
        m_host->onPointer({ui::PointerAction::Down, *m_options.pressAt, m_host->windowZone(*m_options.pressAt)});
    }
    if (m_options.dragValid) {
        m_shell->dragEnter({*m_options.dragValid ? std::filesystem::path(L"x.iso") : std::filesystem::path(L"x.txt")});
    }
    if (m_options.adminDialog) {
        m_shell->showAdminRequired();
    }

    ui::Canvas canvas((*target)->beginDraw(), m_options.theme, m_options.scale, *m_graphics->text, *m_graphics->icons);
    canvas.clear(Color::BgBase);
    // Hover/press tweens settle instantly in a still frame.
    m_host->paint(canvas);
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
    // Session log file next to dism.log (P03 Loglar reads the same folder).
    log::addSink(log::makeFileSink(log::defaultDirectory()));
    log::info("app", L"WinLove started");
    ui::WindowCallbacks callbacks;
    callbacks.paint = [this] { paint(); };
    callbacks.resized = [this](ui::SizeF size, float scale) {
        if (m_target) {
            if (auto resized = m_target->resize(m_window.clientWidthPx(), m_window.clientHeightPx(), scale * 96.0f);
                !resized) {
                showError(resized.error());
            }
        }
        if (m_host) {
            m_host->layout(size);
        }
        paint(); // draw right away: keeps live resizing smooth
    };
    callbacks.hitTest = [this](ui::PointF p) { return m_host ? m_host->windowZone(p) : ui::HitZone::Client; };
    callbacks.wheel = [this](ui::PointF p, float lines) {
        if (m_host) {
            m_host->onWheel(p, lines);
        }
    };
    callbacks.character = [this](wchar_t ch) {
        if (m_host) {
            m_host->onChar(ch);
        }
    };
    callbacks.cursor = [this](ui::PointF p) { return m_host ? m_host->cursorAt(p) : ui::Cursor::Arrow; };
    callbacks.pointer = [this](const ui::PointerEvent& event) {
        if (m_host) {
            m_host->onPointer(event);
        }
    };
    callbacks.activated = [this](bool active) {
        if (m_shell) {
            m_shell->titleBar().setWindowActive(active);
        }
    };
    callbacks.maximizedChanged = [this](bool maximized) {
        if (m_shell) {
            m_shell->titleBar().setMaximized(maximized);
        }
    };
    callbacks.keyDown = [this](const ui::KeyEvent& key) {
        if (m_host && !m_host->onKeyDown(key) && m_shell) {
            m_shell->handleShortcut(key);
        }
    };
    callbacks.timer = [this](UINT id) {
        if (id == Shell::kToastTimer || id == Shell::kLogTimer) {
            if (m_shell) {
                m_shell->onTimer(id);
            }
        } else if (m_host) {
            m_host->onTimer(id);
        }
    };
    callbacks.settingsChanged = [this] {
        ui::refreshReducedMotion();
        m_window.invalidate();
    };

    const ui::WindowAppearance appearance{appIcon(), m_options.theme != ui::ThemeKind::Light,
                                          colorRef(m_options.theme, Color::LineStrong),
                                          colorRef(m_options.theme, Color::BgBase)};
    const ui::SizeF minimum{ui::tokens::size::minWindowW, ui::tokens::size::minWindowH};

    // The host must exist before the window: creation already sends WM_SIZE / WM_NCHITTEST.
    buildUi({
        [this] { m_window.invalidate(); },
        [this](UINT id, UINT ms) { m_window.setTimer(id, ms); },
        [this](UINT id) { m_window.stopTimer(id); },
    });
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
    m_host->layout(m_window.clientSize());
    m_dropTarget = ui::DropTarget::registerOn(m_window, ui::DropCallbacks{
        [this](const std::vector<std::filesystem::path>& files, ui::PointF) { return m_shell && m_shell->dragEnter(files); },
        nullptr,
        [this] {
            if (m_shell) {
                m_shell->dragLeave();
            }
        },
        [this](const std::vector<std::filesystem::path>& files, ui::PointF) {
            if (m_shell) {
                m_shell->drop(files);
            }
        },
    });
    m_window.show();
    if (m_options.openPath) {
        const auto mountIndex = m_options.mountIndex;
        const auto selectIndex = m_options.selectIndex;
        m_shell->openSource(*m_options.openPath, [this, mountIndex, selectIndex] {
            if (selectIndex) {
                m_state->select(*selectIndex);
            }
            if (mountIndex) {
                m_state->select(*mountIndex);
                m_shell->images().mount(*mountIndex); // continue the mount the user asked for before UAC
            }
        });
    } else {
        m_shell->images().adoptExistingMount();
    }
    const int exitCode = ui::Window::runMessageLoop();
    if (m_dropTarget) {
        m_dropTarget->revoke();
        m_dropTarget = nullptr;
    }
    return exitCode;
}

void App::applyTheme() {
    m_window.setFrameColors(m_options.theme != ui::ThemeKind::Light, colorRef(m_options.theme, Color::LineStrong));
    m_window.invalidate();
}

void App::paint() {
    if (!m_target || !m_host) {
        return;
    }
    ui::Canvas canvas(m_target->beginDraw(), m_options.theme, m_window.scale(), *m_graphics->text,
                      *m_graphics->icons);
    canvas.clear(Color::BgBase);
    m_host->paint(canvas);
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
    // Text styles are referenced by the host for measurement; swap them together.
    auto graphics = ui::Graphics::create(embeddedFonts());
    if (!graphics) {
        return std::unexpected(graphics.error());
    }
    m_graphics = std::move(*graphics);
    // Host keeps a pointer to the old TextStyles: rebuild the UI on the new graphics.
    const PageId page = m_shell ? m_shell->currentPage() : PageId::Source;
    m_options.page = page;
    buildUi({
        [this] { m_window.invalidate(); },
        [this](UINT id, UINT ms) { m_window.setTimer(id, ms); },
        [this](UINT id) { m_window.stopTimer(id); },
    });
    m_host->layout(m_window.clientSize());
    auto target = ui::SwapChainTarget::create(*m_graphics->device, m_window.hwnd(), m_window.clientWidthPx(),
                                              m_window.clientHeightPx(), m_window.scale() * 96.0f);
    if (!target) {
        return std::unexpected(target.error());
    }
    m_target = std::move(*target);
    return {};
}

} // namespace wl::app
