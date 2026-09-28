#pragma once
// Private font collection built from font file bytes (embedded resources in the app, files in
// tests). Uses the typographic family model so "IBM Plex Sans" + weight 400/500/600 resolve to
// the Regular/Medium/SemiBold files.
#include "ui/render/RenderDevice.h"

#include <cstddef>
#include <span>

namespace wl::ui {

struct FontBytes {
    const void* data;
    std::size_t size;
};

class FontLibrary {
public:
    [[nodiscard]] static Result<std::unique_ptr<FontLibrary>> create(IDWriteFactory6* factory,
                                                                    std::span<const FontBytes> fonts);
    ~FontLibrary();

    [[nodiscard]] IDWriteFontCollection2* collection() const noexcept { return m_collection.Get(); }
    [[nodiscard]] bool hasFamily(const wchar_t* name) const;

private:
    ComPtr<IDWriteFactory6> m_factory;
    ComPtr<IDWriteInMemoryFontFileLoader> m_loader;
    ComPtr<IDWriteFontCollection2> m_collection;
};

} // namespace wl::ui
