#include "ui/text/FontLibrary.h"

#include "base/Hresult.h"

namespace wl::ui {

Result<std::unique_ptr<FontLibrary>> FontLibrary::create(IDWriteFactory6* factory, std::span<const FontBytes> fonts) {
    constexpr auto kCode = ErrorCode::RenderFailure;
    auto library = std::make_unique<FontLibrary>();
    library->m_factory = factory;

    WL_TRY_HR(factory->CreateInMemoryFontFileLoader(&library->m_loader), kCode, L"creating font loader");
    WL_TRY_HR(factory->RegisterFontFileLoader(library->m_loader.Get()), kCode, L"registering font loader");

    ComPtr<IDWriteFontSetBuilder2> builder;
    WL_TRY_HR(factory->CreateFontSetBuilder(&builder), kCode, L"creating font set builder");
    for (const auto& font : fonts) {
        ComPtr<IDWriteFontFile> file;
        // No owner object: DirectWrite copies the bytes, so callers may free them afterwards.
        WL_TRY_HR(library->m_loader->CreateInMemoryFontFileReference(factory, font.data,
                                                                     static_cast<UINT32>(font.size), nullptr, &file),
                  kCode, L"referencing font bytes");
        WL_TRY_HR(builder->AddFontFile(file.Get()), kCode, L"adding font file");
    }
    ComPtr<IDWriteFontSet> set;
    WL_TRY_HR(builder->CreateFontSet(&set), kCode, L"building font set");
    WL_TRY_HR(factory->CreateFontCollectionFromFontSet(set.Get(), DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC,
                                                       &library->m_collection),
              kCode, L"creating font collection");
    return library;
}

FontLibrary::~FontLibrary() {
    if (m_factory && m_loader) {
        m_factory->UnregisterFontFileLoader(m_loader.Get());
    }
}

bool FontLibrary::hasFamily(const wchar_t* name) const {
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(m_collection->FindFamilyName(name, &index, &exists)) && exists;
}

} // namespace wl::ui
