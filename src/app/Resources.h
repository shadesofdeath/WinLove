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

[[nodiscard]] HICON appIcon();

} // namespace wl::app
