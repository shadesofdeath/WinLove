#pragma once
// Access to data embedded in WinLove.exe (see WinLove.rc / resource.h).
#include "app/Localization.h"
#include "core/unattend/Welcome.h"
#include "ui/text/FontLibrary.h"

#include <windows.h>

#include <string_view>
#include <vector>

namespace wl::app {

// The bundled font files: Inter and Geist with their monospace companions (D-092), and the wordmark's
// face (D-147, one weight).
[[nodiscard]] std::vector<ui::FontBytes> embeddedFonts();

[[nodiscard]] Result<Localization> embeddedStrings(Language language);
// resources/catalog/appx.json as embedded bytes (empty when missing).
[[nodiscard]] std::string_view embeddedAppxCatalog();
// resources/catalog/services.json (P10 risk / notes).
[[nodiscard]] std::string_view embeddedServiceCatalog();
// resources/catalog/settings.json (P12 form).
[[nodiscard]] std::string_view embeddedSettingsCatalog();
// resources/catalog/components.json (P07 system components and cleanup).
[[nodiscard]] std::string_view embeddedComponentsCatalog();
// resources/catalog/tasks.json (scheduled tasks, D-048) and hosts.json (block lists, D-049).
[[nodiscard]] std::string_view embeddedTaskCatalog();
[[nodiscard]] std::string_view embeddedHostsCatalog();
// resources/catalog/programs.json (D-078).
[[nodiscard]] std::string_view embeddedProgramsCatalog();
[[nodiscard]] std::string_view embeddedCompatCatalog(); // D-082
// D-103: every welcome translation (welcome-langs.json), parsed once. English is always present.
[[nodiscard]] const std::vector<core::WelcomeLanguage>& embeddedWelcomeLanguages();

[[nodiscard]] HICON appIcon();
// The same icon as a resource name, for Window to load at its DPI (taskbar / Alt+Tab, WM_SETICON).
[[nodiscard]] const wchar_t* appIconResource();

} // namespace wl::app
