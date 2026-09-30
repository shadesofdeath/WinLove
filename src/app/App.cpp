#include "app/App.h"

#include "core/image/DriverInf.h"

#include "app/pages/UpdatesPage.h"

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

// D2D reports a lost device from EndDraw as D2DERR_RECREATE_TARGET (before Present ever sees it);
// hung / internal driver errors need the same recovery.
bool deviceLost(std::int32_t hr) {
    switch (static_cast<std::uint32_t>(hr)) {
    case 0x887A0005: // DXGI_ERROR_DEVICE_REMOVED
    case 0x887A0007: // DXGI_ERROR_DEVICE_RESET
    case 0x887A0006: // DXGI_ERROR_DEVICE_HUNG
    case 0x887A0020: // DXGI_ERROR_DRIVER_INTERNAL_ERROR
    case 0x8899000C: // D2DERR_RECREATE_TARGET
        return true;
    default: return false;
    }
}

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
        } else if (startsWith(a, L"--demo-apply=")) {
            options.demoApply = std::wstring(value(L"--demo-apply="));
        } else if (a == L"--demo-registry") {
            options.demoRegistry = true;
        } else if (a == L"--demo-services") {
            options.demoServices = true;
        } else if (a == L"--demo-tweaks") {
            options.demoTweaks = true;
        } else if (a == L"--demo-unattended") {
            options.demoUnattended = true;
        } else if (a == L"--demo-postsetup") {
            options.demoPostSetup = true;
        } else if (a == L"--demo-presets") {
            options.demoPresets = true;
        } else if (a == L"--demo-drivers") {
            options.demoDrivers = true;
        } else if (a == L"--demo-updates") {
            options.demoUpdates = true;
        } else if (a == L"--demo-components") {
            options.demoComponents = true;
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
    if (m_options.demoComponents) {
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        auto app = [](const wchar_t* identity, const wchar_t* version, std::uint64_t mb) {
            core::AppxComponent c;
            c.package.displayName = identity;
            c.package.packageName = std::wstring(identity) + L"_" + version + L"_neutral_~_8wekyb3d8bbwe";
            c.package.version = version;
            c.size = mb * 1024 * 1024;
            return c;
        };
        std::vector<core::AppxComponent> items{
            app(L"Microsoft.GamingApp", L"2410.1001.4.0", 412), app(L"Microsoft.XboxGamingOverlay", L"7.224.11061.0", 96),
            app(L"Microsoft.XboxIdentityProvider", L"12.115.1001.0", 8), app(L"Microsoft.ZuneMusic", L"11.2408.12.0", 142),
            app(L"Microsoft.Windows.Photos", L"2024.11100.16010.0", 94), app(L"Clipchamp.Clipchamp", L"3.1.10020.0", 71),
            app(L"Microsoft.MicrosoftOfficeHub", L"18.2408.1122.0", 41), app(L"Microsoft.Todos", L"2.114.7122.0", 38),
            app(L"Microsoft.MicrosoftStickyNotes", L"6.1.2.0", 12), app(L"Microsoft.BingWeather", L"4.54.63007.0", 21),
            app(L"Microsoft.BingNews", L"4.55.62231.0", 18), app(L"MicrosoftWindows.Client.WebExperience", L"424.1301.270.9", 64),
            app(L"Microsoft.WindowsStore", L"22410.1401.1.0", 58), app(L"Microsoft.SecHealthUI", L"1000.26100.1.0", 9),
            app(L"Microsoft.VCLibs.140.00", L"14.0.33519.0", 6), app(L"Microsoft.WindowsCalculator", L"11.2409.0.0", 14),
            app(L"Contoso.Unknown", L"1.0.0.0", 3),
        };
        m_state->setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, mountDir, std::move(items), {}});
        auto& controller = m_shell->components();
        for (const auto& g : controller.groups()) {
            for (const auto& item : g.items) {
                if (item.identity == L"Microsoft.GamingApp" || item.identity == L"Microsoft.XboxGamingOverlay" ||
                    item.identity == L"Microsoft.ZuneMusic" || item.identity == L"Microsoft.SecHealthUI") {
                    controller.toggle(item);
                }
            }
        }
        m_shell->showPage(m_options.page.value_or(PageId::Components));
    }
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
        if (!m_options.demoApply.empty()) {
            AppState::ApplyRun run;
            run.changes = m_state->changes();
            run.plan = core::ops::plan(run.changes);
            run.groups = core::ops::groups(run.plan);
            run.stepState.assign(run.plan.steps.size(), 2);
            run.startedMs = ui::nowMs() - 95'000;
            run.edition = L"Windows 11 Pro";
            run.sizeBefore = 24'622'989'344ULL;
            if (m_options.demoApply == L"running") {
                run.stepState.back() = 1;
                run.currentStep = static_cast<int>(run.stepState.size()) - 1;
                run.fraction = 0.42;
                log::info("apply", L"[1/2] removeCapability OpenSSH.Client~~~~0.0.1.0");
                log::info("dism", L"remove capability OpenSSH.Client~~~~0.0.1.0 \u2026 ok");
                log::info("apply", L"[2/2] enableFeature Microsoft-Windows-Subsystem-Linux");
            } else {
                run.stage = AppState::ApplyRun::Stage::Done;
                run.fraction = 1.0;
                run.sizeAfter = 24'180'112'000ULL;
                core::ops::ApplyJobResult result;
                result.report.completed = true;
                for (const auto& step : run.plan.steps) {
                    result.report.results.push_back({step, {}});
                    result.stepTimes.push_back(std::chrono::milliseconds(14'000));
                }
                result.committed = true;
                result.commitTime = std::chrono::milliseconds(81'000);
                result.elapsed = std::chrono::milliseconds(112'000);
                run.result = std::move(result);
                m_state->setApplyRun(std::move(run));
                m_state->setMounted(std::nullopt);
                m_shell->showPage(PageId::Apply);
            }
            if (m_state->applyRun() == std::nullopt) {
                m_state->setApplyRun(std::move(run));
            }
        }
        m_shell->showPage(m_options.page.value_or(PageId::Features));
    }
    if (m_options.demoRegistry) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        auto& registry = m_shell->registry();
        for (const auto& tweak : registry.catalog().tweaks()) {
            if (tweak.recommended && tweak.category != "appearance") {
                registry.toggle(tweak);
            }
        }
        if (auto sample = core::parseRegText(L"Windows Registry Editor Version 5.00\n[HKEY_CURRENT_USER\\Software\\Contoso]\n"
                                             L"\"Telemetry\"=dword:00000000\n\"Channel\"=\"stable\"\n")) {
            registry.addImport(L"C:\\Tweaks\\contoso-defaults.reg", std::move(*sample));
        }
        m_shell->showPage(m_options.page.value_or(PageId::Registry));
    }
    if (m_options.demoTweaks) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        // The selections of screen 10.
        auto& controller = m_shell->imageSettings();
        const std::pair<std::string_view, std::string_view> picks[] = {
            {"telemetry", "security"}, {"advertising-id", "off"}, {"diag-viewer", "off"},
            {"inking", "off"},         {"web-search", "off"},     {"location", "off"}};
        for (const auto& setting : controller.catalog().settings()) {
            for (const auto& [id, option] : picks) {
                if (setting.id != id) {
                    continue;
                }
                const auto it = std::ranges::find(setting.options, option, &ImageSettingOption::id);
                if (it != setting.options.end()) {
                    controller.select(setting, static_cast<int>(it - setting.options.begin()));
                }
            }
        }
        m_shell->showPage(m_options.page.value_or(PageId::Tweaks));
    }
    if (m_options.demoUnattended) {
        // The answers of screen 11; the second edit is what the preview marks as changed.
        auto& controller = m_shell->unattend();
        controller.edit([](core::UnattendOptions& o) {
            o.skipPrivacy = true;
            o.bypassNro = true;
            o.skipOnlineAccount = true;
            o.accountName = L"admin";
            o.password = L"parola12";
            o.autoLogon = true;
            o.bypassSecureBoot = true;
            o.bypassRam = true;
        });
        m_shell->showPage(m_options.page.value_or(PageId::Unattended));
        controller.edit([](core::UnattendOptions& o) { o.bypassTpm = true; });
    }
    if (m_options.demoPostSetup) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        // The steps of screen 12.
        using Step = core::PostSetupStep;
        auto& controller = m_shell->postSetup();
        for (Step step : {
                 Step{Step::Type::Winget, L"7-Zip 24.08", L"7zip.7zip", {}, true},
                 Step{Step::Type::Winget, L"Firefox", L"Mozilla.Firefox", {}, true},
                 Step{Step::Type::Command, L"G\u00fc\u00e7 plan\u0131: Y\u00fcksek performans",
                      L"powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", {}, true},
                 Step{Step::Type::Copy, L"Duvar ka\u011f\u0131tlar\u0131 kopyala", L"D:\\assets\\wallpapers",
                      L"C:\\Users\\Public\\Pictures", true},
                 Step{Step::Type::Command, L"OneDrive kald\u0131r", L"%SystemRoot%\\SysWOW64\\OneDriveSetup.exe /uninstall", {},
                      true},
                 Step{Step::Type::Winget, L"VS Code", L"Microsoft.VisualStudioCode", {}, true},
                 Step{Step::Type::Command, L"Yeniden ba\u015flat", L"shutdown /r /t 30", {}, false},
             }) {
            controller.add(std::move(step));
        }
        m_shell->showPage(m_options.page.value_or(PageId::PostSetup));
    }
    if (m_options.demoPresets) {
        // A library like screen 17 (in memory: the user's own presets are neither read nor touched).
        using core::ops::OpKind;
        using core::ops::Operation;
        const auto& catalog = m_shell->imageSettings().catalog();
        auto choose = [&](core::ops::ChangeSet& set, std::string_view id, std::string_view option) {
            for (const auto& setting : catalog.settings()) {
                if (setting.id != id) {
                    continue;
                }
                const auto it = std::ranges::find(setting.options, option, &ImageSettingOption::id);
                if (it != setting.options.end()) {
                    set.addAll(ImageSettingsController::operationsFor(setting, static_cast<int>(it - setting.options.begin())));
                }
            }
        };
        auto appx = [](const wchar_t* identity) {
            return Operation{OpKind::RemoveAppx, std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe"};
        };
        auto postSetup = [](std::vector<core::PostSetupStep> steps) {
            core::PostSetupPlan plan;
            plan.steps = std::move(steps);
            return PostSetupController::operationFor(plan, 0);
        };
        using Step = core::PostSetupStep;

        Preset gaming;
        gaming.name = L"Gaming Slim";
        gaming.changes.addAll({appx(L"Microsoft.BingNews"), appx(L"Microsoft.XboxGamingOverlay"),
                               appx(L"Microsoft.SecHealthUI"), Operation{OpKind::SetServiceStart, L"DiagTrack", L"disabled"},
                               postSetup({Step{Step::Type::Winget, L"Steam", L"Valve.Steam", {}, true}})});
        choose(gaming.changes, "game-dvr", "off");
        choose(gaming.changes, "telemetry", "security");
        gaming.unattend = AppState::Unattend{};
        gaming.unattend->options.accountName = L"gamer";

        Preset office;
        office.name = L"Ofis G\u00fcvenli";
        office.changes.addAll({appx(L"Microsoft.BingNews"), appx(L"Microsoft.GetHelp"),
                               Operation{OpKind::SetServiceStart, L"DiagTrack", L"manual"},
                               postSetup({Step{Step::Type::Winget, L"LibreOffice", L"TheDocumentFoundation.LibreOffice", {},
                                               true}})});
        choose(office.changes, "web-search", "off");
        choose(office.changes, "telemetry", "security");
        office.unattend = AppState::Unattend{};
        office.unattend->options.accountName = L"ofis";

        Preset kiosk;
        kiosk.name = L"Minimal Kiosk";
        kiosk.changes.addAll({appx(L"Microsoft.BingNews"), appx(L"Microsoft.ZuneMusic"), appx(L"Microsoft.WindowsStore")});
        choose(kiosk.changes, "widgets", "off");
        Preset blank;
        blank.name = L"Varsay\u0131lan (bo\u015f)";
        std::vector<Preset> library;
        library.push_back(std::move(gaming));
        library.push_back(std::move(office));
        library.push_back(std::move(kiosk));
        library.push_back(std::move(blank));
        m_shell->presets().adopt(std::move(library));
        m_shell->showPage(m_options.page.value_or(PageId::Presets));
    }
    if (m_options.demoServices) {
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        using core::StartType;
        auto svc = [](const wchar_t* name, const wchar_t* display, StartType start,
                      std::vector<std::wstring> deps = {}) {
            core::ServiceEntry s;
            s.name = name;
            s.displayName = display;
            s.start = start;
            s.type = 0x10;
            s.dependsOn = std::move(deps);
            return s;
        };
        std::vector<core::ServiceEntry> items{
            svc(L"DiagTrack", L"Ba\u011fl\u0131 Kullan\u0131c\u0131 Deneyimleri ve Telemetri", StartType::Auto),
            svc(L"WSearch", L"Windows Arama", StartType::AutoDelayed, {L"RPCSS"}),
            svc(L"Spooler", L"Yazd\u0131rma Biriktiricisi", StartType::Auto, {L"RPCSS", L"http"}),
            svc(L"wuauserv", L"Windows Update", StartType::Manual, {L"rpcss"}),
            svc(L"RemoteRegistry", L"Uzak Kay\u0131t Defteri", StartType::Disabled, {L"RPCSS"}),
            svc(L"XblAuthManager", L"Xbox Live Kimlik Do\u011frulama", StartType::Manual),
            svc(L"PrintNotify", L"Yaz\u0131c\u0131 Uzant\u0131lar\u0131 ve Bildirimleri", StartType::Manual, {L"Spooler"}),
            svc(L"BITS", L"Arka Plan Ak\u0131ll\u0131 Aktarma Hizmeti", StartType::AutoDelayed, {L"RpcSs"}),
        };
        m_state->setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Ready, mountDir, std::move(items), {}});
        auto& controller = m_shell->services();
        controller.set(m_state->serviceList()->items[0], StartType::Disabled);
        m_shell->showPage(m_options.page.value_or(PageId::Services));
    }
    if (m_options.demoDrivers) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        auto inf = [](const wchar_t* file, const wchar_t* cls, const wchar_t* provider, const wchar_t* ver,
                      const wchar_t* arch, std::uint64_t kb) {
            std::wstring text = std::wstring(L"[Version]\nClass=") + cls + L"\nProvider=\"" + provider +
                                L"\"\nDriverVer=05/14/2024," + ver + L"\n[Manufacturer]\n%M%=M," + arch + L"\n";
            auto d = core::parseInfText(text, std::filesystem::path(L"D:\\Drivers\\Dell\\") / file);
            d.size = kb * 1024;
            return d;
        };
        std::vector<core::DriverInf> infs{
            inf(L"e2f68.inf", L"Net", L"Intel", L"12.19.2.45", L"NTamd64", 1840),
            inf(L"netwtw08.inf", L"Net", L"Intel", L"23.60.1.2", L"NTamd64", 38200),
            inf(L"rt640x64.inf", L"Net", L"Realtek", L"10.68.815.2023", L"NTamd64", 1200),
            inf(L"iaStorVD.inf", L"SCSIAdapter", L"Intel Corporation", L"19.5.2.1049", L"NTamd64", 2950),
            inf(L"stornvme_oem.inf", L"SCSIAdapter", L"Samsung", L"3.3.0.2003", L"NTamd64", 410),
            inf(L"HdBusExt.inf", L"System", L"Intel", L"10.29.0.9677", L"NTamd64", 96),
            inf(L"iaLPSS2_I2C.inf", L"System", L"Intel", L"30.100.2229.2", L"NTamd64", 220),
            inf(L"RTKVHD64.inf", L"MEDIA", L"Realtek", L"6.0.9601.1", L"NTamd64", 51300),
            inf(L"ibtusb.inf", L"Bluetooth", L"Intel", L"23.60.0.1", L"NTamd64", 4600),
            inf(L"qcwlan_arm.inf", L"Net", L"Qualcomm", L"3.0.0.912", L"NTarm64", 6100),
        };
        m_state->addDriverScan(L"D:\\Drivers\\Dell", std::move(infs));
        for (const auto& d : m_state->driverScan().infs) {
            if (d.className == L"SCSIAdapter" || d.path.filename() == L"netwtw08.inf") {
                core::ops::Operation op{core::ops::OpKind::AddDriver, d.path.wstring(), d.className};
                op.sizeDelta = static_cast<std::int64_t>(d.size);
                m_state->queue(std::move(op));
            }
        }
        m_shell->showPage(m_options.page.value_or(PageId::Drivers));
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
    if (m_options.demoUpdates && m_state->source()) {
        const int index = m_state->source()->install.images.back().index;
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", index,
                                         m_state->source()->install.images.back().name});
        UpdatesPage::queuePackages(*m_state, {LR"(C:\Updates\windows11.0-kb5044284-x64_8d2c.msu)",
                                              LR"(C:\Updates\windows11.0-kb5043080-x64_ssu_7e8f.msu)",
                                              LR"(C:\Updates\windows11.0-kb5044030-x64-ndp481_a1b2.msu)",
                                              LR"(C:\Updates\windows10.0-kb5041585-x64.msu)"});
        m_shell->showPage(PageId::Updates);
    }
    if (m_options.fakeOperation && m_state->source()) {
        const bool reading = *m_options.fakeOperation == L"read";
        EngineOperation op{reading                                  ? EngineOperation::Kind::Reading
                           : *m_options.fakeOperation == L"prepare" ? EngineOperation::Kind::Preparing
                                                                    : EngineOperation::Kind::Mounting,
                           m_state->selectedImage() ? m_state->selectedImage()->name : L"", L"C:\\WinLove\\mount",
                           m_state->selectedIndex().value_or(1)};
        op.startedMs = ui::nowMs() - 60000.0 * m_options.fakeProgress;
        if (reading) {
            // The second progress (D-027): the image is mounted, its lists are being read.
            m_state->setMounted(MountedImage{op.path, L"C:\\WinLove\\work\\sources\\install.wim", op.index, op.edition});
            op.stage = m_options.fakeProgress < 0.6f ? 0 : m_options.fakeProgress < 0.9f ? 1 : 2;
        }
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
    callbacks.closeRequested = [this] { return m_forceClose || !m_shell || m_shell->confirmClose(); };
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
        // A mount left from an earlier run must still be seen (Images page InfoBar: Onar /
        // Devam et); mountSafely itself reuses the same image or repairs leftovers.
        m_shell->images().inspectMountFolder();
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
        if (deviceLost(hr)) {
            if (auto rebuilt = recreateGraphics(); !rebuilt) {
                showError(rebuilt.error());
                m_forceClose = true;
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
