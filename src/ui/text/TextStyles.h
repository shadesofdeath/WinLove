#pragma once
// One IDWriteTextFormat per design type style (tokens::TypeStyle), with the line height and
// baseline from the token + font metrics, ellipsis trimming and no wrapping.
#include "ui/generated/Tokens.g.h"
#include "ui/text/FontLibrary.h"

#include <array>
#include <string>
#include <string_view>

namespace wl::ui {

enum class TextAlign : std::uint8_t { Leading, Center, Trailing };

// D-092: the type families, chosen in the app's settings (Inter, Geist, Segoe UI Variable). A family
// that is not in the bundled collection comes from the system's, then the fallback.
struct FontFamilies {
    const wchar_t* ui = L"Inter";
    const wchar_t* uiFallback = L"Segoe UI";
    const wchar_t* mono = L"JetBrains Mono";
    const wchar_t* monoFallback = L"Cascadia Mono";
};

class TextStyles {
public:
    [[nodiscard]] static Result<std::unique_ptr<TextStyles>> create(IDWriteFactory6* factory, const FontLibrary& fonts,
                                                                    const FontFamilies& families = {});

    [[nodiscard]] IDWriteTextFormat* format(tokens::TypeStyle style) const noexcept {
        return m_formats[static_cast<std::size_t>(style)].Get();
    }
    [[nodiscard]] static const tokens::TypeSpec& spec(tokens::TypeStyle style) noexcept {
        return tokens::kTypeStyles[static_cast<std::size_t>(style)];
    }

    // Single-line layout vertically centered in `height`; applies letter spacing and uppercase.
    [[nodiscard]] Result<ComPtr<IDWriteTextLayout>> layout(std::wstring_view text, tokens::TypeStyle style,
                                                           float width, float height, TextAlign align) const;
    // Multi-line layout: word wrap at `width`, top-aligned (dialog bodies, descriptions).
    [[nodiscard]] Result<ComPtr<IDWriteTextLayout>> wrappedLayout(std::wstring_view text, tokens::TypeStyle style,
                                                                  float width, TextAlign align = TextAlign::Leading) const;
    // Height of `text` wrapped at `width` (a multiple of the style's line height).
    [[nodiscard]] float measureWrapped(std::wstring_view text, tokens::TypeStyle style, float width) const;
    // UI language for linguistic casing of uppercase styles (tr-TR: i → İ, ı → I).
    void setLocale(std::wstring locale) { m_locale = std::move(locale); }
    [[nodiscard]] const std::wstring& locale() const noexcept { return m_locale; }
    // Width of the text on one line, in DIPs.
    [[nodiscard]] float measure(std::wstring_view text, tokens::TypeStyle style) const;

private:
    ComPtr<IDWriteFactory6> m_factory;
    std::wstring m_locale = L"en-US";
    std::array<ComPtr<IDWriteTextFormat>, static_cast<std::size_t>(tokens::TypeStyle::Count)> m_formats;
};

} // namespace wl::ui
