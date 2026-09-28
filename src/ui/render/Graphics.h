#pragma once
// Everything needed to draw: device, fonts, text styles, icon geometries. Rebuilt as a whole on
// device loss (cheap: fonts are copied bytes, geometries are rebuilt lazily).
#include "ui/icons/IconCache.h"
#include "ui/render/RenderDevice.h"
#include "ui/text/FontLibrary.h"
#include "ui/text/TextStyles.h"

#include <span>

namespace wl::ui {

struct Graphics {
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<FontLibrary> fonts;
    std::unique_ptr<TextStyles> text;
    std::unique_ptr<IconCache> icons;

    [[nodiscard]] static Result<std::unique_ptr<Graphics>> create(std::span<const FontBytes> fontFiles);
};

} // namespace wl::ui
