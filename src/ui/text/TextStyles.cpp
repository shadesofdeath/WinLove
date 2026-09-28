#include "ui/text/TextStyles.h"

#include "base/Hresult.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace wl::ui {

namespace {

constexpr auto kCode = ErrorCode::RenderFailure;

// Baseline that centers the font's ascent+descent inside the token line height.
float baselineFor(IDWriteFontCollection* collection, const wchar_t* family, DWRITE_FONT_WEIGHT weight,
                  const tokens::TypeSpec& spec) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> fontFamily;
    ComPtr<IDWriteFont> font;
    if (collection && SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists &&
        SUCCEEDED(collection->GetFontFamily(index, &fontFamily)) &&
        SUCCEEDED(fontFamily->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                   &font))) {
        DWRITE_FONT_METRICS m{};
        font->GetMetrics(&m);
        const float em = static_cast<float>(m.designUnitsPerEm);
        const float ascent = m.ascent * spec.size / em;
        const float descent = m.descent * spec.size / em;
        return (spec.lineHeight - (ascent + descent)) / 2 + ascent;
    }
    return spec.lineHeight * 0.78f; // metrics unavailable: typical Latin ratio
}

} // namespace

Result<std::unique_ptr<TextStyles>> TextStyles::create(IDWriteFactory6* factory, const FontLibrary& fonts) {
    auto styles = std::make_unique<TextStyles>();
    styles->m_factory = factory;

    ComPtr<IDWriteInlineObject> ellipsis;
    for (std::size_t i = 0; i < styles->m_formats.size(); ++i) {
        const auto& spec = tokens::kTypeStyles[i];
        const bool mono = spec.role == tokens::FontRole::Mono;
        const wchar_t* primary = mono ? tokens::font::kMono : tokens::font::kUi;
        const bool bundled = fonts.hasFamily(primary);
        // Bundled font if present, otherwise the system fallback family from the system collection.
        const wchar_t* family = bundled ? primary : (mono ? tokens::font::kMonoFallback : tokens::font::kUiFallback);
        IDWriteFontCollection* collection = bundled ? fonts.collection() : nullptr;
        const auto weight = static_cast<DWRITE_FONT_WEIGHT>(spec.weight);

        ComPtr<IDWriteTextFormat> format;
        WL_TRY_HR(factory->CreateTextFormat(family, collection, weight, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, spec.size, L"", &format),
                  kCode, L"creating text format");
        WL_TRY_HR(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, spec.lineHeight,
                                         baselineFor(collection, family, weight, spec)),
                  kCode, L"setting line spacing");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (!ellipsis) {
            WL_TRY_HR(factory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis), kCode, L"creating ellipsis");
        }
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        format->SetTrimming(&trimming, ellipsis.Get());
        styles->m_formats[i] = std::move(format);
    }
    return styles;
}

Result<ComPtr<IDWriteTextLayout>> TextStyles::layout(std::wstring_view text, tokens::TypeStyle style, float width,
                                                     float height, TextAlign align) const {
    const auto& s = spec(style);
    std::wstring shaped(text);
    if (s.uppercase) {
        // Section labels; Turkish-aware casing (i -> İ) comes with the locale-aware helper in Faz 1.7.
        for (auto& c : shaped) {
            c = static_cast<wchar_t>(std::towupper(c));
        }
    }
    ComPtr<IDWriteTextLayout> layout;
    WL_TRY_HR(m_factory->CreateTextLayout(shaped.data(), static_cast<UINT32>(shaped.size()), format(style),
                                          std::max(width, 0.0f), std::max(height, 0.0f), &layout),
              kCode, L"creating text layout");
    layout->SetTextAlignment(align == TextAlign::Center     ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : align == TextAlign::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                            : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (s.letterSpacing != 0.0f) {
        ComPtr<IDWriteTextLayout1> layout1;
        if (SUCCEEDED(layout.As(&layout1))) {
            layout1->SetCharacterSpacing(0, s.letterSpacing, 0, {0, static_cast<UINT32>(shaped.size())});
        }
    }
    return layout;
}

float TextStyles::measure(std::wstring_view text, tokens::TypeStyle style) const {
    auto l = layout(text, style, 100000.0f, spec(style).lineHeight, TextAlign::Leading);
    if (!l) {
        return 0;
    }
    DWRITE_TEXT_METRICS metrics{};
    (*l)->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

} // namespace wl::ui
