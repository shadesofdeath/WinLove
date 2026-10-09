#pragma once
// Everything needed to draw: device, fonts, text styles, icon geometries. On device loss only the
// device is made again (RenderDevice::recreateDevice): the rest hangs on the factories, and the
// widget tree that measures with the text styles must outlive it (running jobs report into it).
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
