#pragma once
// One IDWriteTextFormat per design type style (tokens::TypeStyle), with the line height and
// baseline from the token + font metrics, ellipsis trimming and no wrapping.
#include "ui/generated/Tokens.g.h"
#include "ui/text/FontLibrary.h"

#include <array>
#include <string_view>

namespace wl::ui {

enum class TextAlign : std::uint8_t { Leading, Center, Trailing };

class TextStyles {
public:
    [[nodiscard]] static Result<std::unique_ptr<TextStyles>> create(IDWriteFactory6* factory, const FontLibrary& fonts);

    [[nodiscard]] IDWriteTextFormat* format(tokens::TypeStyle style) const noexcept {
        return m_formats[static_cast<std::size_t>(style)].Get();
    }
    [[nodiscard]] static const tokens::TypeSpec& spec(tokens::TypeStyle style) noexcept {
        return tokens::kTypeStyles[static_cast<std::size_t>(style)];
    }

    // Single-line layout vertically centered in `height`; applies letter spacing and uppercase.
    [[nodiscard]] Result<ComPtr<IDWriteTextLayout>> layout(std::wstring_view text, tokens::TypeStyle style,
                                                           float width, float height, TextAlign align) const;
    // Width of the text on one line, in DIPs.
    [[nodiscard]] float measure(std::wstring_view text, tokens::TypeStyle style) const;

private:
    ComPtr<IDWriteFactory6> m_factory;
    std::array<ComPtr<IDWriteTextFormat>, static_cast<std::size_t>(tokens::TypeStyle::Count)> m_formats;
};

} // namespace wl::ui
