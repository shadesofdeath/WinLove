#include "app/App.h"

#include "app/catalog/IconCatalog.h"
#include "app/ChangelogWindow.h"
#include "core/net/SelfUpdate.h"
#include "core/updates/UupLanguages.h"

#include "app/state/AnswerStore.h"

#include "core/image/DriverInf.h"
#include "core/image/RegistryInput.h"
#include "core/image/Source.h"
#include "core/image/VirtualDisk.h"
#include "core/image/dism/HostDism.h"
#include "core/image/wim/WimGapi.h"

#include "app/pages/DownloadPage.h"
#include "app/pages/PoliciesPage.h"
#include "app/pages/ContextMenuPage.h"
#include "app/pages/AppsPage.h"
#include "app/controllers/ImageDriverController.h"
#include "app/pages/DriversPage.h"
#include "app/pages/IconsPage.h"
#include "app/pages/StartMenuPage.h"
#include "app/pages/IsoPage.h"
#include "app/pages/SettingsPage.h"
#include "app/pages/UpdatesPage.h"

#include "base/Log.h"

#include "app/Format.h"
#include "app/Resources.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"
#include "ui/render/Canvas.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/widgets/ProgressRing.h"

#include <json.hpp>

#include <shellapi.h>

#include <chrono>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

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

// "Sistem": high contrast when Windows has it on, else the app light / dark mode. Windows' high
// contrast wins over any choice (audit D9): it is an accessibility setting, not a taste.
ui::ThemeKind resolveTheme(ThemeChoice choice) {
    if (ui::refreshSystemContrast()) {
        return ui::ThemeKind::HighContrast;
    }
    switch (choice) {
    case ThemeChoice::Dark: return ui::ThemeKind::Dark;
    case ThemeChoice::Light: return ui::ThemeKind::Light;
    case ThemeChoice::Black: return ui::ThemeKind::Black;
    case ThemeChoice::System: break;
    }
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        return ui::ThemeKind::HighContrast;
    }
    DWORD light = 0;
    DWORD size = sizeof(light);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return status == ERROR_SUCCESS && light != 0 ? ui::ThemeKind::Light : ui::ThemeKind::Dark;
}

// Windows' "Text size" (Settings > Accessibility): 100-225 %. The whole interface follows it, as the
// layout is built for the type sizes it has (audit D9).
float windowsTextScale() {
    DWORD percent = 100;
    DWORD size = sizeof(percent);
    if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD,
                     nullptr, &percent, &size) != ERROR_SUCCESS) {
        return 1.0f;
    }
    return static_cast<float>(std::clamp<DWORD>(percent, 100, 225)) / 100.0f;
}

float zoomFor(const AppSettings& settings) {
    return densityScale(settings.density) * windowsTextScale();
}

// D-123: Ayarlar › DISM motoru and ESD sıkıştırma iş parçacıkları, given to the engine at start.
void applyEngineSettings(const AppSettings& settings) {
    switch (settings.dismEngine) {
    case DismEngineChoice::Host: core::setDismEngine(core::DismEngine::Host); break;
    case DismEngineChoice::Adk:
        core::setDismEngine(core::DismEngine::Adk);
        if (const auto adk = core::adkDismFolder()) {
            core::forceWimgapi(*adk / L"wimgapi.dll"); // the WIM library of the same kit
        }
        break;
    case DismEngineChoice::Auto: core::setDismEngine(core::DismEngine::Auto); break;
    }
    core::setCompressionThreads(settings.esdThreads);
}

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
            else if (v == L"black") options.theme = ui::ThemeKind::Black;
            else return fail(ErrorCode::InvalidArgument, L"--theme must be dark|light|hc", arg);
            options.themeGiven = true;
        } else if (startsWith(a, L"--accent=")) {
            const auto v = value(L"--accent=");
            static constexpr const wchar_t* kNames[] = {L"blue", L"copper", L"sea", L"pomegranate", L"olive"};
            for (int i = 0; i < ui::kAccentCount; ++i) {
                if (v == kNames[i]) {
                    options.accent = ui::presetColor(static_cast<ui::Accent>(i));
                }
            }
            if (!options.accent) {
                options.accent = ui::parseHexColor(v); // D-143: any colour
            }
            if (!options.accent) {
                return fail(ErrorCode::InvalidArgument, L"--accent must be blue|copper|sea|pomegranate|olive or #RRGGBB", arg);
            }
            ui::setAccentColor(*options.accent);
        } else if (startsWith(a, L"--anim-time=")) {
            const double ms = std::wcstod(std::wstring(value(L"--anim-time=")).c_str(), nullptr);
            if (!(ms >= 0)) {
                return fail(ErrorCode::InvalidArgument, L"--anim-time must be a number of milliseconds", arg);
            }
            options.animTime = ms;
            ui::setRingTime(ms); // D-142
        } else if (a == L"--demo-accent-picker") {
            options.demoAccentPicker = true;
        } else if (startsWith(a, L"--lang=")) {
            const auto v = value(L"--lang=");
            if (v == L"tr") options.language = Language::Turkish;
            else if (v == L"en") options.language = Language::English;
            else return fail(ErrorCode::InvalidArgument, L"--lang must be tr|en", arg);
            options.languageGiven = true;
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
            options.sizeGiven = true;
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
        } else if (a == L"--relaunched") {
            // main.cpp: started by a WinLove that is closing; it waits for that one to go.
        } else if (a == L"--no-elevate") {
            // handled in main.cpp (skip the startup UAC relaunch)
        } else if (a == L"--demo-vhd" || startsWith(a, L"--demo-vhd=")) {
            options.demoVhd = a == L"--demo-vhd" ? std::wstring(L"closed") : std::wstring(value(L"--demo-vhd="));
        } else if (a == L"--demo-overview" || startsWith(a, L"--demo-overview=")) {
            options.demoOverview = a == L"--demo-overview" ? std::wstring(L"ready") : std::wstring(value(L"--demo-overview="));
        } else if (startsWith(a, L"--demo-apply=")) {
            options.demoApply = std::wstring(value(L"--demo-apply="));
        } else if (a == L"--demo-registry" || a == L"--demo-registry=dialog") {
            options.demoRegistry = true;
            options.demoRegistryDialog = a.ends_with(L"=dialog");
        } else if (a == L"--demo-tasks") {
            options.demoTasks = true;
        } else if (a == L"--demo-task-create") {
            options.demoTasks = true;
            options.demoTaskCreate = true;
        } else if (a == L"--demo-files" || startsWith(a, L"--demo-files=")) {
            options.demoFiles = a == L"--demo-files" ? std::wstring() : std::wstring(value(L"--demo-files="));
        } else if (a == L"--demo-image-drivers") {
            options.demoImageDrivers = true;
        } else if (a == L"--demo-apps" || startsWith(a, L"--demo-apps=")) {
            options.demoApps = a == L"--demo-apps" ? std::wstring() : std::wstring(value(L"--demo-apps="));
        } else if (a == L"--demo-languages" || startsWith(a, L"--demo-languages=")) {
            options.demoLanguages = a == L"--demo-languages" ? std::wstring() : std::wstring(value(L"--demo-languages="));
        } else if (startsWith(a, L"--demo-reveal=")) {
            options.demoReveal = std::wstring(value(L"--demo-reveal="));
        } else if (startsWith(a, L"--demo-store-share=")) {
            options.demoStoreShare = std::wstring(value(L"--demo-store-share="));
        } else if (a == L"--demo-startmenu" || a == L"--demo-startmenu=settings" || a == L"--demo-startmenu=taskbar") {
            options.demoStartMenu = true;
            options.demoStartMenuSettings = a.ends_with(L"=settings");
            options.demoTaskbar = a.ends_with(L"=taskbar");
        } else if (a == L"--demo-icons" || a == L"--demo-icons=redirect") {
            options.demoIcons = true;
            options.demoIconsRedirect = a.ends_with(L"=redirect");
        } else if (a == L"--demo-mounts" || startsWith(a, L"--demo-mounts=")) {
            options.demoMounts = true;
            options.demoMountsVariant = a == L"--demo-mounts" ? std::wstring() : std::wstring(value(L"--demo-mounts="));
        } else if (a == L"--demo-editions") {
            options.demoEditions = true;
        } else if (a == L"--demo-wifi") {
            options.demoWifi = true;
        } else if (startsWith(a, L"--demo-tool=")) {
            options.demoTool = std::wstring(value(L"--demo-tool="));
        } else if (a == L"--demo-branding") {
            options.demoBranding = true;
        } else if (a == L"--demo-boot-drivers") {
            options.demoBootDrivers = std::wstring();
        } else if (startsWith(a, L"--demo-boot-drivers=")) {
            options.demoBootDrivers = std::wstring(value(L"--demo-boot-drivers="));
        } else if (a.starts_with(L"--demo-download")) {
            const auto v = a.size() > 16 ? a.substr(16) : std::wstring_view(L"list");
            options.demoDownload = v == L"pick" ? 1 : v == L"downloading" ? 2 : v == L"converting" ? 3 : v == L"apps" ? 4 : v == L"win10" ? 5 : v == L"failed" ? 6 : v == L"warnings" ? 7 : v == L"hotpatch" ? 8
                                 : v == L"esd" ? 9 : v == L"uup-only" ? 10 : v == L"esd-info" ? 11 : v == L"esd-downloading" ? 12
                                 : v == L"esd-verifying" ? 13 : v == L"esd-converting" ? 14 : v == L"esd-failed" ? 15 : 0;
        } else if (a == L"--demo-hosts") {
            options.demoHosts = true;
        } else if (a == L"--demo-services") {
            options.demoServices = true;
        } else if (a.starts_with(L"--demo-policies")) {
            options.demoPolicies = a.size() > 16 ? std::wstring(a.substr(16)) : std::wstring();
        } else if (a.starts_with(L"--demo-contextmenu")) {
            options.demoContextMenu = a.size() > 19 ? std::wstring(a.substr(19)) : std::wstring();
        } else if (a == L"--demo-preset-ask") {
            options.demoPresetAsk = true;
        } else if (a == L"--demo-tweaks") {
            options.demoTweaks = true;
        } else if (a == L"--demo-image-values") {
            options.demoImageValues = true;
        } else if (startsWith(a, L"--demo-tweak-template=")) {
            const std::string list = utf8::fromWide(value(L"--demo-tweak-template="));
            for (std::size_t at = 0; at <= list.size();) {
                const std::size_t comma = std::min(list.find(',', at), list.size());
                if (comma > at) {
                    options.demoTweakTemplates.push_back(list.substr(at, comma - at));
                }
                at = comma + 1;
            }
        } else if (startsWith(a, L"--demo-tweak-filter=")) {
            options.demoTweakFilter = utf8::fromWide(value(L"--demo-tweak-filter="));
        } else if (a == L"--demo-unattended" || a == L"--demo-unattended=welcome") {
            options.demoUnattended = true;
            options.demoWelcome = a.ends_with(L"=welcome");
        } else if (a == L"--demo-postsetup") {
            options.demoPostSetup = true;
        } else if (a == L"--demo-presets") {
            options.demoPresets = true;
        } else if (startsWith(a, L"--switch-lang=")) {
            const auto v = value(L"--switch-lang=");
            if (v == L"tr") options.switchLanguage = Language::Turkish;
            else if (v == L"en") options.switchLanguage = Language::English;
            else return fail(ErrorCode::InvalidArgument, L"--switch-lang must be tr|en", arg);
        } else if (a == L"--demo-drivers") {
            options.demoDrivers = true;
        } else if (a == L"--demo-updates") {
            options.demoUpdates = true;
        } else if (a == L"--demo-media-update") {
            options.demoMediaUpdate = true;
        } else if (a == L"--demo-host-dism") {
            options.demoHostDism = true;
        } else if (a == L"--demo-no-winre") {
            options.demoNoWinre = true;
        } else if (a == L"--demo-aio") {
            options.demoAio = true;
        } else if (a == L"--demo-usb" || startsWith(a, L"--demo-usb=")) {
            options.demoUsbGiven = true;
            options.demoUsb = a == L"--demo-usb" ? std::wstring() : std::wstring(value(L"--demo-usb="));
        } else if (startsWith(a, L"--demo-catalog=")) {
            options.demoCatalog = std::wstring(value(L"--demo-catalog="));
        } else if (startsWith(a, L"--demo-inventory=")) {
            options.demoInventory = std::wstring(value(L"--demo-inventory="));
        } else if (startsWith(a, L"--demo-reveal=")) {
            options.demoReveal = std::wstring(value(L"--demo-reveal="));
        } else if (startsWith(a, L"--demo-store-share=")) {
            options.demoStoreShare = std::wstring(value(L"--demo-store-share="));
        } else if (a == L"--demo-components") {
            options.demoComponents = true;
        } else if (a == L"--demo-compat") {
            options.demoCompat = true;
        } else if (a == L"--demo-update" || startsWith(a, L"--demo-update=")) {
            options.demoUpdate = a == L"--demo-update" ? std::wstring(L"available") : std::wstring(value(L"--demo-update="));
            static constexpr const wchar_t* kStates[] = {L"available",   L"current", L"failed", L"checking", L"downloading",
                                                         L"verifying",   L"ready",   L"install-failed", L"updated"};
            if (std::ranges::find(kStates, *options.demoUpdate) == std::end(kStates)) {
                return fail(ErrorCode::InvalidArgument,
                            L"--demo-update must be available|current|failed|checking|downloading|verifying|ready|install-failed|updated", arg);
            }
        } else if (a == L"--demo-changelog" || startsWith(a, L"--demo-changelog=")) {
            options.demoChangelog = a == L"--demo-changelog" ? std::wstring() : std::wstring(value(L"--demo-changelog="));
        } else if (a == L"--auto-iso") {
            options.autoIso = true;
        } else if (a == L"--iso-choices") {
            options.isoChoices = true;
        } else if (a == L"--iso-options") {
            options.isoOptions = true;
        } else if (startsWith(a, L"--updated-from=")) {
            options.updatedFrom = std::wstring(value(L"--updated-from="));
        } else if (startsWith(a, L"--demo-toast=")) {
            options.demoToast = std::wstring(value(L"--demo-toast="));
        } else if (a == L"--toast-timeout") {
            options.toastTimeout = true;
        } else if (a == L"--demo-app-menu") {
            options.demoAppMenu = true;
        } else if (a == L"--demo-features") {
            options.demoFeatures = true;
        } else if (a == L"--demo-logs") {
            options.demoLogs = true;
        } else if (a == L"--palette") {
            options.palette = std::wstring();
        } else if (startsWith(a, L"--palette=")) {
            options.palette = std::wstring(value(L"--palette="));
        } else if (startsWith(a, L"--keys=")) {
            std::wstring_view rest = value(L"--keys=");
            while (!rest.empty()) {
                const auto comma = rest.find(L',');
                const std::wstring_view name = rest.substr(0, comma);
                rest = comma == std::wstring_view::npos ? std::wstring_view{} : rest.substr(comma + 1);
                const UINT vk = name == L"up"      ? VK_UP
                                : name == L"down"  ? VK_DOWN
                                : name == L"left"  ? VK_LEFT
                                : name == L"right" ? VK_RIGHT
                                : name == L"enter" ? VK_RETURN
                                : name == L"esc"   ? VK_ESCAPE
                                : name == L"del"   ? VK_DELETE
                                : name == L"tab"   ? VK_TAB
                                : name == L"pgdn"  ? VK_NEXT
                                : name == L"pgup"  ? VK_PRIOR
                                : name == L"home"  ? VK_HOME
                                : name == L"end"   ? VK_END
                                                   : 0;
                if (vk == 0) {
                    return fail(ErrorCode::InvalidArgument, L"--keys takes up,down,left,right,enter,esc,del,tab,pgdn,pgup,home,end", arg);
                }
                options.keys.push_back(vk);
            }
        } else if (a == L"--nav-collapsed") {
            options.navCollapsed = true;
        } else if (a == L"--maximized") {
            options.maximized = true;
        } else if (startsWith(a, L"--font=")) {
            const std::wstring_view font = value(L"--font=");
            options.font = font == L"geist" ? UiFont::Geist : font == L"inter" ? UiFont::Inter : UiFont::SegoeVariable;
        } else if (a == L"--test-device-lost") {
            options.testDeviceLost = true;
        } else if (startsWith(a, L"--hover-at=")) {
            ok = point(L"--hover-at=", options.hoverAt);
        } else if (startsWith(a, L"--click-at=")) {
            std::optional<ui::PointF> p;
            ok = point(L"--click-at=", p);
            if (p) {
                options.clickAt.push_back(*p);
            }
        } else if (startsWith(a, L"--click-after-load=")) {
            std::optional<ui::PointF> p;
            ok = point(L"--click-after-load=", p);
            if (p) {
                options.clickAfterLoad.push_back(*p);
            }
        } else if (startsWith(a, L"--settle=")) {
            // "<ms>" or "<ms>,<quiet ms>" (a rate-limited answer waits 11 s without a word)
            const std::wstring v(value(L"--settle="));
            wchar_t* end = nullptr;
            options.settleMs = static_cast<int>(std::wcstol(v.c_str(), &end, 10));
            if (end && *end == L',') {
                options.settleQuietMs = static_cast<int>(std::wcstol(end + 1, nullptr, 10));
            }
        } else if (startsWith(a, L"--context-at=")) {
            ok = point(L"--context-at=", options.contextAt);
        } else if (startsWith(a, L"--press-at=")) {
            ok = point(L"--press-at=", options.pressAt);
        } else if (startsWith(a, L"--tooltip-at=")) {
            ok = point(L"--tooltip-at=", options.tooltipAt);
        } else if (startsWith(a, L"--recent-file=")) {
            options.recentFile = std::filesystem::path(value(L"--recent-file="));
        } else if (startsWith(a, L"--profile=")) {
            options.profile = std::filesystem::path(value(L"--profile="));
        } else if (a == L"--dialog=admin") {
            options.adminDialog = true;
        } else if (startsWith(a, L"--drag=")) {
            options.dragValid = value(L"--drag=") == L"valid";
        } else if (startsWith(a, L"--mount=")) {
            options.mountIndex = static_cast<int>(std::wcstol(std::wstring(value(L"--mount=")).c_str(), nullptr, 10));
        } else if (startsWith(a, L"--select=")) {
            // "4" or "4,2,3": the first is the edition the inspector shows, the rest are marked too.
            std::wstring_view rest = value(L"--select=");
            while (!rest.empty()) {
                const auto comma = rest.find(L',');
                const int index = static_cast<int>(std::wcstol(std::wstring(rest.substr(0, comma)).c_str(), nullptr, 10));
                rest = comma == std::wstring_view::npos ? std::wstring_view{} : rest.substr(comma + 1);
                if (!options.selectIndex) {
                    options.selectIndex = index;
                }
                options.selectMarked.push_back(index);
            }
        } else if (startsWith(a, L"--demo-upgrade=")) {
            options.demoUpgrade = std::wstring(value(L"--demo-upgrade="));
        } else if (startsWith(a, L"--verified=")) {
            options.verified = std::wstring(value(L"--verified="));
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
    // Widgets reference graphics-owned text styles: destroy the trees first.
    m_changelog.reset();
    m_host.reset();
}

void App::post(std::function<void()> fn) {
    if (!m_options.renderTo) {
        m_window.post(std::move(fn));
        return;
    }
    // Headless, no message loop: the main thread's own posts run right away (the render steps rely
    // on that); a worker's wait for drainPosted() on the main thread.
    if (std::this_thread::get_id() == m_mainThread) {
        fn();
        return;
    }
    std::scoped_lock lock(m_postedMutex);
    m_posted.push_back(std::move(fn));
}

void App::drainPosted() {
    for (;;) {
        std::vector<std::function<void()>> batch;
        {
            std::scoped_lock lock(m_postedMutex);
            batch.swap(m_posted);
        }
        if (batch.empty()) {
            return;
        }
        for (auto& fn : batch) {
            fn();
        }
    }
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

    // Headless renders (tests, AI sessions) never touch the user's recent list or settings: they
    // get throw-away files unless a fixture is passed with --recent-file.
    const bool render = m_options.renderTo.has_value();
    const auto scratch = std::filesystem::temp_directory_path() / L"WinLove-render";
    if (render) {
        // Every render starts from the defaults: a click of an earlier one (--click-at folding a
        // nav group, a toggle) must not carry over through the scratch settings file.
        std::error_code ignored;
        std::filesystem::remove(scratch / L"settings.json", ignored);
    }
    // A windowed test run (--profile) keeps the same three files in its own folder.
    const auto& profile = m_options.profile;
    m_state = std::make_unique<AppState>(
        m_options.recentFile.value_or(render    ? scratch / L"recent.json"
                                      : profile ? *profile / L"recent.json"
                                                : RecentSources::defaultFile()),
        render ? scratch / L"settings.json" : profile ? *profile / L"settings.json" : AppSettings::defaultFile(),
        render    ? std::filesystem::path() // renders show what their arguments say
        : profile ? *profile / L"answers.dat"
                  : defaultAnswersFile());
    // The user's settings (P16) — a render shows what its arguments say, never the scratch file.
    if (!render) {
        const AppSettings& settings = m_state->settings();
        if (!m_options.themeGiven) {
            m_options.theme = resolveTheme(settings.theme);
        }
        if (!m_options.languageGiven) {
            m_options.language = settings.language;
        }
        ui::setReducedMotionForced(settings.reduceMotion);
        if (!m_options.accent) {
            ui::setAccentColor(settings.accent);
        }
        m_accentShown = ui::accentColor();
        if (!m_options.font) {
            m_options.font = settings.font;
        }
        // D-123: the DISM WinLove loads (decided once, before the first DISM call) and the ESD threads.
        applyEngineSettings(settings);
    }
    // D-092: the type family; built before any widget measures with it.
    if (m_options.font && *m_options.font != UiFont::SegoeVariable) { // D-107: Segoe UI Variable is the default
        if (auto set = m_graphics->setFamilies(fontFamilies(*m_options.font)); !set) {
            log::warn("app", L"type family not applied: " + describe(set.error()));
        }
    }
    auto strings = embeddedStrings(m_options.language);
    if (!strings) {
        return std::unexpected(strings.error());
    }
    m_strings = std::move(*strings);
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
    // Ctrl+Shift+T: through the settings, so the page and settings.json agree with the window.
    shellServices.toggleTheme = [this] {
        AppSettings settings = m_state->settings();
        settings.theme = m_options.theme == ui::ThemeKind::Dark ? ThemeChoice::Light : ThemeChoice::Dark;
        m_state->setSettings(std::move(settings));
    };
    shellServices.settingsChanged = [this] { applySettings(); };
    shellServices.postToUi = [this](std::function<void()> fn) { post(std::move(fn)); };
    shellServices.ownerWindow = [this] { return m_window.hwnd(); };
    shellServices.relaunchElevated = [this](const std::wstring& args) {
        // --relaunched: the new process waits for this one to close (one WinLove at a time).
        auto relaunched = core::relaunchElevated(args.empty() ? L"--relaunched" : args + L" --relaunched");
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
    if (!m_options.renderTo) {
        // D-140: "Şimdi denetle". A render never reaches the network (the button does nothing there).
        shellServices.checkForUpdates = [this] {
            if (m_updates) {
                m_updates->checkNow();
            }
        };
        // D-145: "Yükle", its stop, the restart, the changelog window.
        shellServices.installUpdate = [this] {
            if (m_updates) {
                m_updates->install();
            }
        };
        shellServices.cancelUpdate = [this] {
            if (m_updates) {
                m_updates->cancelInstall();
            }
        };
        shellServices.restartForUpdate = [this]() -> std::optional<AppState::AppUpdate::Install::Why> {
            return m_updates ? m_updates->restart() : std::nullopt;
        };
        shellServices.showChangelog = [this] { openChangelog(); };
        // 2026-11-24: the status bar's task on the taskbar button; a long one flashes it when done.
        shellServices.taskbarProgress = [this](std::optional<float> fraction) { m_window.setTaskbarProgress(fraction); };
        shellServices.flashIfInactive = [this] { m_window.flashIfInactive(); };
    }
    shellServices.keepAwake = [this](const std::wstring& reason) {
        if (m_options.renderTo) {
            return;
        }
        // No idle sleep while a job runs; a shutdown asks first, saying why (audit A13).
        SetThreadExecutionState(reason.empty() ? ES_CONTINUOUS : ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        if (reason.empty()) {
            ShutdownBlockReasonDestroy(m_window.hwnd());
        } else {
            ShutdownBlockReasonCreate(m_window.hwnd(), reason.c_str());
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
    if (!m_options.renderTo) {
        m_shell->checkHost(); // D-081
    } else if (m_options.demoHostDism) {
        core::HostDismReport report;
        report.problems = {{L"C:\\Windows\\System32\\Dism\\CbsProvider.dll", L"missing"},
                           {L"C:\\Windows\\System32\\wimgapi.dll", L"not signed"}};
        report.signaturesChecked = true;
        m_state->setHostDism(std::move(report));
    }
}

// ---- offscreen -----------------------------------------------------------------------------

int App::renderOffscreen() {
    auto target = ui::OffscreenTarget::create(*m_graphics->device, m_options.size, m_options.scale);
    if (!target) {
        showError(target.error());
        return 1;
    }
    ui::forceInstantMotion(true);
    if (m_options.demoChangelog) {
        return renderChangelog(**target); // D-145: the changelog window, nothing else
    }
    if (m_options.autoIso || (m_options.demoOverview && m_options.demoOverview->starts_with(L"iso-"))) {
        // D-144: "Bitince ISO oluştur" on; a folder that is the same on every machine (a render's settings are scratch).
        AppSettings settings = m_state->settings();
        settings.isoAfterApply = true;
        settings.isoFolder = L"D:\\ISO";
        m_state->setSettings(std::move(settings));
    }
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
    if (m_options.demoComponents && !m_options.demoInventory.empty()) {
        // D-112: a real image's rows (wlcli inventory --json), read before the demo mount below.
        std::ifstream in(std::filesystem::path(m_options.demoInventory), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) {
            text.erase(0, 3);
        }
        const auto doc = nlohmann::json::parse(text, nullptr, false);
        if (doc.is_object()) {
            auto w = [](const nlohmann::json& j, const char* key) { return utf8::toWide(j.value(key, std::string{})); };
            core::ImageInventory inv;
            for (const auto& d : doc.value("drivers", nlohmann::json::array())) {
                core::InventoryDriver driver{w(d, "package"), w(d, "inf"), w(d, "classGuid"), w(d, "class"), w(d, "provider"),
                                             w(d, "version"), w(d, "date"), {}, d.value("size", std::uint64_t{0}), d.value("classInf", false)};
                for (const auto& name : d.value("devices", nlohmann::json::array())) {
                    driver.devices.push_back(utf8::toWide(name.get<std::string>()));
                }
                inv.drivers.push_back(std::move(driver));
            }
            for (const auto& l : doc.value("languages", nlohmann::json::array())) {
                core::InventoryLanguage language{w(l, "tag"), w(l, "name"), {}, l.value("size", std::uint64_t{0})};
                if (l.contains("paths") && l["paths"].is_array()) {
                    for (const auto& p : l["paths"]) {
                        language.paths.push_back(utf8::toWide(p.get<std::string>()));
                    }
                }
                inv.languages.push_back(std::move(language));
            }
            for (const auto& k : doc.value("keyboards", nlohmann::json::array())) {
                inv.keyboards.push_back({w(k, "klid"), w(k, "name"), w(k, "file"), k.value("shared", false)});
            }
            for (const auto& f : doc.value("fonts", nlohmann::json::array())) {
                core::InventoryFont font{w(f, "family"), {}, {}, f.value("size", std::uint64_t{0})};
                for (const auto& v : f.value("values", nlohmann::json::array())) {
                    font.values.push_back(utf8::toWide(v.get<std::string>()));
                }
                for (const auto& v : f.value("files", nlohmann::json::array())) {
                    font.files.push_back(utf8::toWide(v.get<std::string>()));
                }
                inv.fonts.push_back(std::move(font));
            }
            for (const auto& a : doc.value("systemApps", nlohmann::json::array())) {
                inv.systemApps.push_back({w(a, "fullName"), w(a, "family"), w(a, "name"), w(a, "displayName"), w(a, "folder"),
                                          a.value("size", std::uint64_t{0})});
            }
            m_demoInventory = std::move(inv);
        }
    }
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
        // D-082: what they need, as Windows 10 22H2 lists it (there VCLibs is a provisioned package).
        for (auto& item : items) {
            const std::wstring& id = item.package.displayName;
            if (id == L"Microsoft.WindowsStore" || id == L"Microsoft.WindowsCalculator" || id == L"Microsoft.Windows.Photos" ||
                id == L"Microsoft.ZuneMusic" || id == L"Microsoft.BingWeather") {
                item.needs = {L"Microsoft.VCLibs.140.00"};
            }
        }
        // Sizes as measured in Windows 11 25H2 (26200.8037) Pro.
        constexpr std::uint64_t mb = 1024 * 1024;
        if (m_demoInventory) {
            m_state->setImageInventory(AppState::ImageInventory{mountDir, std::move(*m_demoInventory)});
            m_demoInventory.reset();
        }
        m_state->setSystemComponents(AppState::SystemComponents{
            mountDir,
            {{"edge", {true, 803 * mb}}, {"edge-webview", {true, 796 * mb}}, {"edge-update", {true, 806 * mb}},
             {"onedrive", {true, 197 * mb}}, {"winre", {true, 643 * mb}},
             // Package-level components (D-059), from the component store of the same image.
             {"telemetry", {true, 11 * mb}}, {"defender-definitions", {true, 483 * mb}}, {"app-guard", {true, 1 * mb}},
             {"photo-viewer", {true, 19 * mb}}, {"media-streaming", {true, 12 * mb}}, {"casting", {true, 4 * mb}},
             {"wmp-sharing", {true, 4 * mb}}, {"play-to", {true, 4 * mb}}, {"spatial-audio", {true, 6 * mb}},
             {"screensavers-3d", {true, 2 * mb}}, {"fonts-jpan", {true, 81 * mb}}, {"fonts-hans", {true, 56 * mb}},
             {"fonts-hant", {true, 26 * mb}}, {"fonts-kore", {true, 17 * mb}}, {"appv", {true, 24 * mb}},
             {"uev", {true, 15 * mb}}, {"branchcache", {true, 8 * mb}}, {"kiosk", {true, 8 * mb}}, {"fci", {true, 8 * mb}},
             {"work-folders", {true, 4 * mb}}, {"offline-files", {true, 4 * mb}}, {"remoteapp", {true, 4 * mb}},
             {"remote-assistance", {true, 3 * mb}}, {"take-a-test", {true, 2 * mb}}, {"edge-devtools", {true, 11 * mb}},
             {"pos", {true, 8 * mb}}, {"recovery-media", {true, 7 * mb}}, {"help", {true, 6 * mb}},
             {"bio-enrollment", {true, 5 * mb}},
             // Deep removal (D-060): inbox drivers of legacy classes, WinSxS payload of the same image.
             {"deep-modem", {true, 28 * mb}}, {"deep-tape", {true, 2 * mb}}, {"deep-floppy", {true, 1 * mb}},
             {"deep-firewire", {true, 1 * mb}}, {"deep-pcmcia", {true, 1 * mb}}, {"deep-pos", {true, 1 * mb}},
             // D-079: what shrinking WinSxS frees in the same image (wlcli store-shrink --dry-run).
             {"component-store-shrink", {true, 3041 * mb}}}});
        if (!m_options.demoStoreShare.empty()) {
            // D-113: a real image's store split (wlcli store-share <mount> components.json).
            auto known = *m_state->systemComponents();
            std::ifstream in(std::filesystem::path(m_options.demoStoreShare), std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                const auto o = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
                if (o.is_discarded() || !o.contains("id")) {
                    continue;
                }
                core::StoreShare share;
                share.bytes64 = o.value("bytes64", std::uint64_t{0});
                share.bytes32 = o.value("bytes32", std::uint64_t{0});
                for (const auto& n : o.value("names64", nlohmann::json::array())) {
                    share.names64.push_back(utf8::toWide(n.get<std::string>()));
                }
                for (const auto& n : o.value("names32", nlohmann::json::array())) {
                    share.names32.push_back(utf8::toWide(n.get<std::string>()));
                }
                if (!share.empty()) {
                    known.store[o["id"].get<std::string>()] = std::move(share);
                }
            }
            m_state->setSystemComponents(std::move(known));
        }
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
        if (!m_options.demoReveal.empty()) {
            m_shell->revealComponent(m_options.demoReveal); // D-113: a component and the rows above it, open
        }
        if (m_options.demoCompat) {
            m_shell->showCompat();
        }
    }
    if (m_options.demoUpdate) {
        demoUpdate(); // D-140
    }
    if (!m_options.demoToast.empty()) {
        const auto bar = m_options.demoToast.find(L'|');
        m_shell->showToast(ui::InfoKind::Warning, m_options.demoToast.substr(0, bar),
                           bar == std::wstring::npos ? std::wstring() : m_options.demoToast.substr(bar + 1));
    }
    if (m_options.demoAppMenu) {
        m_shell->showAppMenu();
    }
    if (m_options.demoFeatures) {
        using core::OptionalFeature;
        using S = core::ServicingState;
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        auto feature = [](std::wstring name, std::wstring display, S state) {
            return OptionalFeature{OptionalFeature::Kind::Feature, std::move(name), std::move(display), {}, state, 0, false};
        };
        auto child = [](std::wstring name, std::wstring display, S state, std::wstring parent) {
            OptionalFeature f{OptionalFeature::Kind::Feature, std::move(name), std::move(display), {}, state, 0, false};
            f.parent = std::move(parent);
            return f;
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
            // D-114: the feature tree and the language groups (the reference tool's Özellikler).
            feature(L"NetFx4-AdvSrvs", L".NET Framework 4.8 Gelişmiş Hizmetler", S::Installed),
            child(L"NetFx4Extended-ASPNET45", L"ASP.NET 4.8", S::Staged, L"NetFx4-AdvSrvs"),
            child(L"WCF-Services45", L"WCF Hizmetleri", S::Installed, L"NetFx4-AdvSrvs"),
            child(L"WCF-TCP-PortSharing45", L"TCP Bağlantı Noktası Paylaşımı", S::Installed, L"WCF-Services45"),
            child(L"WCF-HTTP-Activation45", L"HTTP Etkinleştirmesi", S::Staged, L"WCF-Services45"),
            capability(L"Language.Basic~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) yazma", 12),
            capability(L"Language.Handwriting~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) el yazısı", 9),
            capability(L"Language.OCR~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) OCR", 4),
            capability(L"Language.Basic~~~en-US~0.0.1.0", L"English (United States) typing", 14),
            capability(L"Language.TextToSpeech~~~en-US~0.0.1.0", L"English (United States) text-to-speech", 48),
            capability(L"Language.Fonts.Jpan~~~und-JPAN~0.0.1.0", L"Japanese Supplemental Fonts", 92),
        };
        m_state->setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Ready, mountDir,
                                                                std::move(items), {}});
        m_state->queue(FeatureController::operationFor(m_state->optionalFeatures()->items[4]));
        m_state->queue(FeatureController::operationFor(m_state->optionalFeatures()->items[7]));
        if (m_options.demoApply == L"confirm") {
            // "--demo-apply=confirm": the risk dialog with a list taller than the window (it scrolls).
            for (int i = 0; i < 40; ++i) {
                m_state->queue(core::ops::Operation{core::ops::OpKind::SetServiceStart, L"WdDemoService" + std::to_wstring(i),
                                                    L"4", core::ops::Risk::High, -(i + 1) * 1'048'576LL});
            }
            m_shell->showPage(m_options.page.value_or(PageId::Apply));
            m_shell->requestApply();
        } else if (m_options.demoApply == L"stopped" || m_options.demoApply == L"discarding" || m_options.demoApply == L"saving" ||
                   m_options.demoApply == L"discarded" || m_options.demoApply == L"saved" || m_options.demoApply == L"closefailed") {
            demoRunClose(m_options.demoApply);
        } else if (!m_options.demoApply.empty()) {
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
            } else if (m_options.demoApply == L"paused") {
                // D-106: the steps are done; it waits before the save for changes by hand.
                run.stage = AppState::ApplyRun::Stage::Paused;
                run.fraction = 0.8;
                log::info("apply", L"paused before saving: C:\\WinLove\\mount is open for changes by hand");
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
                const bool held = m_options.demoApply == L"held"; // a failed step: not saved, still mounted (A7)
                if ((m_options.demoApply == L"skipped" || held) && !result.report.results.empty()) {
                    // "--demo-apply=skipped": the first step refused as a permanent package (changes
                    // nothing, saved all the same); "held": the first step failed half way.
                    result.report.results.front().outcome =
                        held ? fail(ErrorCode::DismFailure, L"The process cannot access the file", L"remove capability",
                                    static_cast<std::int32_t>(0x80070020))
                             : fail(ErrorCode::DismFailure, L"DISM error", L"remove capability",
                                    static_cast<std::int32_t>(0x800F0825));
                    run.stepState.front() = 3;
                }
                result.committed = !held;
                result.held = held;
                result.commitTime = std::chrono::milliseconds(held ? 0 : 81'000);
                result.elapsed = std::chrono::milliseconds(112'000);
                run.result = std::move(result);
                run.queueVersionAtEnd = m_state->changes().version();
                m_state->setApplyRun(std::move(run));
                if (!held) {
                    m_state->setMounted(std::nullopt);
                }
            }
            if (m_state->applyRun() == std::nullopt) {
                m_state->setApplyRun(std::move(run));
            }
        }
        // A finished run is shown on the Apply page; a running one (and no run) on Features.
        const bool finished = m_options.demoApply == L"done" || m_options.demoApply == L"skipped" ||
                              m_options.demoApply == L"held" ||
                              (m_state->applyRun().has_value() && m_state->applyRun()->stage == AppState::ApplyRun::Stage::Done);
        if (m_options.demoApply != L"confirm") {
            m_shell->showPage(m_options.page.value_or(finished ? PageId::Apply : PageId::Features));
        }
    }
    if (m_options.demoRegistry) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        auto& registry = m_shell->registry();
        // D-067: values typed in the dialog (one switched off) and an imported file.
        const struct {
            const wchar_t* key;
            const wchar_t* name;
            core::RegValueType type;
            const wchar_t* data;
            bool afterSetup;
        } typed[] = {
            {L"HKLM\\SOFTWARE\\Contoso\\Agent", L"UpdateChannel", core::RegValueType::String, L"stable", false},
            {L"HKCU\\Software\\Contoso\\Agent", L"TrayIcon", core::RegValueType::Dword, L"0", true},
            {L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Contoso", L"Start", core::RegValueType::Dword, L"0x4", false},
            {L"HKCU\\Software\\Contoso\\Legacy", L"", core::RegValueType::DeleteKey, L"", true},
        };
        for (const auto& t : typed) {
            if (auto write = core::registryWriteFromInput(t.key, t.name, t.type, t.data)) {
                registry.addValue(std::move(*write), t.afterSetup);
            }
        }
        registry.toggle(1);
        if (auto sample = core::parseRegText(L"Windows Registry Editor Version 5.00\n[HKEY_CURRENT_USER\\Software\\Contoso]\n"
                                             L"\"Telemetry\"=dword:00000000\n\"Channel\"=\"stable\"\n")) {
            registry.addImport(L"C:\\Tweaks\\contoso-defaults.reg", std::move(*sample));
        }
        m_shell->showPage(m_options.page.value_or(PageId::Registry));
        if (m_options.demoRegistryDialog) {
            m_shell->editRegistryValue(0);
        }
    }
    if (m_options.demoTweaks) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        // The selections of screen 10.
        auto& controller = m_shell->imageSettings();
        if (m_options.demoImageValues) {
            // The image already has "Konum" off (the demo picks it too), "Uyarlanmış deneyimler" off (taken back
            // below) and "Geri bildirim" off, plus an OEM manufacturer.
            AppState::ImageValues values{AppState::ImageValues::Status::Ready, m_state->mounted()->mountDir, {}, {}, {}};
            for (const auto& setting : controller.catalog().settings()) {
                for (const auto& [id, option] : {std::pair<std::string_view, std::string_view>{"location", "off"},
                                                 {"tailored", "off"}, {"feedback", "off"}}) {
                    const auto it = std::ranges::find(setting.options, option, &ImageSettingOption::id);
                    if (setting.id == id && it != setting.options.end()) {
                        for (const auto& op : ImageSettingsController::operationsFor(
                                 setting, static_cast<int>(it - setting.options.begin()))) {
                            values.held.insert(AppState::imageValueKey(op.kind, op.target, op.value));
                        }
                    }
                }
            }
            values.texts[L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OEMInformation::Manufacturer"] = L"Contoso";
            m_state->setImageValues(std::move(values));
        }
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
        if (m_options.demoImageValues) {
            const auto& settings = controller.catalog().settings();
            if (const auto it = std::ranges::find(settings, "tailored", &ImageSetting::id); it != settings.end()) {
                controller.select(*it, it->defaultOption);
            }
        }
        m_shell->showPage(m_options.page.value_or(PageId::Tweaks));
        for (const auto& id : m_options.demoTweakTemplates) { // D-146
            m_shell->applyTweakTemplate(id);
        }
        if (!m_options.demoTweakFilter.empty()) {
            m_shell->filterTweakTemplate(m_options.demoTweakFilter);
        }
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
        if (m_options.demoWelcome) {
            controller.setWelcome(true); // D-084
        }
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
    if (m_options.demoFiles) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        // Real paths of this repository so sizes and icons are real.
        const auto repo = std::filesystem::current_path();
        (void)m_shell->filesForDemo().add({repo / L"tools", repo / L"README.md", repo / L"resources" / L"brand"}, L"Tools");
        (void)m_shell->filesForDemo().add({repo / L"resources" / L"fonts"}, L"Windows\\Web\\Wallpaper\\WinLove");
        m_shell->showPage(PageId::Files);
        if (*m_options.demoFiles == L"where") {
            m_shell->addFilesTo({repo / L"docs"});
        }
    }
    if (m_options.demoApps) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        // The package winget downloaded into the lab (build\lab\appx), read as the page would.
        const auto lab = std::filesystem::current_path() / L"build" / L"lab" / L"appx";
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(lab, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (core::isAppxFile(it->path())) {
                if (auto install = core::planAppxInstall(it->path(), L"x64")) {
                    m_state->queue(AppsController::operationFor(*install));
                }
            }
        }
        core::AppxInstall missing;
        missing.package = L"C:\\Apps\\Contoso.Notes_2.4.0.0_x64.msix";
        missing.name = L"Contoso.Notes";
        missing.displayName = L"Contoso Notes";
        missing.publisher = L"Contoso Ltd.";
        missing.version = L"2.4.0.0";
        missing.architectures = {L"x64"};
        missing.missing = {L"Microsoft.VCLibs.140.00.UWPDesktop"};
        m_state->queue(AppsController::operationFor(missing));
        auto& apps = m_shell->appsForDemo();
        apps.setBrowser(1);
        apps.mergeAssociations({{L".pdf", L"SumatraPDF", L"SumatraPDF"}, {L".mp4", L"VLC.mp4", L"VLC media player"},
                                {L"mailto", L"Thunderbird.Url.mailto", L"Thunderbird"}});
        m_shell->showPage(PageId::Apps);
        if (*m_options.demoApps == L"defaults") {
            if (auto* page = m_shell->appsPageForDemo()) {
                page->showDefaultsTab();
            }
        }
    }
    if (m_options.demoStore) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        m_shell->showPage(PageId::Apps);
        if (*m_options.demoStore == L"dialog") {
            m_shell->showStoreDialog();
            m_shell->storeResultsForDemo(L"whatsapp", {{L"9NKSQGP7F2NH", L"WhatsApp", L"WhatsApp Inc."},
                                                       {L"9NBDXK71NK08", L"WhatsApp Beta", L"WhatsApp Inc."},
                                                       {L"9WZDNCRFHWM4", L"Wikipedia", L"Wikimedia Foundation"},
                                                       {L"9N3SQK8PDS8G", L"ScreenToGif", L"Nicke"}});
        } else {
            AppState::StoreFetch fetch;
            fetch.stage = AppState::StoreFetch::Stage::Downloading;
            fetch.title = L"WhatsApp";
            fetch.doneBytes = 141'557'760;
            fetch.totalBytes = 367'321'088;
            m_state->setStoreFetch(std::move(fetch));
        }
    }
    if (m_options.demoStartMenu) {
        m_state->setMounted(MountedImage{L"C:\\", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        auto& pins = m_shell->startPinsForDemo();
        pins.preload();
        std::vector<core::StartApp> list;
        if (const auto* apps = pins.apps()) {
            for (const wchar_t* id : {L"File Explorer", L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App",
                                      L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
                                      L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel", L"MSEdge",
                                      L"Microsoft.Paint_8wekyb3d8bbwe!App", L"Microsoft.ScreenSketch_8wekyb3d8bbwe!App"}) {
                const auto it = std::ranges::find_if(*apps, [&](const core::StartApp& a) { return a.id == id || a.name == id; });
                if (it != apps->end()) {
                    list.push_back(*it);
                }
            }
        }
        pins.setPins(std::move(list));
        // D-083: a taskbar of one's own — File Explorer, Terminal, Notepad, Calculator, Settings.
        auto& bar = m_shell->taskbarPinsForDemo();
        bar.preload();
        std::vector<core::StartApp> barList;
        if (const auto* apps = bar.apps()) {
            for (const wchar_t* id : {L"Microsoft.Windows.Explorer", L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App",
                                      L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
                                      L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"}) {
                const auto it = std::ranges::find_if(*apps, [&](const core::StartApp& a) { return a.id == id; });
                if (it != apps->end()) {
                    barList.push_back(*it);
                }
            }
        }
        bar.setPins(std::move(barList));
        m_shell->showPage(PageId::StartMenu);
        if (m_options.demoStartMenuSettings || m_options.demoTaskbar) {
            if (auto* page = m_shell->startMenuPageForDemo()) {
                page->showTab(m_options.demoTaskbar ? StartMenuPage::kTabTaskbar : StartMenuPage::kTabSettings);
            }
        }
    }
    if (m_options.demoDownload >= 0) {
        m_shell->showPage(PageId::Download);
        if (auto* page = m_shell->downloadPageForDemo()) {
            page->demo(m_options.demoDownload);
        }
    }
    if (m_options.demoIcons) {
        // D-068: this PC's own Windows stands in for the mounted image (read only: nothing is applied).
        m_state->setMounted(MountedImage{L"C:\\", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        // The app's own icon from the repository (build\<preset>\bin → resources\brand).
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const auto brand = std::filesystem::path(exe).parent_path().parent_path().parent_path().parent_path() / L"resources" /
                           L"brand" / L"WinLove.ico";
        auto& icons = m_shell->iconsForDemo();
        for (const char* id : {"this-pc", "recycle-empty", "folder", "drive"}) {
            if (const auto* slot = findIconSlot(id)) {
                (void)icons.assign(*slot, brand);
            }
        }
        (void)icons.setShortcutArrowRemoved(true);
        auto& patches = m_shell->iconPatchForDemo();
        const std::wstring imageres = L"Windows\\SystemResources\\imageres.dll.mun";
        patches.preload(imageres);
        (void)patches.replace(imageres, core::ResourceKey{3, {}}, brand);
        (void)patches.replace(imageres, core::ResourceKey{109, {}}, brand);
        m_shell->showPage(PageId::Icons);
        if (auto* page = m_shell->iconsPageForDemo()) {
            page->showTab(m_options.demoIconsRedirect ? 1 : 0);
        }
    }
    if (m_options.demoMounts) {
        auto check = [](const wchar_t* folder, const wchar_t* wim, int index, const wchar_t* name, core::MountState state, bool readOnly) {
            core::MountCheck c;
            c.folder = folder;
            c.imageName = name;
            c.state = state;
            if (state != core::MountState::Orphaned) {
                c.record = core::MountInfo{folder, wim, index, readOnly};
            }
            return c;
        };
        AppState::SystemMounts mounts;
        mounts.status = AppState::SystemMounts::Status::Ready;
        mounts.items = {
            check(L"C:\\Tools\\Mount\\Win11_25H2", L"D:\\ISO\\Win11_25H2\\sources\\install.wim", 6, L"Windows 11 Pro", core::MountState::Ok, false),
            check(L"D:\\Work\\mount", L"D:\\Work\\Win10_22H2.wim", 1, L"Windows 10 Home", core::MountState::NeedsRemount, false),
            check(L"C:\\Mount\\boot", L"D:\\ISO\\Win11_25H2\\sources\\boot.wim", 2, L"Microsoft Windows Setup (amd64)", core::MountState::Ok, true),
            check(L"E:\\old-mount", L"E:\\gone\\install.wim", 3, L"", core::MountState::ImageMissing, false),
        };
        m_state->setSystemMounts(std::move(mounts));
        m_shell->showPage(PageId::Source);
        demoAdoption(m_options.demoMountsVariant);
    }
    if (m_options.demoLanguages) {
        // 25H2 Pro (26200.8037) with Turkish and the components 25H2 installs; English queued from
        // Windows Update (D-061): names and sizes as uupdump.net listed them on 2026-10-04.
        core::SourceInfo source;
        source.path = L"C:\\ISO\\Win11_25H2_Turkish_x64_v2.iso";
        source.format = core::ImageFormat::Iso;
        core::ImageInfo pro;
        pro.index = 4;
        pro.name = L"Windows 11 Pro";
        pro.architecture = core::Architecture::X64;
        pro.build = 26200;
        pro.spBuild = 8037;
        source.install.images = {pro};
        m_state->setSource(std::move(source));
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        static constexpr const wchar_t* kComponents[] = {
            L"Microsoft-Windows-InternetExplorer-Optional-Package~amd64", L"Microsoft-Windows-MediaPlayer-Package~amd64",
            L"Microsoft-Windows-MediaPlayer-Package~wow64",               L"Microsoft-Windows-Notepad-System-FoD-Package~amd64",
            L"Microsoft-Windows-Notepad-System-FoD-Package~wow64",        L"Microsoft-Windows-PowerShell-ISE-FOD-Package~amd64",
            L"Microsoft-Windows-PowerShell-ISE-FOD-Package~wow64",        L"Microsoft-Windows-Printing-PMCPPC-FoD-Package~amd64",
            L"Microsoft-Windows-SenseClient-FoD-Package~amd64",           L"Microsoft-Windows-StepsRecorder-Package~amd64",
            L"Microsoft-Windows-StepsRecorder-Package~wow64",             L"Microsoft-Windows-VBSCRIPT-FoD-Package~amd64",
            L"Microsoft-Windows-VBSCRIPT-FoD-Package~wow64",              L"Microsoft-Windows-WMIC-FoD-Package~amd64",
            L"Microsoft-Windows-WMIC-FoD-Package~wow64"};
        auto split = [](const wchar_t* c) {
            const std::wstring s = c;
            return std::pair{s.substr(0, s.find(L'~')), s.substr(s.find(L'~') + 1)};
        };
        core::ImageIntl intl;
        intl.current = {L"tr-TR", L"tr-TR", L"tr-TR", L"041f:0000041f", L"Turkey Standard Time"};
        intl.languages = {L"tr-TR"};
        std::vector<std::wstring> packages{L"Microsoft-Windows-Client-LanguagePack-Package~31bf3856ad364e35~amd64~tr-TR~10.0.26100.8037"};
        for (const wchar_t* f : {L"Basic", L"Handwriting", L"OCR", L"TextToSpeech"}) {
            packages.push_back(std::format(L"Microsoft-Windows-LanguageFeatures-{}-tr-tr-Package~31bf3856ad364e35~amd64~~10.0.26100.8036", f));
        }
        for (const wchar_t* c : kComponents) {
            const auto [name, arch] = split(c);
            packages.push_back(std::format(L"{}~31bf3856ad364e35~{}~~10.0.26100.8036", name, arch));
            packages.push_back(std::format(L"{}~31bf3856ad364e35~{}~tr-TR~10.0.26100.7824", name, arch));
        }
        m_state->setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Ready, m_state->mounted()->mountDir, intl,
                                                  std::move(packages), {}});
        // What uupdump.net lists for a few languages of 26200.8037 (sizes as listed).
        std::vector<core::UupFile> files;
        auto file = [&](std::wstring name, std::uint64_t kb) {
            files.push_back(core::UupFile{std::move(name), kb << 10, L"", std::wstring(64, L'0'), L"http://tlu.dl.delivery.mp.microsoft.com/x"});
        };
        struct Lang {
            const wchar_t* tag;
            std::uint64_t pack, basic, hand, ocr, tts, speech; // KB, 0 = none
        };
        static constexpr Lang kLangs[] = {
            {L"de-DE", 22100, 23400, 12100, 160, 48700, 62300}, {L"en-GB", 18600, 21700, 11900, 155, 44200, 59100},
            {L"en-US", 19008, 21795, 12288, 155, 49006, 62889}, {L"es-ES", 21000, 22600, 11700, 160, 42000, 31000},
            {L"fr-FR", 21500, 22800, 11800, 158, 41500, 30900}, {L"it-IT", 20400, 22100, 11600, 156, 20100, 0},
            {L"ja-JP", 31200, 61500, 24800, 210, 52100, 58400}, {L"nl-NL", 19800, 21900, 11400, 152, 6100, 0},
            {L"pl-PL", 20100, 22000, 11300, 151, 5900, 0},      {L"pt-BR", 20900, 22300, 11500, 153, 20300, 6500},
            {L"ru-RU", 20600, 22500, 11700, 157, 5600, 0},      {L"tr-TR", 20300, 21600, 11200, 150, 5400, 0},
            {L"uk-UA", 19900, 14100, 0, 0, 0, 0},               {L"zh-CN", 33400, 72800, 31600, 230, 54400, 41000},
        };
        for (const auto& l : kLangs) {
            const std::wstring lower = text::lower(l.tag);
            file(std::format(L"Microsoft-Windows-Client-LanguagePack-Package-amd64-{}.esd", l.tag), l.pack);
            const std::pair<const wchar_t*, std::uint64_t> features[] = {
                {L"Basic", l.basic}, {L"Handwriting", l.hand}, {L"OCR", l.ocr}, {L"TextToSpeech", l.tts}, {L"Speech", l.speech}};
            for (const auto& [name, kb] : features) {
                if (kb > 0) {
                    file(std::format(L"Microsoft-Windows-LanguageFeatures-{}-{}-Package-amd64.cab", name, lower), kb);
                }
            }
            for (const wchar_t* c : kComponents) {
                const auto [name, arch] = split(c);
                file(std::format(L"{}-{}-{}.cab", name, arch, l.tag), 40);
            }
        }
        file(L"Microsoft-Windows-LanguageFeatures-Fonts-Jpan-Package-amd64.cab", 81200);
        file(L"Microsoft-Windows-LanguageFeatures-Fonts-Hans-Package-amd64.cab", 64100);
        auto languages = core::uupLanguages(files, L"x64");
        auto& controller = m_shell->languagesForDemo();
        const LanguageTarget languageTarget{26200, 8037, L"x64"};
        if (*m_options.demoLanguages == L"dialog") {
            m_shell->showPage(PageId::Languages);
            m_shell->languageOffersForDemo(languageTarget, languages);
        } else {
            const auto en = std::ranges::find_if(languages, [](const core::UupLanguage& l) { return l.language == L"en-US"; });
            std::vector<core::LanguagePackFile> packs;
            for (const auto& f : controller.pick(*en, LanguageParts{})) {
                auto p = core::classifyLanguageName(L"C:\\WinLove\\work\\languages\\26200.8037-x64\\" + core::uupSaveName(f));
                p.size = f.source.size;
                packs.push_back(std::move(p));
            }
            controller.queuePacks(packs);
            core::IntlSettings settings;
            settings.uiLanguage = L"en-US";
            settings.timeZone = L"GMT Standard Time";
            controller.setSettings(settings);
            if (*m_options.demoLanguages == L"fetch") {
                AppState::LanguageFetch fetch;
                fetch.stage = AppState::LanguageFetch::Stage::Downloading;
                fetch.languages = {L"de-DE", L"ja-JP"};
                fetch.doneBytes = 96'468'992;
                fetch.totalBytes = 412'090'368;
                m_state->setLanguageFetch(std::move(fetch));
            }
            m_shell->showPage(PageId::Languages);
        }
    }
    if (m_options.demoImageDrivers) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        auto driver = [](const wchar_t* published, const wchar_t* original, const wchar_t* cls, const wchar_t* desc,
                         const wchar_t* provider, const wchar_t* version, const wchar_t* date, bool boot) {
            core::DriverEntry d;
            d.publishedName = published;
            d.originalFileName = original;
            d.className = cls;
            d.classDescription = desc;
            d.provider = provider;
            d.version = version;
            d.date = date;
            d.bootCritical = boot;
            d.signed_ = true;
            return d;
        };
        std::vector<core::DriverEntry> items{
            driver(L"oem0.inf", L"rt640x64.inf", L"Net", L"Ağ bağdaştırıcıları", L"Realtek", L"10.72.524.2024", L"2024-05-24", false),
            driver(L"oem1.inf", L"iastorvd.inf", L"SCSIAdapter", L"Depolama denetleyicileri", L"Intel Corporation", L"20.0.0.1038", L"2023-11-02", true),
            driver(L"oem2.inf", L"nvlddmkm.inf", L"Display", L"Görüntü bağdaştırıcıları", L"NVIDIA", L"32.0.15.6094", L"2024-08-14", false),
            driver(L"oem3.inf", L"ibtusb.inf", L"Bluetooth", L"Bluetooth", L"Intel Corporation", L"23.60.5.10", L"2024-06-11", false),
        };
        m_state->setImageDrivers(AppState::ImageDrivers{AppState::ImageDrivers::Status::Ready, m_state->mounted()->mountDir,
                                                        std::move(items), {}});
        m_state->queue(ImageDriverController::operationFor(m_state->imageDrivers()->items[2]));
        m_shell->showPage(PageId::Drivers);
        if (auto* page = m_shell->driversPageForDemo()) {
            page->showImageTab();
        }
    }
    if (m_options.demoWifi) {
        m_state->setMounted(MountedImage{LR"(C:\WinLove\mount)", LR"(C:\WinLove\work\sources\install.wim)", 4,
                                         L"Windows 11 Pro"});
        m_shell->showPage(PageId::PostSetup);
        m_shell->wifiDialogForDemo();
    }
    if (m_options.demoBranding) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        auto& branding = m_shell->branding();
        branding.setOem(L"Manufacturer", L"Kuzey Bilgisayar");
        branding.setOem(L"Model", L"KB Ofis 2026");
        branding.setOem(L"SupportPhone", L"+90 212 555 01 01");
        branding.setOem(L"SupportURL", L"https://destek.example.com");
        const std::filesystem::path web = L"C:\\Windows\\Web";
        (void)branding.setPicture(core::PictureSlot::Wallpaper, web / L"Wallpaper" / L"Windows" / L"img0.jpg");
        (void)branding.setPicture(core::PictureSlot::LockScreen, web / L"Screen" / L"img100.jpg");
        branding.addFonts({L"C:\\Windows\\Fonts\\segoeui.ttf", L"C:\\Windows\\Fonts\\cambria.ttc",
                           L"C:\\Windows\\Fonts\\consola.ttf"});
        m_shell->showPage(m_options.page.value_or(PageId::Branding));
    }
    if (m_options.demoTasks || m_options.demoHosts) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4,
                                         L"Windows 11 Pro"});
        AppState::ImageValues values{AppState::ImageValues::Status::Ready, m_state->mounted()->mountDir, {}, {}, {}};
        values.disabledTasks = {L"\\Microsoft\\Windows\\Autochk\\Proxy",
                                L"\\Microsoft\\Windows\\Customer Experience Improvement Program\\Consolidator"};
        for (const auto& t : values.disabledTasks) {
            values.held.insert(AppState::imageValueKey(core::ops::OpKind::SetTaskState, t, L"disabled"));
        }
        auto& hosts = m_shell->hosts();
        if (!hosts.lists().empty()) {
            values.hostsSections[hosts.lists().front().id] = hosts.lists().front().text();
            values.held.insert(AppState::imageValueKey(core::ops::OpKind::SetHosts, hosts.lists().front().id,
                                                       hosts.lists().front().text()));
        }
        m_state->setImageValues(std::move(values));
        if (m_options.demoTasks) {
            m_shell->tasks().applyRecommended();
            m_shell->tasks().addCustom(L"\\Microsoft\\Office\\OfficeTelemetryAgentLogOn");
            m_shell->showPage(m_options.page.value_or(PageId::Tasks));
            if (m_options.demoTaskCreate) {
                m_shell->createTaskDialogForDemo();
            }
        } else {
            if (hosts.lists().size() > 1) {
                hosts.toggle(hosts.lists()[1]);
            }
            hosts.importText(L"0.0.0.0 tracker.example.com\n0.0.0.0 ads.example.net\n127.0.0.1 localhost\n");
            m_shell->showPage(m_options.page.value_or(PageId::Hosts));
        }
    }
    if (m_options.demoPolicies) {
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        auto catalog = core::policy::readPolicyDefinitions(L"C:\\Windows\\PolicyDefinitions",
                                                           m_options.language == Language::Turkish ? L"tr-TR" : L"en-US");
        if (catalog) {
            m_state->setPolicyList(AppState::PolicyList{AppState::PolicyList::Status::Ready, mountDir,
                                                        std::make_shared<const core::policy::PolicyCatalog>(std::move(*catalog)), {}});
        }
        auto& policies = m_shell->policies();
        auto rowOf = [&](std::wstring_view id) -> std::optional<PolicyController::Row> {
            for (const auto& row : policies.rows()) {
                if (const auto* p = policies.policyOf(row); p && p->id == id) {
                    return row;
                }
            }
            return std::nullopt;
        };
        if (const auto row = rowOf(L"Microsoft.Policies.WindowsUpdate:AutoUpdateCfg")) {
            (void)policies.set(*row, core::policy::PolicyState::Enabled);
        }
        if (const auto row = rowOf(L"Microsoft.Policies.ControlPanel:NoControlPanel")) {
            (void)policies.set(*row, core::policy::PolicyState::Enabled);
        }
        if (const auto row = rowOf(L"Microsoft.Policies.DataCollection:AllowTelemetry")) {
            (void)policies.set(*row, core::policy::PolicyState::Disabled);
        }
        m_shell->showPage(m_options.page.value_or(PageId::Policies));
        if (auto* page = m_shell->policiesPageForDemo()) {
            page->search(m_options.language == Language::Turkish ? L"otomatik güncelleştirme" : L"automatic update");
            page->selectRow(0);
        }
        if (*m_options.demoPolicies == L"options") {
            if (const auto row = rowOf(L"Microsoft.Policies.WindowsUpdate:AutoUpdateCfg")) {
                m_shell->policyOptionsForDemo(*row);
            }
        }
    }
    if (m_options.demoContextMenu) {
        const std::filesystem::path mountDir = L"C:\\WinLove\\mount";
        m_state->setMounted(MountedImage{mountDir, L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        using Kind = core::ContextMenuEntry::Kind;
        auto entry = [](Kind kind, const wchar_t* id, const wchar_t* name, std::vector<std::wstring> places, const wchar_t* source,
                        bool extended = false, bool hidden = false) {
            core::ContextMenuEntry e;
            e.kind = kind;
            e.id = id;
            e.name = name;
            e.places = std::move(places);
            e.source = source;
            e.extended = extended;
            e.hidden = hidden;
            if (kind != Kind::Verb) {
                e.clsid = id;
            }
            return e;
        };
        const bool tr = m_options.language == Language::Turkish;
        std::vector<core::ContextMenuEntry> items{
            entry(Kind::Packaged, L"{9F156763-7844-4DC4-B2B1-901F640F5155}", tr ? L"Terminal'de aç" : L"Open in Terminal",
                  {L"Directory", L"Directory\\Background"}, L"Microsoft.WindowsTerminal"),
            entry(Kind::Handler, L"{596AB062-B4D2-4215-9F74-E9109B0A8153}", tr ? L"Önceki sürümleri geri yükle" : L"Restore previous versions",
                  {L"AllFilesystemObjects", L"Directory", L"Drive"}, L"%SystemRoot%\\system32\\twext.dll"),
            entry(Kind::Handler, L"{E2BF9676-5F8F-435C-97EB-11607A5BEDF7}", tr ? L"Paylaş" : L"Share", {L"AllFilesystemObjects"},
                  L"%SystemRoot%\\system32\\ntshrui.dll"),
            entry(Kind::Verb, L"Directory\\Background\\shell\\cmd", tr ? L"Komut penceresini burada aç" : L"Open command window here",
                  {L"Directory\\Background"}, L"", true),
            entry(Kind::Verb, L"Drive\\shell\\encrypt-bde-elev", tr ? L"BitLocker'ı aç" : L"Turn on BitLocker", {L"Drive"}, L""),
            entry(Kind::Handler, L"{09A47860-11B0-4DA5-AFA5-26D86198A780}", tr ? L"Microsoft Defender ile tara" : L"Scan with Microsoft Defender",
                  {L"*", L"Directory", L"Drive"}, L"X:\\Program Files\\Windows Defender\\shellext.dll"),
            entry(Kind::Verb, L"*\\shell\\removeproperties", L"removeproperties", {L"*"}, L"", false, true),
        };
        items[3].command = L"cmd.exe /s /k pushd \"%V\"";
        m_state->setContextMenuList(AppState::ContextMenuList{AppState::ContextMenuList::Status::Ready, mountDir, items, {}});
        auto& menu = m_shell->contextMenu();
        menu.setHidden(m_state->contextMenuList()->items[2], true);
        core::ContextMenuItem own;
        own.name = tr ? L"VS Code ile aç" : L"Open with VS Code";
        own.command = L"\"C:\\Program Files\\Microsoft VS Code\\Code.exe\" \"%1\"";
        own.places = {L"*", L"Directory"};
        (void)menu.addItem(own);
        m_shell->showPage(m_options.page.value_or(PageId::ContextMenu));
        if (auto* page = m_shell->contextMenuPageForDemo()) {
            page->selectRow(3);
        }
        if (*m_options.demoContextMenu == L"add") {
            m_shell->contextMenuAddForDemo();
        }
    }
    if (m_options.demoPresetAsk) {
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
        m_state->queueMany({core::ops::Operation{core::ops::OpKind::DisableFeature, L"Printing-XPSServices-Features"},
                            core::ops::Operation{core::ops::OpKind::DisableFeature, L"WorkFolders-Client"},
                            core::ops::Operation{core::ops::OpKind::SetServiceStart, L"DiagTrack", L"disabled"}});
        Preset preset;
        preset.name = m_options.language == Language::Turkish ? L"Oyun bilgisayarı" : L"Gaming PC";
        preset.changes.add(core::ops::Operation{core::ops::OpKind::DisableFeature, L"Recall"});
        m_shell->showPage(m_options.page.value_or(PageId::Presets));
        m_shell->applyPresetForDemo(preset);
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
                      const wchar_t* arch, std::uint64_t kb, const wchar_t* hwid = L"PCI\\VEN_8086&DEV_0000", int ifType = 0) {
            std::wstring text = std::wstring(L"[Version]\nClass=") + cls + L"\nProvider=\"" + provider +
                                L"\"\nCatalogFile=" + std::filesystem::path(file).stem().wstring() +
                                L".cat\nDriverVer=05/14/2024," + ver + L"\n[Manufacturer]\n%M%=M," + arch + L"\n[M." + arch +
                                L"]\n%D%=Install, " + hwid + L"\n";
            if (ifType != 0) {
                text += L"[Install.NDI]\n*IfType = " + std::to_wstring(ifType) + L"\n";
            }
            auto d = core::parseInfText(text, std::filesystem::path(L"D:\\Drivers\\Dell\\") / file);
            d.size = kb * 1024;
            return d;
        };
        std::vector<core::DriverInf> infs{
            inf(L"e2f68.inf", L"Net", L"Intel", L"12.19.2.45", L"NTamd64", 1840, L"PCI\\VEN_8086&DEV_15F3", 6),
            inf(L"netwtw08.inf", L"Net", L"Intel", L"23.60.1.2", L"NTamd64", 38200, L"PCI\\VEN_8086&DEV_2723", 71),
            inf(L"rt640x64.inf", L"Net", L"Realtek", L"10.68.815.2023", L"NTamd64", 1200, L"PCI\\VEN_10EC&DEV_8168", 6),
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
    if (m_options.demoBootDrivers) {
        // D-134: a 25H2 x64 medium whose boot.wim carries two packages already (as read from its file list).
        const std::wstring variant = *m_options.demoBootDrivers;
        const bool iso = variant == L"iso" || variant == L"iso-done" || variant == L"iso-running";
        core::SourceInfo source;
        source.path = L"D:\\ISO\\Win11_25H2_Turkish_x64.iso";
        source.format = core::ImageFormat::Iso;
        source.installImage = L"sources/install.wim";
        source.installImageSize = 7217280579ull;
        for (const auto& [index, name] : {std::pair{4, L"Windows 11 Pro"}, std::pair{1, L"Windows 11 Home"}}) {
            core::ImageInfo image;
            image.index = index;
            image.name = name;
            image.architecture = core::Architecture::X64;
            image.build = 26200;
            image.spBuild = 8037;
            source.install.images.push_back(image);
        }
        std::ranges::sort(source.install.images, {}, &core::ImageInfo::index);
        core::WimFile boot;
        boot.header.bootIndex = 2;
        for (const int index : {1, 2}) {
            core::ImageInfo pe;
            pe.index = index;
            pe.name = index == 1 ? L"Microsoft Windows PE (amd64)" : L"Microsoft Windows Setup (amd64)";
            pe.architecture = core::Architecture::X64;
            pe.build = 26100;
            boot.images.push_back(pe);
        }
        source.boot = boot;
        const std::filesystem::path sourcePath = source.path;
        m_state->setSource(std::move(source));
        auto present = [](const wchar_t* published, const wchar_t* original, const wchar_t* cls, const wchar_t* provider,
                          const wchar_t* version) {
            core::WimDriver d;
            d.publishedName = published;
            d.originalName = original;
            d.inf = core::parseInfText(std::wstring(L"[Version]\nClass=") + cls + L"\nProvider=\"" + provider +
                                           L"\"\nDriverVer=06/06/2024," + version + L"\n",
                                       original);
            return d;
        };
        AppState::MediaDrivers media;
        media.status = AppState::MediaDrivers::Status::Ready;
        media.source = sourcePath;
        media.drivers.bootIndex = 2;
        media.drivers.architecture = core::Architecture::X64;
        media.drivers.installRead = true;
        media.drivers.boot = {present(L"oem0.inf", L"pvscsi.inf", L"SCSIAdapter", L"Broadcom Inc.", L"1.3.28.0"),
                              present(L"oem1.inf", L"rt640x64.inf", L"Net", L"Realtek", L"10.68.815.2023")};
        m_state->setMediaDrivers(std::move(media));
        if (variant == L"host") {
            // "Bu bilgisayardan al" on the boot tab: this PC's packages (a VMD laptop with Wi-Fi, a GeForce,
            // VMware's host adapters), and what the tab ticks of them.
            m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
            auto host = [](const wchar_t* folder, const wchar_t* file, const wchar_t* cls, const wchar_t* provider, const wchar_t* ver,
                           const wchar_t* hwid, int ifType, std::uint64_t kb) {
                std::wstring text = std::wstring(L"[Version]\nClass=") + cls + L"\nProvider=\"" + provider + L"\"\nCatalogFile=x.cat\nDriverVer=05/08/2026," +
                                    ver + L"\n[Manufacturer]\n%M%=M,NTamd64\n[M.NTamd64]\n%D%=Install, " + hwid + L"\n";
                if (ifType != 0) {
                    text += L"[Install.NDI]\n*IfType = " + std::to_wstring(ifType) + L"\n";
                }
                auto d = core::parseInfText(text, std::filesystem::path(L"C:\\WinLove\\host-drivers") / folder / file);
                d.size = kb * 1024;
                return d;
            };
            std::vector<core::DriverInf> infs{
                host(L"oem0", L"iastorvd.inf", L"SCSIAdapter", L"Intel Corporation", L"20.2.1.1016", L"PCI\\VEN_8086&DEV_A77F", 0, 3400),
                host(L"oem603", L"rt68cx21x64.inf", L"Net", L"Realtek", L"1168.30.50.508", L"PCI\\VEN_10EC&DEV_8125", 6, 2100),
                host(L"oem604", L"netwtw6e.inf", L"Net", L"Intel", L"24.50.0.4", L"PCI\\VEN_8086&DEV_51F1", 71, 41000),
                host(L"oem92", L"netadapter.inf", L"Net", L"Broadcom Inc.", L"17.6.0.0", L"*VMnetAdapter1", 6, 90),
                host(L"oem611", L"nvhda.inf", L"MEDIA", L"NVIDIA Corporation", L"1.4.3.4", L"HDAUDIO\\FUNC_01&VEN_10DE", 0, 1800),
                host(L"oem613", L"iigd_dch.inf", L"Display", L"Intel Corporation", L"32.0.101.6913", L"PCI\\VEN_8086&DEV_A7A0", 0, 254000),
                host(L"oem584", L"ialpss2_i2c_adl.inf", L"System", L"Intel Corporation", L"30.100.2531.31", L"PCI\\VEN_8086&DEV_7A4C", 0, 300),
            };
            const auto picked = ImageDriverController::setupPick(infs, core::Architecture::X64, m_state->mediaDrivers()->drivers.boot);
            for (const auto& d : infs) {
                // As ImageDriverController::setBoot does with an image mounted: the storage one goes to the image too.
                if (std::ranges::find(picked, d.path) != picked.end() && core::setupRole(d) == core::SetupRole::Storage) {
                    m_state->queue(ImageDriverController::addOperationFor(d));
                }
            }
            m_state->addDriverScan(L"C:\\WinLove\\host-drivers", std::move(infs));
            m_state->setBootDrivers(picked, true);
            m_shell->showPage(PageId::Drivers);
            if (auto* page = m_shell->driversPageForDemo()) {
                page->showBootTab();
            }
            m_shell->showToast(ui::InfoKind::Success,
                               m_strings->format(Str::DriversHostPickedBoot, {{L"n", L"7"}, {L"p", std::to_wstring(picked.size())}}),
                               L"C:\\WinLove\\host-drivers");
        } else if (iso) {
            // The ISO page: no mount (the build needs none), a scan, three drivers for boot.wim — the
            // storage one is in no edition of the install image and not queued.
            auto inf = [](const wchar_t* file, const wchar_t* cls, const wchar_t* provider, const wchar_t* hwid, int ifType) {
                std::wstring text = std::wstring(L"[Version]\nClass=") + cls + L"\nProvider=\"" + provider + L"\"\nCatalogFile=x.cat\nDriverVer=05/14/2024,1.0.0.0\n[Manufacturer]\n%M%=M,NTamd64\n[M.NTamd64]\n%D%=Install, " + hwid + L"\n";
                if (ifType != 0) {
                    text += L"[Install.NDI]\n*IfType = " + std::to_wstring(ifType) + L"\n";
                }
                return core::parseInfText(text, std::filesystem::path(L"D:\\Drivers\\Dell\\") / file);
            };
            std::vector<core::DriverInf> infs{inf(L"iaStorVD.inf", L"SCSIAdapter", L"Intel Corporation", L"PCI\\VEN_8086&DEV_A77F", 0),
                                              inf(L"e2f68.inf", L"Net", L"Intel", L"PCI\\VEN_8086&DEV_15F3", 6),
                                              inf(L"qcwlan_arm.inf", L"Net", L"Qualcomm", L"PCI\\VEN_17CB&DEV_1103", 71)};
            m_state->addDriverScan(L"D:\\Drivers\\Dell", infs);
            m_state->setBootDrivers({infs[0].path, infs[1].path, infs[2].path}, true);
            if (variant == L"iso-done") {
                AppState::IsoRun run;
                run.running = false;
                run.output = L"C:\\Users\\Public\\Desktop\\Win11_25H2_WinLove.iso";
                run.result = core::IsoResult{7406092288ull, {}};
                run.bootDriversAdded = {infs[0].path, infs[1].path};
                run.bootDriversRefused = {core::DriverRefusal{infs[2].path, core::DriverRefusal::Why::Architecture, L"arm64"},
                                          core::DriverRefusal{L"D:\\Drivers\\Old\\oldraid.inf", core::DriverRefusal::Why::CatalogMissing,
                                                              L"oldraid.cat"}};
                m_state->setIsoRun(std::move(run));
            } else if (variant == L"iso-running") { // D-142: the build under way — its progress row and ring
                AppState::IsoRun run;
                run.running = true;
                run.stage = 2;
                run.fraction = 0.42;
                run.startedMs = ui::nowMs();
                m_state->setIsoRun(std::move(run));
            }
            m_shell->showPage(PageId::Iso);
        } else if (!m_state->driverScan().infs.empty()) {
            // With --demo-drivers: its fake mount and queue (the two storage drivers are queued for the image).
            const auto& infs = m_state->driverScan().infs;
            std::vector<std::filesystem::path> ticked;
            for (const auto& d : infs) {
                const auto name = d.path.filename().wstring();
                if (name == L"e2f68.inf" || name == L"iaStorVD.inf" || name == L"stornvme_oem.inf") {
                    ticked.push_back(d.path);
                }
            }
            m_state->setBootDrivers(ticked, true);
            if (variant == L"storage-only") {
                // The storage driver ticked for boot.wim only: not in the image's queue.
                for (const auto& d : infs) {
                    if (d.path.filename() == L"iaStorVD.inf") {
                        m_state->unqueue(core::ops::OpKind::AddDriver, d.path.wstring());
                    }
                }
            }
            m_shell->showPage(PageId::Drivers);
            if (auto* page = m_shell->driversPageForDemo()) {
                page->showBootTab();
                page->setArchFilterForDemo(0); // the ARM64 INF in view, dimmed
            }
        }
    }
    if (m_options.openPath) {
        // Headless: open synchronously so the frame shows the Images page with real data.
        if (auto info = core::openSource(*m_options.openPath)) {
            m_state->setSource(std::move(*info));
            if (m_options.selectIndex) {
                m_state->selectMany(m_options.selectMarked, *m_options.selectIndex);
            }
            m_shell->showPage(m_options.page.value_or(PageId::Images));
            if (!m_options.demoTool.empty()) {
                m_shell->toolDialogForDemo(m_options.demoTool, *m_options.openPath);
            }
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
        m_shell->showPage(m_options.page.value_or(PageId::Updates)); // --page=apply: Uygula waiting for the download (D-124)
        if (m_options.demoCatalog == L"download") {
            AppState::UpdateFetch fetch;
            fetch.stage = AppState::UpdateFetch::Stage::Downloading;
            fetch.kb = L"KB5129195";
            fetch.doneBytes = 1'342'177'280;
            fetch.totalBytes = 5'269'923'022;
            m_state->setUpdateFetch(std::move(fetch));
        } else if (m_options.demoCatalog == L"dialog") {
            // What the catalog offered for 25H2 x64 on 2026-09-30 (wlcli catalog 26200.8037).
            auto entry = [](const wchar_t* title, const wchar_t* kb, core::CatalogKind kind, bool preview, int m, int d,
                            std::uint64_t size, int revision) {
                core::CatalogEntry e;
                e.title = title;
                e.kb = kb;
                e.kind = kind;
                e.preview = preview;
                e.year = 2026;
                e.month = m;
                e.day = d;
                e.size = size;
                e.build = revision ? 26200 : 0;
                e.revision = revision;
                return e;
            };
            std::vector<core::CatalogOffer> offers{
                {entry(L"LCU", L"KB5129195", core::CatalogKind::Cumulative, false, 9, 14, 5173184334, 9457), true, false},
                {entry(L"LCU preview", L"KB5124010", core::CatalogKind::Cumulative, true, 9, 22, 5223465117, 9550), false, false},
                {entry(L".NET", L"KB5126052", core::CatalogKind::DotNet, false, 9, 8, 96738688, 0), true, false},
                // D-080: the dynamic updates of the same release.
                {entry(L"Safe OS", L"KB5125758", core::CatalogKind::SafeOs, false, 9, 22, 182857835, 0), true, false},
                {entry(L"Setup", L"KB5127216", core::CatalogKind::Setup, false, 9, 22, 18316225, 0), true, false},
            };
            m_shell->showUpdateOffers(core::catalogTarget(26200, 8037, L"x64"), std::move(offers));
        }
    }
    if (m_options.demoEditions && m_state->source() && m_state->source()->install.images.size() > 2) {
        const auto& images = m_state->source()->install.images;
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim",
                                         images[3 % images.size()].index, images[3 % images.size()].name});
        m_shell->tasks().applyRecommended();
        m_shell->applyForDemo().setExtraEdition(images[0].index, true);
        m_shell->applyForDemo().setExtraEdition(images[2].index, true);
        m_shell->showPage(PageId::Apply);
    }
    if (m_options.demoOverview) {
        demoOverview();
    }
    if (m_options.demoVhd) {
        demoVhd();
    }
    if (m_options.isoChoices) {
        // D-144 addendum: options other than the first ones — the ISO page and the Apply row show the same.
        AppState::IsoChoices choices = m_state->isoChoices();
        choices.label = L"WINLOVE_TR";
        choices.boot = core::BootMode::UefiOnly;
        choices.repack = 3; // ESD
        choices.noPrompt = true;
        choices.secureBoot2023 = true;
        m_state->setIsoChoices(std::move(choices));
    }
    if (m_options.isoOptions) {
        m_shell->showIsoOptions(); // D-144 addendum: over the page shown (the Apply summary)
    }
    if (m_options.demoUsbGiven && m_state->source()) {
        m_shell->showPage(PageId::Iso);
        if (auto* page = m_shell->isoPageForDemo()) {
            core::UsbDisk disk;
            disk.number = 2;
            disk.vendor = L"SanDisk";
            disk.model = L"Ultra USB 3.0";
            disk.serial = L"4C530001";
            disk.size = 30'752'636'928;
            disk.busType = 7;
            disk.letters = {L"E:\\"};
            page->setDisks({disk});
            page->showUsbTab();
            if (m_options.demoUsb == L"confirm") {
                m_shell->startIsoForDemo();
            }
        }
    }
    if (m_options.demoNoWinre && m_state->source()) {
        if (!m_options.demoUsbGiven) {
            m_shell->showPage(PageId::Iso); // (showing it again would build the USB tab anew)
        }
        if (auto* page = m_shell->isoPageForDemo()) {
            page->setEditionsWithoutWinre({1});
        }
    }
    if (m_options.demoMediaUpdate && m_state->source()) {
        m_state->setMediaUpdate({L"C:\\WinLove\\updates\\windows11.0-kb5129195-x64_ed361878.msu",
                                 L"C:\\WinLove\\updates\\windows11.0-kb5127216-x64_902338a7.cab", true});
        m_shell->showPage(PageId::Iso);
    }
    if (m_options.demoAio && m_state->source()) {
        m_shell->showPage(PageId::Iso);
        if (auto* page = m_shell->isoPageForDemo()) {
            page->setMediaCannotInstallDemo({2});
        }
    }
    if (m_options.fakeOperation && m_state->source()) {
        const std::wstring& name = *m_options.fakeOperation;
        const bool reading = name == L"read";
        const bool verifying = name == L"verify";
        using K = EngineOperation::Kind;
        const K kind = reading                     ? K::Reading
                       : verifying                 ? K::Verifying
                       : name == L"prepare" || name == L"prepare-delete" ? K::Preparing
                       : name == L"delete"         ? K::Deleting
                       : name == L"export"         ? K::Exporting
                       : name == L"rename"         ? K::Renaming
                       : name == L"duplicate"      ? K::Duplicating
                       : name == L"move"           ? K::Moving
                       : name == L"unmount"        ? K::Unmounting
                                                   : K::Mounting;
        EngineOperation op{kind,
                           verifying                  ? std::filesystem::path(m_state->source()->installImage).filename().wstring()
                           : m_state->selectedImage() ? m_state->selectedImage()->name
                                                      : L"",
                           verifying ? m_state->source()->path : std::filesystem::path(L"C:\\WinLove\\mount"),
                           verifying ? 0 : m_state->selectedIndex().value_or(1)};
        op.startedMs = ui::nowMs() - 60000.0 * m_options.fakeProgress;
        if (name == L"prepare-delete") {
            op.purpose = K::Deleting; // the ISO's copy step of a delete (2026-11-11: its row said "Bağlanıyor")
        }
        if ((kind == K::Deleting || kind == K::Exporting || kind == K::Preparing) && m_options.selectMarked.size() > 1) {
            op.indexes = m_options.selectMarked; // several editions: each row says it
            op.index = 0;
        }
        if (reading) {
            // The second progress (D-027): the image is mounted, its lists are being read.
            m_state->setMounted(MountedImage{op.path, L"C:\\WinLove\\work\\sources\\install.wim", op.index, op.edition});
            op.stage = m_options.fakeProgress < 0.6f ? 0 : m_options.fakeProgress < 0.9f ? 1 : 2;
        }
        m_state->beginOperation(op);
        m_state->updateOperation(m_options.fakeProgress);
    }
    if (m_options.demoUpgrade && m_state->source() && m_state->selectedImage()) {
        const core::ImageInfo image = *m_state->selectedImage();
        m_state->setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", image.index, image.name});
        m_shell->images().rememberEditions(core::ImageEditions{
            L"Core", {L"CoreSingleLanguage", L"Professional", L"Education", L"ProfessionalEducation", L"ProfessionalWorkstation",
                      L"Enterprise"}});
        if (*m_options.demoUpgrade == L"queued") {
            m_state->queue(core::ops::Operation{core::ops::OpKind::SetEdition, L"edition", L"Professional", core::ops::Risk::Medium});
        }
        m_host->layout(m_options.size);
        if (*m_options.demoUpgrade == L"dialog") {
            m_shell->askUpgradeEdition();
        }
    }
    if (m_options.verified && m_state->source()) {
        core::WimVerifyReport report;
        report.streams = 94409;
        report.bytes = 15091372328ull;
        report.damaged = *m_options.verified == L"damaged" ? 3 : 0;
        m_shell->onImageVerified(report, std::filesystem::path(m_state->source()->installImage).filename().wstring());
    }
    if (m_options.switchLanguage) {
        // What "Arayüz dili" does at run time: every widget again in the other language, the
        // state (mount, queue, answers) untouched.
        m_options.language = *m_options.switchLanguage;
        rebuildUi();
    }
    if (m_options.toastTimeout) {
        m_shell->onTimer(Shell::kToastTimer); // what the window's timer does after Toast::kDurationMs
    }
    m_host->layout(m_options.size);
    if (m_options.palette) {
        m_shell->openPalette(*m_options.palette);
    }
    if (m_options.demoAccentPicker) { // D-143: Ayarlar › Vurgu rengi with its picker open
        if (SettingsPage* settings = m_shell->settingsPageForDemo()) {
            settings->openAccentPicker();
            m_host->layout(m_options.size);
        }
    }
    for (const UINT vk : m_options.keys) {
        // The window's own path: the focused widget first, then the shell's shortcuts.
        if (const ui::KeyEvent key{vk, false, false, false}; !m_host->onKeyDown(key)) {
            m_shell->handleShortcut(key);
        }
        m_host->layout(m_options.size);
    }

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
    if (m_options.contextAt) {
        pointerAt(*m_options.contextAt);
        m_host->onContextMenu(*m_options.contextAt);
        m_host->layout(m_options.size); // the menu gets its bounds from the host size
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

    // --settle: a live page's workers (the network too) land before the click / the frame.
    auto settle = [&] {
        drainPosted(); // what the workers finished so far, on this thread
        m_host->layout(m_options.size);
        if (m_options.settleMs <= 0) {
            return;
        }
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        const auto deadline = start + std::chrono::milliseconds(m_options.settleMs);
        auto quiet = start;
        const auto still = std::chrono::milliseconds(m_options.settleQuietMs);
        while (clock::now() < deadline && clock::now() - quiet < still) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bool any = false;
            {
                std::scoped_lock lock(m_postedMutex);
                any = !m_posted.empty();
            }
            if (any) {
                drainPosted();
                m_host->layout(m_options.size);
                quiet = clock::now();
            }
        }
        report(std::format(L"settled: last result {} ms after the start\n",
                           std::chrono::duration_cast<std::chrono::milliseconds>(quiet - start).count()));
    };
    settle();
    for (const auto& p : m_options.clickAfterLoad) {
        // D-139: a row of a list a worker read (Programlar's index) exists only now.
        pointerAt(p);
        m_host->onPointer({ui::PointerAction::Down, p, m_host->windowZone(p)});
        m_host->onPointer({ui::PointerAction::Up, p, m_host->windowZone(p)});
        settle();
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
                if (deviceLost(resized.error().hresult)) {
                    // A lost device is made again by paint(). Said outright: the failed resize left
                    // the context without a target, and whether EndDraw then answers with a code
                    // paint() takes for a lost device was left to chance (audit APP-14).
                    m_simulateDeviceLoss = true;
                } else {
                    showError(resized.error());
                }
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
    callbacks.endSessionBlocked = [this] { return m_shell && m_shell->imageWorkRunning(); };
    callbacks.cursor = [this](ui::PointF p) { return m_host ? m_host->cursorAt(p) : ui::Cursor::Arrow; };
    callbacks.pointer = [this](const ui::PointerEvent& event) {
        if (m_host) {
            m_host->onPointer(event);
        }
    };
    callbacks.contextMenu = [this](ui::PointF p) {
        if (m_host) {
            m_host->onContextMenu(p);
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
        if (id == kOldExeTimer) {
            removeOldExe(); // D-145
            return;
        }
        if (id == kUpdateCheckTimer) {
            // D-140: a few seconds after the window came up, once; the check itself runs on its own thread.
            m_window.stopTimer(kUpdateCheckTimer);
            if (m_updates) {
                m_updates->checkIfDue();
            }
            return;
        }
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
        m_window.setZoom(zoomFor(m_state->settings())); // Windows' text size may have changed
        applySettings(); // theme "Sistem" follows Windows
        m_window.invalidate();
    };

    const ui::WindowAppearance appearance{appIcon(), appIconResource(), m_options.theme != ui::ThemeKind::Light,
                                          colorRef(m_options.theme, Color::AccentBase), // the border, as applyTheme()
                                          colorRef(m_options.theme, Color::BgBase)};
    const ui::SizeF minimum{ui::tokens::size::minWindowW, ui::tokens::size::minWindowH};

    // The host must exist before the window: creation already sends WM_SIZE / WM_NCHITTEST.
    buildUi(windowHostServices());
    if (auto created = m_window.create(L"WinLove", m_options.size, minimum, std::move(callbacks), appearance);
        !created) {
        showError(created.error());
        return 1;
    }
    // buildUi started reads (this PC's DISM files, the mounts on it) before there was a window: a
    // result posted in that moment is queued without a message to run it and waited for the next
    // post. An empty one, now that the window is there, runs whatever waits (audit APP-20).
    m_window.post([] {});
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
    m_window.setZoom(zoomFor(m_state->settings())); // D-092: density and Windows' text size
    m_simulateDeviceLoss = m_options.testDeviceLost;
    // The user's request (2026-11-09): WinLove opens filling the screen; --size (tests, captures) keeps its size.
    m_window.show(!m_options.sizeGiven && !m_options.renderTo);
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
    m_shell->windowsDownload().releaseLeftovers(); // a conversion the app closed or crashed in
    // D-140: is a newer WinLove out? Asked on a thread of its own a few seconds after start, at most
    // once a day and only when Ayarlar says so (UpdateController::checkIfDue); the start never waits.
    m_updates = std::make_unique<UpdateController>(
        *m_state, [this](std::function<void()> fn) { m_window.post(std::move(fn)); }, UpdateController::networkFetch());
    // D-145: "Yükle" works on this exe; "Yeniden başlat" closes this window once the new one runs.
    m_updates->setInstaller(UpdateController::systemInstaller([this] { m_window.close(); }));
    if (!m_options.updatedFrom.empty()) {
        AppState::AppUpdate update = m_state->appUpdate();
        update.updatedFrom = m_options.updatedFrom; // the shell says it once (a toast) and the About card
        m_state->setAppUpdate(std::move(update));
        log::info("update", L"started by an update from " + m_options.updatedFrom);
    }
    removeOldExe(); // an update's old file, once the process it belonged to is gone (tried a few times)
    m_window.setTimer(kUpdateCheckTimer, kUpdateCheckDelayMs);
    const int exitCode = ui::Window::runMessageLoop();
    m_updates.reset(); // its thread stops before the window and the state go
    if (m_dropTarget) {
        m_dropTarget->revoke();
        m_dropTarget = nullptr;
    }
    return exitCode;
}

void App::applyTheme() {
    // 2026-11-09 (the user, like the reference tool): Windows 11's 1px window border in the chosen accent.
    m_window.setFrameColors(m_options.theme != ui::ThemeKind::Light, colorRef(m_options.theme, Color::AccentBase));
    m_window.invalidate();
    if (m_changelog) {
        m_changelog->applyTheme(); // D-145
    }
}

void App::removeOldExe() {
    if (m_options.renderTo) {
        return;
    }
    wchar_t path[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    const std::filesystem::path exe(std::wstring(path, n));
    // The old process may still be closing (it started this one, then closed its window): again in a
    // moment, a few times; whatever is left goes at the next start.
    if (core::removeUpdateBackup(exe) || ++m_oldExeTries > 10) {
        m_window.stopTimer(kOldExeTimer);
    } else if (m_oldExeTries == 1) {
        m_window.setTimer(kOldExeTimer, 2000);
    }
}

void App::openChangelog() {
    if (m_options.renderTo || !m_strings || !m_graphics) {
        return; // a render never opens a window
    }
    if (m_changelog) {
        m_changelog->bringToFront();
        return;
    }
    auto openLink = [](const std::wstring& url) {
        if (url.starts_with(L"https://") || url.starts_with(L"http://")) { // a note's link: web pages only
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    };
    ChangelogWindow::Context context{*m_graphics, *m_state, *m_strings, m_options.language};
    context.theme = [this] { return m_options.theme; };
    context.zoom = zoomFor(m_state->settings());
    context.owner = m_window.hwnd();
    context.icon = appIcon();
    context.iconResource = appIconResource();
    context.openLink = openLink;
    context.install = [this] {
        if (m_updates) {
            m_updates->install();
        }
    };
    context.openRelease = [this, openLink] {
        const auto& u = m_state->appUpdate();
        openLink(u.result && u.result->latest ? core::releasePageUrl(u.result->latest->url) : std::wstring(core::kReleasesPage));
    };
    context.closed = [this] {
        m_window.post([this] { m_changelog.reset(); }); // not inside its window procedure
    };
    auto opened = ChangelogWindow::open(std::move(context));
    if (!opened) {
        log::warn("update", L"the changelog window did not open: " + describe(opened.error()));
        return;
    }
    m_changelog = std::move(*opened);
}

int App::renderChangelog(ui::OffscreenTarget& target) {
    // D-145: the window's content as it would open, from a saved GitHub answer (no network).
    std::filesystem::path file = m_options.demoChangelog ? std::filesystem::path(*m_options.demoChangelog) : std::filesystem::path();
    if (file.empty()) {
        wchar_t path[MAX_PATH * 4] = {};
        const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
        // build\<preset>\bin\WinLove.exe → the repository's fixture
        file = std::filesystem::path(std::wstring(path, n)).parent_path().parent_path().parent_path().parent_path() / L"tests" /
               L"integration" / L"fixtures" / L"changelog-sample.json";
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) {
            file = std::filesystem::current_path(ec) / L"tests" / L"integration" / L"fixtures" / L"changelog-sample.json"; // run from the repository
        }
    }
    std::ifstream in(file, std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto releases = core::parseReleases(raw);
    if (!releases) {
        showError(Error{ErrorCode::NotFound, L"--demo-changelog: no release list", file.wstring()});
        return 1;
    }
    const core::ReleaseCheck check = core::evaluateReleases(L"" WL_VERSION_LABEL, std::move(*releases));
    ui::HostServices services;
    services.text = m_graphics->text.get();
    ui::Host host(std::move(services));
    ChangelogView::Services view;
    view.install = [] {}; // drawn; a render clicks nothing
    view.openRelease = [] {};
    auto root = std::make_unique<ChangelogView>(*m_strings, m_options.language, std::move(view));
    ChangelogView* changelog = root.get();
    host.setRoot(std::move(root));
    constexpr std::int64_t kNow = 1794398400; // 2026-11-11 12:00 UTC: "2 gün önce" in every render
    changelog->setCheck(check, kNow);
    changelog->setInstallable(check.verdict == core::ReleaseVerdict::Available);
    host.layout(m_options.size);
    for (const UINT vk : m_options.keys) {
        if (const ui::KeyEvent key{vk, false, false, false}; !host.onKeyDown(key)) {
            changelog->onKeyDown(key);
        }
    }
    for (const auto& p : m_options.clickAt) {
        host.onPointer({ui::PointerAction::Move, p, host.windowZone(p)});
        host.onPointer({ui::PointerAction::Down, p, host.windowZone(p)});
        host.onPointer({ui::PointerAction::Up, p, host.windowZone(p)});
    }
    if (m_options.hoverAt) {
        host.onPointer({ui::PointerAction::Move, *m_options.hoverAt, host.windowZone(*m_options.hoverAt)});
    }
    host.layout(m_options.size);
    {
        ui::Canvas canvas(target.beginDraw(), m_options.theme, m_options.scale, *m_graphics->text, *m_graphics->icons);
        canvas.clear(Color::BgBase);
        host.paint(canvas);
        if (auto done = target.endDraw(); !done) {
            showError(done.error());
            return 1;
        }
    }
    if (auto saved = target.savePng(*m_options.renderTo); !saved) {
        showError(saved.error());
        return 1;
    }
    report(L"rendered " + m_options.renderTo->wstring() + L"\n");
    return 0;
}

ui::HostServices App::windowHostServices() {
    ui::HostServices services{
        [this] { m_window.invalidate(); },
        [this](UINT id, UINT ms) { m_window.setTimer(id, ms); },
        [this](UINT id) { m_window.stopTimer(id); },
    };
    // The double-click area at the window's DPI, in the DIPs the host works in (Window::doubleClickArea).
    services.doubleClickArea = [this] { return m_window.doubleClickArea(); };
    return services;
}

void App::applySettings() {
    if (m_options.renderTo) {
        return; // a still frame shows what its arguments say
    }
    const AppSettings& settings = m_state->settings();
    ui::setReducedMotionForced(settings.reduceMotion);
    m_window.setZoom(zoomFor(settings));
    if (m_changelog) {
        m_changelog->setZoom(zoomFor(settings)); // D-145
    }
    // D-143: the picker previews a colour without saving it (ui::setAccentColor only); the window's frame
    // takes the accent when it is saved.
    if (settings.accent != m_accentShown) {
        m_accentShown = settings.accent;
        ui::setAccentColor(settings.accent);
        applyTheme(); // every accent brush is resolved at paint time: a repaint is enough
    }
    if (const ui::ThemeKind theme = resolveTheme(settings.theme); theme != m_options.theme) {
        m_options.theme = theme;
        applyTheme();
    }
    const bool fontChanged = settings.font != m_options.font.value_or(UiFont::SegoeVariable);
    if (settings.language != m_options.language || fontChanged) {
        m_options.language = settings.language;
        m_options.font = settings.font;
        // Later: this runs inside a control of the shell that is about to be destroyed.
        m_window.post([this, fontChanged] { rebuildUi(fontChanged); });
    }
}

void App::rebuildUi(bool newFamilies) {
    auto strings = embeddedStrings(m_options.language);
    if (!strings) {
        return;
    }
    m_changelog.reset(); // D-145: it reads the old strings too (it opens again in the new language)
    releaseShell(); // the old shell reads the old strings: it goes first
    m_strings = std::move(*strings);
    if (newFamilies) { // nothing measures with the old text styles any more
        if (auto set = m_graphics->setFamilies(fontFamilies(m_options.font.value_or(UiFont::SegoeVariable))); !set) {
            log::warn("app", L"type family not applied: " + describe(set.error()));
        }
        m_graphics->text->setLocale(localeName(m_options.language));
    }
    if (m_options.renderTo) {
        buildUi({}); // --switch-lang: the frame is laid out and drawn by renderOffscreen
        return;
    }
    buildUi(windowHostServices());
    m_host->layout(m_window.clientSize());
    m_window.invalidate();
}

void App::releaseShell() {
    if (m_shell) {
        m_options.page = m_shell->currentPage();
        m_options.navCollapsed = m_shell->navCollapsed();
    }
    m_host.reset();
    m_shell = nullptr;
}

void App::paint() {
    if (!m_target || !m_host) {
        return;
    }
    // The canvas goes before a lost device is let go: its brush holds the old device, which then
    // keeps the old swap chain on the window, and the new one is refused (E_ACCESSDENIED).
    auto presented = [this] {
        ui::Canvas canvas(m_target->beginDraw(), m_options.theme, m_window.scale(), *m_graphics->text,
                          *m_graphics->icons);
        canvas.clear(Color::BgBase);
        m_host->paint(canvas);
        return m_target->endDrawAndPresent();
    }();
    std::int32_t hr = presented ? 0 : presented.error().hresult;
    if (std::exchange(m_simulateDeviceLoss, false)) {
        hr = static_cast<std::int32_t>(0x887A0005); // DXGI_ERROR_DEVICE_REMOVED
    }
    if (deviceLost(hr)) {
        const auto* shell = m_shell;
        if (auto rebuilt = recreateGraphics(); !rebuilt) {
            log::error("app", L"device-loss recovery failed: " + describe(rebuilt.error()));
            showError(rebuilt.error());
            m_forceClose = true;
            m_window.close();
            return;
        }
        log::info("app", m_shell == shell ? L"device-loss recovery: new device, same shell"
                                          : L"device-loss recovery: the shell was rebuilt");
        m_window.invalidate();
    } else if (!presented) {
        // Any other failure leaves the frame undrawn; it used to pass without a word (audit APP-14).
        // Once: it would come again with every frame.
        static bool logged = false;
        if (!std::exchange(logged, true)) {
            log::error("app", L"drawing failed: " + describe(presented.error()));
        }
    }
}

Result<void> App::recreateGraphics() {
    // Only what belongs to the lost device is made again: the swap chain, its context and the file
    // icon bitmaps. The shell stays — rebuilding it threw away the controllers of running jobs, and
    // a finished Uygula was never told (audit A6). Fonts, text formats and icon geometries hang on
    // the factories, which survive a device loss.
    m_target.reset();
    ui::Canvas::forgetFileIcons();
    if (auto recreated = m_graphics->device->recreateDevice(); !recreated) {
        return recreated;
    }
    auto target = ui::SwapChainTarget::create(*m_graphics->device, m_window.hwnd(), m_window.clientWidthPx(),
                                              m_window.clientHeightPx(), m_window.scale() * 96.0f);
    if (!target) {
        return std::unexpected(target.error());
    }
    m_target = std::move(*target);
    return {};
}


// ---- D-140: --demo-update ---------------------------------------------------------------------

void App::demoUpdate() {
    // The update check's answers as the controller leaves them in the state — render only: nothing
    // here touches the network or settings.json (a render's settings are a scratch file).
    using Phase = AppState::AppUpdate::Phase;
    const std::wstring variant = *m_options.demoUpdate;
    const std::int64_t now = UpdateController::systemNow();
    AppState::AppUpdate update;
    update.at = now - 4 * 60;
    if (variant == L"checking") {
        update.phase = Phase::Checking;
        update.manual = true;
    } else if (variant == L"failed") {
        update.phase = Phase::Failed;
        update.manual = true;
        update.error = Error{ErrorCode::IoError, L"the server did not answer", core::kReleasesApi,
                             static_cast<std::int32_t>(0x80072EE7)}; // ERROR_WINHTTP_NAME_NOT_RESOLVED: offline
    } else {
        core::Release release;
        if (variant == L"current") {
            release.tag = L"v1.3.0-beta"; // GitHub's answer of 2026-11-11 (older than this build: "up to date")
            release.name = L"WinLove 1.3.0 Beta";
            release.published = 1794212128; // 2026-11-09T08:15:28Z
        } else {
            release.tag = L"v1.5.0-beta"; // newer than this build (1.4.0 Beta)
            release.name = L"WinLove 1.5.0 Beta";
            release.published = now - 2 * 86400;
            release.notes = L"WinLove 1.5.0 Beta — VHD / VHDX kaynağı, güncelleme denetimi ve daha hızlı Uygula / VHD and VHDX "
                            L"sources, an update check and a faster Apply.\n"
                            L"• VHD / VHDX: bir sanal diskteki Windows doğrudan açılır.\n"
                            L"• Güncelleme denetimi: yeni sürüm çıkınca Hakkında sayfası haber verir; hiçbir şey indirilmez.";
        }
        release.url = core::releasePageUrl(L"https://github.com/shadesofdeath/WinLove/releases/tag/" + release.tag);
        release.prerelease = true;
        release.version = core::parseAppVersion(release.tag);
        update.phase = Phase::Done;
        update.result = core::evaluateReleases(L"" WL_VERSION_LABEL, {release});
        update.result->releasesListed = 10;
        // A startup check that found a new release: the notice (a toast, the status bar) on any page but About.
        update.notice = update.result->verdict == core::ReleaseVerdict::Available;
    }
    // D-145: "Yükle" in each of its states (as UpdateController leaves them; nothing is downloaded).
    using Install = AppState::AppUpdate::Install;
    if (variant == L"downloading" || variant == L"verifying" || variant == L"ready" || variant == L"install-failed") {
        Install install;
        install.tag = L"v1.5.0-beta";
        install.name = L"WinLove 1.5.0 Beta";
        install.total = 11'534'336;
        if (variant == L"downloading") {
            install.stage = Install::Stage::Downloading;
            install.done = 4'771'840;
        } else if (variant == L"verifying") {
            install.stage = Install::Stage::Verifying;
            install.done = install.total;
        } else if (variant == L"ready") {
            install.stage = Install::Stage::Ready;
            install.done = install.total;
        } else {
            install.stage = Install::Stage::Failed;
            install.why = Install::Why::Verify;
            install.detail = L"sha256: 9b1e04\u2026, the release lists 32a1e5\u2026";
        }
        update.install = std::move(install);
        update.notice = false; // the startup notice was followed: "Yükle" was clicked
    } else if (variant == L"updated") {
        update = {};
        update.updatedFrom = L"1.2.3 Beta"; // this instance was started by a self-update from 1.2.3 Beta
    }
    m_state->setAppUpdate(std::move(update));
}

// ---- D-138: --demo-overview -------------------------------------------------------------------

void App::demoAdoption(const std::wstring& variant) {
    // 2026-11-11: taking over an image another tool mounted (Kaynak's list). Render only: nothing is read.
    if (variant.empty()) {
        return;
    }
    const std::filesystem::path folder = L"C:\\Tools\\Mount\\Win11_25H2";
    const std::filesystem::path wim = L"D:\\ISO\\Win11_25H2\\sources\\install.wim";
    if (variant == L"adopting" || variant == L"remounting") {
        const bool remount = variant == L"remounting";
        EngineOperation op{remount ? EngineOperation::Kind::Remounting : EngineOperation::Kind::Adopting,
                           remount ? std::wstring(L"mount") : folder.filename().wstring(),
                           remount ? std::filesystem::path(L"D:\\Work\\mount") : folder, 0};
        op.startedMs = ui::nowMs() - 4'000.0;
        m_state->beginOperation(op);
        return;
    }
    if (variant == L"failed") {
        m_shell->adoptFailedForDemo(core::MountState::Invalid);
        return;
    }
    // Taken over: Windows 11 Pro from the other tool's setup folder, mounted in its folder.
    core::SourceInfo source;
    source.path = L"D:\\ISO\\Win11_25H2";
    source.format = core::ImageFormat::Folder;
    source.installImage = L"sources/install.wim";
    source.installImageSize = 7'217'280'579ULL;
    source.install.header.compression = core::WimCompression::Lzx;
    const wchar_t* names[] = {L"Windows 11 Home", L"Windows 11 Education", L"Windows 11 Pro", L"Windows 11 Pro Education",
                              L"Windows 11 Pro for Workstations", L"Windows 11 Home Single Language"};
    for (int i = 0; i < 6; ++i) {
        core::ImageInfo image;
        image.index = i + 1;
        image.name = names[i];
        image.architecture = core::Architecture::X64;
        image.build = 26200;
        image.spBuild = 8037;
        image.defaultLanguage = L"tr-TR";
        image.languages = {L"tr-TR"};
        image.totalBytes = 24'622'989'344ULL;
        source.install.images.push_back(image);
    }
    const std::filesystem::path sourcePath = source.path;
    m_shell->adoptOpened(std::move(source), MountedImage{folder, wim, 3, L"Windows 11 Pro"}, /*preload=*/false);
    m_state->forgetRecent(sourcePath); // a made-up folder: not left in the renders' recent list
    if (variant == L"overview") {
        m_shell->showPage(PageId::Overview);
    } else if (variant == L"unmount") {
        m_shell->showPage(PageId::Images);
        m_shell->askUnmount();
    }
}

void App::demoRunClose(const std::wstring& variant) {
    // 2026-11-11: a run stopped half way (the image still mounted, unsaved), then — from anywhere —
    // saved or discarded: what the Apply page says while that runs and after. Render only.
    AppState::ApplyRun run;
    run.changes = m_state->changes();
    run.plan = core::ops::plan(run.changes);
    run.groups = core::ops::groups(run.plan);
    run.stepState.assign(run.plan.steps.size(), 0);
    run.edition = L"Windows 11 Pro";
    run.sizeBefore = 24'622'989'344ULL;
    run.stage = AppState::ApplyRun::Stage::Done;
    run.fraction = 0.5;
    run.startedMs = ui::nowMs() - 95'000;
    run.mountSerial = m_state->mountSerial();
    core::ops::ApplyJobResult result;
    result.report.completed = false; // stopped before the second step
    if (!run.plan.steps.empty()) {
        result.report.results.push_back({run.plan.steps.front(), {}});
        result.stepTimes.push_back(std::chrono::milliseconds(14'000));
        run.stepState.front() = 2;
    }
    result.elapsed = std::chrono::milliseconds(47'000);
    run.result = std::move(result);
    run.queueVersionAtEnd = m_state->changes().version();
    m_state->setApplyRun(std::move(run));
    if (variant == L"stopped") {
        return;
    }
    const bool commit = variant == L"saving" || variant == L"saved";
    m_state->beginRunClose(commit);
    if (variant == L"discarding" || variant == L"saving") {
        const auto& mounted = *m_state->mounted();
        EngineOperation op{EngineOperation::Kind::Unmounting, mounted.edition, mounted.mountDir, mounted.index};
        op.startedMs = ui::nowMs() - 20'000.0;
        m_state->beginOperation(op);
        m_state->updateOperation(m_options.fakeProgress);
        return;
    }
    if (variant == L"closefailed") {
        m_state->endRunClose(Error{ErrorCode::DismFailure, L"Bir dosya kullanımda olduğundan imaj çözülemedi (0xC1420117).", L"unmount",
                                   static_cast<std::int32_t>(0xC1420117)});
        return;
    }
    if (commit) {
        m_state->applyRunMutable()->sizeAfter = 24'401'112'000ULL;
    }
    m_state->endRunClose(std::nullopt);
    m_state->setMounted(std::nullopt);
}

void App::demoOverview() {
    demoOverview(*m_options.demoOverview, L"C:\\WinLove\\work\\sources\\install.wim", L"C:\\WinLove\\mount");
}

void App::demoOverview(const std::wstring& variant, const std::filesystem::path& imagePath, const std::filesystem::path& mountDir) {
    // An image as it stands in the middle of the work — its lists read, a queue across the navigation —
    // for Görüntü › Özet and the Apply screens. Render only: nothing here reads or writes a disk.
    int index = 4;
    std::wstring edition = L"Windows 11 Pro";
    std::uint64_t before = 24'622'989'344ULL;
    if (const auto& source = m_state->source(); source && !source->install.images.empty()) {
        const auto& image = source->install.images.front();
        index = image.index;
        edition = image.name;
        before = image.totalBytes ? image.totalBytes : before;
        m_state->select(index);
    }
    m_state->setMounted(MountedImage{mountDir, imagePath, index, edition});
    if (variant == L"unread") {
        // Just mounted: the page lists are being read, features first; nothing else is read yet.
        EngineOperation op{EngineOperation::Kind::Reading, edition, mountDir, index};
        op.startedMs = ui::nowMs() - 12'000.0;
        m_state->beginOperation(op);
        m_state->updateOperation(0.18, 0);
        m_state->setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Loading, mountDir, {}, {}});
        m_shell->showPage(m_options.page.value_or(PageId::Overview));
        return;
    }

    // The lists, as a 25H2 Pro has them (sizes as measured, D-113).
    constexpr std::uint64_t mb = 1024 * 1024;
    auto app = [](const wchar_t* identity, const wchar_t* version, std::uint64_t size) {
        core::AppxComponent c;
        c.package.displayName = identity;
        c.package.packageName = std::wstring(identity) + L"_" + version + L"_neutral_~_8wekyb3d8bbwe";
        c.package.version = version;
        c.size = size * 1024 * 1024;
        return c;
    };
    std::vector<core::AppxComponent> apps{
        app(L"Microsoft.GamingApp", L"2410.1001.4.0", 412), app(L"Microsoft.XboxGamingOverlay", L"7.224.11061.0", 96),
        app(L"Microsoft.XboxIdentityProvider", L"12.115.1001.0", 8), app(L"Microsoft.ZuneMusic", L"11.2408.12.0", 142),
        app(L"Microsoft.Windows.Photos", L"2024.11100.16010.0", 94), app(L"Clipchamp.Clipchamp", L"3.1.10020.0", 71),
        app(L"Microsoft.MicrosoftOfficeHub", L"18.2408.1122.0", 41), app(L"Microsoft.Todos", L"2.114.7122.0", 38),
        app(L"Microsoft.MicrosoftStickyNotes", L"6.1.2.0", 12), app(L"Microsoft.BingWeather", L"4.54.63007.0", 21),
        app(L"Microsoft.BingNews", L"4.55.62231.0", 18), app(L"MicrosoftWindows.Client.WebExperience", L"424.1301.270.9", 64),
        app(L"Microsoft.WindowsStore", L"22410.1401.1.0", 58), app(L"Microsoft.SecHealthUI", L"1000.26100.1.0", 9),
        app(L"Microsoft.VCLibs.140.00", L"14.0.33519.0", 6), app(L"Microsoft.WindowsCalculator", L"11.2409.0.0", 14),
        app(L"Microsoft.WindowsFeedbackHub", L"1.2409.1.0", 27),
    };
    for (auto& item : apps) {
        const std::wstring& id = item.package.displayName;
        if (id == L"Microsoft.WindowsStore" || id == L"Microsoft.WindowsCalculator" || id == L"Microsoft.Windows.Photos" ||
            id == L"Microsoft.ZuneMusic" || id == L"Microsoft.BingWeather") {
            item.needs = {L"Microsoft.VCLibs.140.00"};
        }
    }
    m_state->setSystemComponents(AppState::SystemComponents{
        mountDir,
        {{"edge", {true, 803 * mb}}, {"edge-webview", {true, 796 * mb}}, {"edge-update", {true, 806 * mb}},
         {"onedrive", {true, 197 * mb}}, {"winre", {true, 643 * mb}}, {"telemetry", {true, 11 * mb}},
         {"defender-definitions", {true, 483 * mb}}, {"photo-viewer", {true, 19 * mb}}, {"media-streaming", {true, 12 * mb}},
         {"casting", {true, 4 * mb}}, {"fonts-jpan", {true, 81 * mb}}, {"fonts-hans", {true, 56 * mb}}, {"fonts-hant", {true, 26 * mb}},
         {"fonts-kore", {true, 17 * mb}}, {"appv", {true, 24 * mb}}, {"uev", {true, 15 * mb}}, {"kiosk", {true, 8 * mb}},
         {"work-folders", {true, 4 * mb}}, {"remote-assistance", {true, 3 * mb}}, {"take-a-test", {true, 2 * mb}},
         {"help", {true, 6 * mb}}, {"deep-modem", {true, 28 * mb}}, {"deep-tape", {true, 2 * mb}},
         {"component-store-shrink", {true, 3041 * mb}}},
        {}});
    m_state->setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, mountDir, std::move(apps), {}});

    using core::OptionalFeature;
    using S = core::ServicingState;
    auto feature = [](std::wstring name, std::wstring display, S state) {
        return OptionalFeature{OptionalFeature::Kind::Feature, std::move(name), std::move(display), {}, state, 0, false};
    };
    auto capability = [](std::wstring name, std::wstring display, std::uint64_t size) {
        return OptionalFeature{OptionalFeature::Kind::Capability, std::move(name), std::move(display), {}, S::Installed,
                               size * 1024 * 1024, false};
    };
    std::vector<OptionalFeature> features{
        feature(L"NetFx3", L".NET Framework 3.5", S::Staged),
        feature(L"NetFx4-AdvSrvs", L".NET Framework 4.8 Gelişmiş Hizmetler", S::Installed),
        feature(L"Microsoft-Hyper-V-All", L"Hyper-V", S::Staged),
        feature(L"Containers-DisposableClientVM", L"Windows Sandbox", S::Staged),
        feature(L"Microsoft-Windows-Subsystem-Linux", L"WSL", S::Staged),
        feature(L"Printing-PrintToPDFServices-Features", L"Microsoft PDF'ye Yazdır", S::Installed),
        feature(L"SearchEngine-Client-Package", L"Windows Arama", S::Installed),
        feature(L"MSRDC-Infrastructure", L"Uzak Masaüstü Sıkıştırma", S::Installed),
        feature(L"SmbDirect", L"SMB Direct", S::Installed),
        feature(L"WorkFolders-Client", L"İş Klasörleri İstemcisi", S::Installed),
        feature(L"MediaPlayback", L"Medya Özellikleri", S::Installed),
        feature(L"Windows-Defender-Default-Definitions", L"Windows Defender varsayılan tanımları", S::Installed),
        capability(L"Browser.InternetExplorer~~~~0.0.11.0", L"Internet Explorer modu", 42),
        capability(L"OpenSSH.Client~~~~0.0.1.0", L"OpenSSH İstemcisi", 6),
        capability(L"Media.WindowsMediaPlayer~~~~0.0.12.0", L"Windows Media Player (eski)", 31),
        capability(L"Microsoft.Windows.WordPad~~~~0.0.1.0", L"Wordpad", 9),
        capability(L"Language.Basic~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) yazma", 12),
        capability(L"Language.Handwriting~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) el yazısı", 9),
        capability(L"Language.OCR~~~tr-TR~0.0.1.0", L"Türkçe (Türkiye) OCR", 4),
    };
    m_state->setOptionalFeatures(AppState::OptionalFeatures{AppState::OptionalFeatures::Status::Ready, mountDir, std::move(features), {}});

    using core::StartType;
    auto svc = [](const wchar_t* name, const wchar_t* display, StartType start) {
        core::ServiceEntry s;
        s.name = name;
        s.displayName = display;
        s.start = start;
        s.type = 0x10;
        return s;
    };
    m_state->setServiceList(AppState::ServiceList{
        AppState::ServiceList::Status::Ready,
        mountDir,
        {svc(L"DiagTrack", L"Bağlı Kullanıcı Deneyimleri ve Telemetri", StartType::Auto),
         svc(L"dmwappushservice", L"Cihaz Yönetimi WAP Gönderme", StartType::Manual),
         svc(L"WSearch", L"Windows Arama", StartType::AutoDelayed), svc(L"Spooler", L"Yazdırma Biriktiricisi", StartType::Auto),
         svc(L"wuauserv", L"Windows Update", StartType::Manual), svc(L"RemoteRegistry", L"Uzak Kayıt Defteri", StartType::Disabled),
         svc(L"XblAuthManager", L"Xbox Live Kimlik Doğrulama", StartType::Manual),
         svc(L"BITS", L"Arka Plan Akıllı Aktarma Hizmeti", StartType::AutoDelayed),
         svc(L"MapsBroker", L"İndirilen Haritalar Yöneticisi", StartType::AutoDelayed), svc(L"Fax", L"Faks", StartType::Disabled),
         svc(L"RetailDemo", L"Perakende Tanıtım Hizmeti", StartType::Manual), svc(L"lfsvc", L"Coğrafi Konum Hizmeti", StartType::Manual)},
        {}});

    auto driver = [](const wchar_t* published, const wchar_t* original, const wchar_t* cls, const wchar_t* provider) {
        core::DriverEntry d;
        d.publishedName = published;
        d.originalFileName = original;
        d.className = cls;
        d.provider = provider;
        d.signed_ = true;
        return d;
    };
    m_state->setImageDrivers(AppState::ImageDrivers{
        AppState::ImageDrivers::Status::Ready,
        mountDir,
        {driver(L"oem0.inf", L"rt640x64.inf", L"Net", L"Realtek"), driver(L"oem1.inf", L"iastorvd.inf", L"SCSIAdapter", L"Intel Corporation"),
         driver(L"oem2.inf", L"ibtusb.inf", L"Bluetooth", L"Intel Corporation")},
        {}});
    m_state->setImageIntl(
        AppState::ImageIntl{AppState::ImageIntl::Status::Ready, mountDir, core::ImageIntl{{}, {L"tr-TR", L"en-US"}}, {}, {}});

    // The queue, across the navigation: components and apps, features, two updates, a storage driver,
    // two services, the recommended scheduled tasks.
    {
        const auto batch = m_state->batchQueue();
        auto& components = m_shell->components();
        const std::vector<std::wstring> removeApps{L"Microsoft.GamingApp", L"Microsoft.XboxGamingOverlay", L"Microsoft.ZuneMusic",
                                                   L"Clipchamp.Clipchamp", L"Microsoft.Todos", L"Microsoft.BingNews",
                                                   L"Microsoft.WindowsFeedbackHub"};
        const std::vector<std::wstring> removeSystem{L"onedrive", L"telemetry", L"help", L"take-a-test", L"remote-assistance", L"kiosk"};
        for (const auto& g : components.groups()) {
            for (const auto& item : g.items) {
                const bool pick = std::ranges::find(removeApps, item.identity) != removeApps.end() ||
                                  (item.kind == ComponentController::Item::Kind::System &&
                                   std::ranges::find(removeSystem, item.packageName) != removeSystem.end());
                if (pick && !components.queued(item)) {
                    components.toggle(item);
                }
            }
        }
        const auto featureList = m_state->optionalFeatures()->items; // a copy: queueing tells the listeners
        for (const auto& f : featureList) {
            if (f.name == L"Microsoft-Windows-Subsystem-Linux" || f.name == L"NetFx3" || f.name.starts_with(L"OpenSSH.Client") ||
                f.name.starts_with(L"Browser.InternetExplorer")) {
                m_state->queue(FeatureController::operationFor(f));
            }
        }
        UpdatesPage::queuePackages(*m_state, {LR"(C:\Updates\windows11.0-kb5044284-x64_8d2c.msu)",
                                              LR"(C:\Updates\windows11.0-kb5044030-x64-ndp481_a1b2.msu)"});
        m_state->queue(core::ops::Operation{core::ops::OpKind::AddDriver, LR"(C:\Drivers\Intel\VMD\iaStorVD.inf)"});
        auto& services = m_shell->services();
        const auto serviceList = m_state->serviceList()->items;
        services.set(serviceList[0], StartType::Disabled);
        services.set(serviceList[1], StartType::Disabled);
        m_shell->tasks().applyRecommended();
    }

    if (variant != L"ready") {
        AppState::ApplyRun run;
        run.changes = m_state->changes();
        run.plan = core::ops::plan(run.changes);
        run.groups = core::ops::groups(run.plan);
        run.edition = edition;
        run.sizeBefore = before;
        const std::size_t steps = run.plan.steps.size();
        run.stepState.assign(steps, 2);
        if (variant == L"running") {
            // Into the removals; one step was skipped on the way.
            std::size_t current = std::min(steps - 1, steps * 55 / 100);
            for (const auto& g : run.groups) {
                if (g.phase == core::ops::Phase::Remove && g.count > 0) {
                    current = g.first + std::min<std::size_t>(g.count - 1, 4);
                }
            }
            std::fill(run.stepState.begin() + static_cast<std::ptrdiff_t>(current), run.stepState.end(), 0);
            run.stepState[current] = 1;
            if (current > 2) {
                run.stepState[1] = 3;
            }
            run.currentStep = static_cast<int>(current);
            run.fraction = 0.46;
            run.startedMs = ui::nowMs() - 214'000.0;
            const std::wstring of = L"/" + std::to_wstring(steps) + L"] ";
            log::info("apply", L"[1" + of + L"enableFeature Microsoft-Windows-Subsystem-Linux \u2026 ok");
            log::warn("apply", L"[2" + of + L"removeCapability OpenSSH.Client~~~~0.0.1.0: 0x800F0825 (permanent) \u2014 skipped");
            log::info("dism", L"Remove-ProvisionedAppxPackage Microsoft.GamingApp_2410.1001.4.0_neutral_~_8wekyb3d8bbwe \u2026 ok");
            log::info("dism", L"Remove-ProvisionedAppxPackage Microsoft.XboxGamingOverlay_7.224.11061.0_neutral_~_8wekyb3d8bbwe \u2026 ok");
            log::info("apply", L"[" + std::to_wstring(current + 1) + of + L"removeComponent onedrive");
            if (m_options.autoIso) {
                run.autoIso = AppState::ApplyRun::AutoIso{m_shell->autoIsoForDemo().output()}; // D-144: waits for the save
            }
            m_state->setApplyRun(std::move(run));
        } else if (variant == L"paused") {
            run.stage = AppState::ApplyRun::Stage::Paused;
            run.fraction = 0.86;
            run.startedMs = ui::nowMs() - 412'000.0;
            log::info("apply", L"paused before saving: C:\\WinLove\\mount is open for changes by hand");
            m_state->setApplyRun(std::move(run));
        } else {
            const bool held = variant == L"held" || variant == L"iso-skipped";
            run.stage = AppState::ApplyRun::Stage::Done;
            run.fraction = 1.0;
            run.startedMs = ui::nowMs() - 600'000.0;
            core::ops::ApplyJobResult result;
            result.report.completed = true;
            for (const auto& step : run.plan.steps) {
                result.report.results.push_back({step, {}});
                result.stepTimes.push_back(std::chrono::milliseconds(21'000));
            }
            std::optional<core::ops::Operation> failedOp;
            if (held && !result.report.results.empty()) {
                // A step failed half way: nothing saved, still mounted; the queue keeps only that step (A7).
                result.report.results.front().outcome = fail(ErrorCode::DismFailure, L"The process cannot access the file",
                                                             L"enable feature", static_cast<std::int32_t>(0x80070020));
                run.stepState.front() = 3;
                failedOp = run.plan.steps.front().operation;
            }
            result.committed = !held;
            result.held = held;
            result.optimized = !held;
            result.commitTime = std::chrono::milliseconds(held ? 0 : 96'000);
            result.elapsed = std::chrono::milliseconds(held ? 402'000 : 582'000);
            if (!held) {
                const auto delta = run.changes.estimatedSizeDelta();
                run.sizeAfter = static_cast<std::uint64_t>(std::max<std::int64_t>(static_cast<std::int64_t>(before) + delta, 1));
            }
            run.result = std::move(result);
            if (failedOp) {
                m_state->unqueueIf([&](const core::ops::Operation& op) {
                    return !(op.kind == failedOp->kind && op.target == failedOp->target);
                });
            }
            run.queueVersionAtEnd = m_state->changes().version();
            if (variant.starts_with(L"iso-")) {
                demoAutoIso(run, variant); // D-144
            }
            m_state->setApplyRun(std::move(run));
            if (!held) {
                m_state->setMounted(std::nullopt); // saved and unmounted
            }
            if (variant == L"iso-running") {
                AppState::IsoRun iso;
                iso.running = true;
                iso.afterApply = true;
                iso.fraction = 0.42;
                iso.stage = 2;
                iso.startedMs = ui::nowMs() - 48'000.0;
                iso.output = m_state->applyRun()->autoIso->output;
                m_state->setIsoRun(std::move(iso));
            }
        }
    }
    m_shell->showPage(m_options.page.value_or(PageId::Overview));
}

void App::demoAutoIso(AppState::ApplyRun& run, const std::wstring& variant) {
    // D-144: the ISO after the run, as AutoIsoController leaves it in each state. Render only.
    using State = AppState::ApplyRun::AutoIso::State;
    AppState::ApplyRun::AutoIso iso;
    iso.output = m_shell->autoIsoForDemo().output();
    if (variant == L"iso-running") {
        iso.state = State::Running;
    } else if (variant == L"iso-done") {
        iso.state = State::Done;
        iso.bytes = 6'402'813'952ULL;
    } else if (variant == L"iso-failed") {
        iso.state = State::Failed;
        iso.error = Error{ErrorCode::IoError, L"Diskte yeterli yer yok (0x80070070)", iso.output.wstring(),
                          static_cast<std::int32_t>(0x80070070)};
    } else if (variant == L"iso-cancelled") {
        iso.state = State::Cancelled;
    } else {
        iso.state = State::NotStarted; // iso-skipped: the run held the image (a step failed), nothing saved
        iso.skip = AppState::ApplyRun::AutoIso::Skip::NotSaved;
    }
    run.autoIso = std::move(iso);
}

// ---- D-141: --demo-vhd --------------------------------------------------------------------------

void App::demoVhd() {
    // A VHDX holding an applied 26200 Pro, as the lab's disk reads (build\lab\vhd, 2026-11-11). Made
    // up here: nothing is attached, opened or read.
    const std::wstring variant = *m_options.demoVhd;
    core::SourceInfo source;
    source.path = L"D:\\Diskler\\Win11-Pro.vhdx";
    source.format = core::ImageFormat::Vhdx;
    source.installImage = L"Win11-Pro.vhdx";
    source.installImageSize = 14'935'916'544ULL;
    source.install.header.imageCount = 1;
    core::OfflineWindowsFacts facts;
    facts.productName = L"Windows 10 Enterprise";
    facts.editionId = L"Professional";
    facts.installationType = L"Client";
    facts.displayVersion = L"25H2";
    facts.build = 26200;
    facts.ubr = 8037;
    facts.processorArchitecture = L"AMD64";
    facts.languages = {L"tr-TR"};
    facts.defaultLanguage = L"tr-TR";
    facts.systemRoot = L"WINDOWS";
    source.install.images.push_back(core::imageInfoFromFacts(facts, 14'732'476'416ULL));
    core::VirtualDiskInfo disk;
    disk.virtualSize = 40ULL << 30;
    disk.fileSize = source.installImageSize;
    disk.dynamic = true;
    disk.volumeSize = 42'930'843'648ULL;
    disk.volumeFree = 28'198'367'232ULL;
    disk.volumeLabel = L"Windows";
    disk.volumes = 1;
    source.disk = disk;
    const std::filesystem::path vhd = source.path;
    m_state->setSource(std::move(source));
    m_state->forgetRecent(vhd); // a made-up file: not into the render's recent list (the Source page of later frames)
    m_state->select(1);
    const std::filesystem::path folder = m_state->settings().vhdMountDirectory();
    if (variant == L"mounted" || variant == L"unmount" || variant == L"done") {
        demoOverview(variant == L"done" ? std::wstring(L"done") : std::wstring(L"ready"), vhd, folder);
        if (variant == L"unmount") {
            m_shell->askUnmount();
        }
        return;
    }
    m_shell->showPage(m_options.page.value_or(variant == L"iso" ? PageId::Iso : PageId::Images));
    if (variant == L"confirm") {
        m_shell->askMountVirtualDisk(vhd, core::changesDiskPath(vhd, m_state->settings().vhdWorkDirectory()), [] {});
    }
}

} // namespace wl::app
