#pragma once
// SVG path data ("M2 4h12v9H2z ...") -> ID2D1PathGeometry. Supports M L H V C S Q T A Z,
// absolute and relative, with the compact number syntax SVG allows ("1.5.5", "0-12").
#include "ui/render/RenderDevice.h"

#include <string_view>

namespace wl::ui {

// fillEvenOdd: D2D1_FILL_MODE_ALTERNATE (SVG fill-rule="evenodd"); otherwise WINDING.
[[nodiscard]] Result<ComPtr<ID2D1PathGeometry>> buildSvgPath(ID2D1Factory* factory, std::string_view data,
                                                             bool fillEvenOdd = true);

} // namespace wl::ui
