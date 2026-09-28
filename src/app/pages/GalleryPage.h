#pragma once
// Developer widget gallery (ROADMAP 1.8): every widget in every state, side by side, for visual
// checks against 03_components (components-gallery.html). Reached with --page=gallery or
// Ctrl+Shift+G. Dev-only: its row labels are literals (CONVENTIONS.md exemption).
#include "ui/widget/Stack.h"

namespace wl::app {

class GalleryPage : public ui::Stack {
public:
    GalleryPage();
};

} // namespace wl::app
