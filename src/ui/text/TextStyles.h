#pragma once
// One IDWriteTextFormat per design type style (tokens::TypeStyle), with the line height and
// baseline from the token + font metrics, ellipsis trimming and no wrapping.
#include "ui/generated/Tokens.g.h"
#include "ui/text/FontLibrary.h"

#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wl::ui {

enum class TextAlign : std::uint8_t { Leading, Center, Trailing };

// D-092: the type families, chosen in the app's settings (Inter, Geist, Segoe UI Variable). A family
// that is not in the bundled collection comes from the system's, then the fallback.
// D-107: the default is the WinLoveUI package's (design/tokens.json typography.family).
struct FontFamilies {
    const wchar_t* ui = tokens::font::kUi;
    const wchar_t* uiFallback = tokens::font::kUiFallback;
    const wchar_t* mono = tokens::font::kMono;
    const wchar_t* monoFallback = tokens::font::kMonoFallback;
    // D-147: the wordmark's own face (the brand's), whatever the interface's family is.
    const wchar_t* brand = tokens::font::kBrand;
    const wchar_t* brandFallback = tokens::font::kBrandFallback;
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
    void setLocale(std::wstring locale);
    [[nodiscard]] const std::wstring& locale() const noexcept { return m_locale; }
    // Width of the text on one line, in DIPs. Short texts are remembered: the same labels, tab
    // titles and options are asked for on every frame and pointer move (audit UI-09 / D4).
    [[nodiscard]] float measure(std::wstring_view text, tokens::TypeStyle style) const;

private:
    struct TextHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::wstring_view text) const noexcept {
            return std::hash<std::wstring_view>{}(text);
        }
    };
    using Measured = std::unordered_map<std::wstring, float, TextHash, std::equal_to<>>;

    // Widths by style and text; they hold for this object's formats and locale. Bounded: emptied
    // when full (texts that keep changing — file names, counters — must not grow it for ever).
    mutable std::mutex m_measureMutex;
    mutable std::array<Measured, static_cast<std::size_t>(tokens::TypeStyle::Count)> m_measured;
    mutable std::size_t m_measuredCount = 0;

    ComPtr<IDWriteFactory6> m_factory;
    std::wstring m_locale = L"en-US";
    std::array<ComPtr<IDWriteTextFormat>, static_cast<std::size_t>(tokens::TypeStyle::Count)> m_formats;
};

} // namespace wl::ui
