#include "app/Resources.h"

#include "app/resource.h"

namespace wl::app {

namespace {

// Resource memory stays mapped for the life of the process; no copy needed.
std::string_view resourceBytes(int id) {
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!info) {
        return {};
    }
    const HGLOBAL handle = LoadResource(module, info);
    const void* data = handle ? LockResource(handle) : nullptr;
    return data ? std::string_view(static_cast<const char*>(data), SizeofResource(module, info)) : std::string_view{};
}

} // namespace

std::vector<ui::FontBytes> embeddedFonts() {
    std::vector<ui::FontBytes> fonts;
    for (const int id : {IDR_FONT_UI_REGULAR, IDR_FONT_UI_MEDIUM, IDR_FONT_UI_SEMIBOLD, IDR_FONT_MONO_REGULAR}) {
        if (const auto bytes = resourceBytes(id); !bytes.empty()) {
            fonts.push_back({bytes.data(), bytes.size()});
        }
    }
    return fonts;
}

Result<Localization> embeddedStrings(Language language) {
    const auto bytes = resourceBytes(language == Language::Turkish ? IDR_STRINGS_TR : IDR_STRINGS_EN);
    if (bytes.empty()) {
        return fail(ErrorCode::NotFound, L"embedded strings resource missing");
    }
    return Localization::fromJson(bytes);
}

std::string_view embeddedAppxCatalog() {
    return resourceBytes(IDR_CATALOG_APPX);
}

std::string_view embeddedServiceCatalog() {
    return resourceBytes(IDR_CATALOG_SERVICES);
}

std::string_view embeddedTweakCatalog() {
    return resourceBytes(IDR_CATALOG_TWEAKS);
}

HICON appIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_WINLOVE));
}

} // namespace wl::app
