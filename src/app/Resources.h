#pragma once
// Access to data embedded in WinLove.exe (see WinLove.rc / resource.h).
#include "app/Localization.h"
#include "ui/text/FontLibrary.h"

#include <windows.h>

#include <string_view>
#include <vector>

namespace wl::app {

// The four bundled font files (IBM Plex Sans 400/500/600, JetBrains Mono 400).
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

[[nodiscard]] HICON appIcon();

} // namespace wl::app
