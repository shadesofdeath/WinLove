#pragma once
// What the shell's chrome says, made from the state (2026-11-10, UI polish): the status bar's
// segments (D-147: the title bar always says the app's name; the image is named here and on the pages). Free functions, so the tests check them without a window; the
// shell calls them on the AppState notifications it already handles (never per frame).
#include "app/Localization.h"
#include "app/shell/StatusBar.h"
#include "app/state/AppState.h"

#include <string>

namespace wl::app {

// The image (mounted edition or "Bağlı imaj yok"), the queued changes and their estimated effect on
// the image's size; a segment with nothing to say is left empty (not drawn).
[[nodiscard]] StatusBar::Model statusModel(const AppState& state, const Localization& strings, Language language);

} // namespace wl::app
