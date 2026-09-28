#include "ui/render/Graphics.h"

namespace wl::ui {

Result<std::unique_ptr<Graphics>> Graphics::create(std::span<const FontBytes> fontFiles) {
    auto g = std::make_unique<Graphics>();

    auto device = RenderDevice::create();
    if (!device) {
        return std::unexpected(device.error());
    }
    g->device = std::move(*device);

    auto fonts = FontLibrary::create(g->device->dwrite(), fontFiles);
    if (!fonts) {
        return std::unexpected(fonts.error());
    }
    g->fonts = std::move(*fonts);

    auto text = TextStyles::create(g->device->dwrite(), *g->fonts);
    if (!text) {
        return std::unexpected(text.error());
    }
    g->text = std::move(*text);

    auto icons = IconCache::create(g->device->d2dFactory());
    if (!icons) {
        return std::unexpected(icons.error());
    }
    g->icons = std::move(*icons);
    return g;
}

} // namespace wl::ui
