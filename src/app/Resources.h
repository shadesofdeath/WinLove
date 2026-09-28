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

[[nodiscard]] HICON appIcon();

} // namespace wl::app
