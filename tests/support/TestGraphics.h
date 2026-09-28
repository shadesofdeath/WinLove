#pragma once
// Shared, lazily created graphics (device + bundled fonts) for tests that render or measure text.
#include "ui/render/Graphics.h"

namespace wl::test {

ui::Graphics& graphics();

} // namespace wl::test
