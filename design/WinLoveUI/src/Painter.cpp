#include "wl/Painter.h"
#include <cmath>
#include <string>

namespace wl {

// Tasarımdaki yazı ölçüleri (DIP). trackingEm: harf aralığı, em cinsinden.
const Painter::StyleDef Painter::kStyles[(int)TextStyle::_Count] = {
    { type::UiFamily,   28.0f, DWRITE_FONT_WEIGHT_BOLD,      -0.025f }, // Title
    { type::UiFamily,   18.0f, DWRITE_FONT_WEIGHT_BOLD,      -0.015f }, // Heading
    { type::UiFamily,   15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.0f   }, // Panel
    { type::UiFamily,   14.0f, DWRITE_FONT_WEIGHT_NORMAL,     0.0f   }, // Body
    { type::UiFamily,   14.0f, DWRITE_FONT_WEIGHT_MEDIUM,     0.0f   }, // BodyMedium
    { type::UiFamily,   14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.0f   }, // BodySemi
    { type::UiFamily,   13.0f, DWRITE_FONT_WEIGHT_NORMAL,     0.0f   }, // Small
    { type::UiFamily,   13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.0f   }, // SmallSemi
    { type::UiFamily,   12.5f, DWRITE_FONT_WEIGHT_NORMAL,     0.0f   }, // Caption
    { type::UiFamily,   12.0f, DWRITE_FONT_WEIGHT_MEDIUM,     0.0f   }, // Count
    { type::UiFamily,   12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.0f   }, // Badge
    { type::UiFamily,   11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.06f  }, // GroupCap
    { type::UiFamily,   12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,  0.04f  }, // SectionCap
    { type::MonoFamily, 12.5f, DWRITE_FONT_WEIGHT_NORMAL,     0.0f   }, // Mono
};

static bool HasFamily(IDWriteFactory* f, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> sys;
    if (FAILED(f->GetSystemFontCollection(&sys, FALSE))) return false;
    UINT32 i = 0; BOOL exists = FALSE;
    sys->FindFamilyName(name, &i, &exists);
    return exists != FALSE;
}

static D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}
static D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float k) { c.a *= k; return c; }
static D2D1_RECT_F Inset(const D2D1_RECT_F& r, float d) { return { r.left + d, r.top + d, r.right - d, r.bottom - d }; }

HRESULT Painter::Init(ID2D1Factory1* d2d, IDWriteFactory* dwrite) {
    d2d_ = d2d; dw_ = dwrite;
    const wchar_t* ui   = HasFamily(dwrite, type::UiFamily)   ? type::UiFamily   : type::UiFallback;
    const wchar_t* mono = HasFamily(dwrite, type::MonoFamily) ? type::MonoFamily : type::MonoFallback;

    for (int i = 0; i < (int)TextStyle::_Count; ++i) {
        const StyleDef& s = kStyles[i];
        const wchar_t* fam = (s.family == type::MonoFamily) ? mono : ui;
        HRESULT hr = dw_->CreateTextFormat(fam, nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, s.size, L"tr-TR", &fmt_[i]);
        if (FAILED(hr)) return hr;
        fmt_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt_[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(fmt_[i].Get(), &ellipsis_[i]))) {
            DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            fmt_[i]->SetTrimming(&tr, ellipsis_[i].Get());
        }
    }
    D2D1_STROKE_STYLE_PROPERTIES1 sp{ D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                      D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f,
                                      D2D1_STROKE_TRANSFORM_TYPE_NORMAL };
    return d2d_->CreateStrokeStyle(sp, nullptr, 0, round_.ReleaseAndGetAddressOf());
}

void Painter::Begin(ID2D1DeviceContext* dc, const Theme& theme) {
    if (dc != dc_ || !brush_) {
        dc_ = dc;
        brush_.Reset();
        dc_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    }
    theme_ = &theme;
}

ID2D1SolidColorBrush* Painter::Br(const D2D1_COLOR_F& c) { brush_->SetColor(c); return brush_.Get(); }

// --- temel -----------------------------------------------------------------
void Painter::Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    if (radius > 0) dc_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Br(c));
    else dc_->FillRectangle(r, Br(c));
}

void Painter::Stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius, float width) {
    D2D1_RECT_F in = Inset(r, width * 0.5f);   // çizgi kutunun içinde kalsın, keskin dursun
    float rr = radius > 0 ? radius - width * 0.5f : 0;
    if (rr > 0) dc_->DrawRoundedRectangle(D2D1::RoundedRect(in, rr, rr), Br(c), width);
    else dc_->DrawRectangle(in, Br(c), width);
}

void Painter::HLine(float x0, float x1, float y, const D2D1_COLOR_F& c, float w) { dc_->FillRectangle({ x0, y, x1, y + w }, Br(c)); }
void Painter::VLine(float x, float y0, float y1, const D2D1_COLOR_F& c, float w) { dc_->FillRectangle({ x, y0, x + w, y1 }, Br(c)); }

void Painter::Dot(float cx, float cy, float size, const D2D1_COLOR_F& c) {
    dc_->FillEllipse(D2D1::Ellipse({ cx, cy }, size * 0.5f, size * 0.5f), Br(c));
}

HRESULT Painter::MakeLayout(std::wstring_view s, TextStyle st, float w, float h, IDWriteTextLayout** out) {
    HRESULT hr = dw_->CreateTextLayout(s.data(), (UINT32)s.size(), fmt_[(int)st].Get(),
                                       w < 0 ? 0 : w, h < 0 ? 0 : h, out);
    if (FAILED(hr)) return hr;
    float tracking = kStyles[(int)st].trackingEm;
    if (tracking != 0) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED((*out)->QueryInterface(IID_PPV_ARGS(&l1)))) {
            float px = tracking * kStyles[(int)st].size;
            l1->SetCharacterSpacing(0, px, 0, { 0, (UINT32)s.size() });
        }
    }
    return S_OK;
}

void Painter::Text(std::wstring_view s, const D2D1_RECT_F& r, TextStyle st, const D2D1_COLOR_F& c, HAlign h) {
    if (s.empty()) return;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(MakeLayout(s, st, r.right - r.left, r.bottom - r.top, &l))) return;
    l->SetTextAlignment(h == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                      : h == HAlign::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
                      : DWRITE_TEXT_ALIGNMENT_LEADING);
    dc_->DrawTextLayout({ r.left, r.top }, l.Get(), Br(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Painter::Paragraph(std::wstring_view s, const D2D1_RECT_F& r, TextStyle st, const D2D1_COLOR_F& c, float lineHeight) {
    if (s.empty()) return 0;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(MakeLayout(s, st, r.right - r.left, 10000.0f, &l))) return 0;
    l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    l->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (lineHeight > 0) l->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineHeight, lineHeight * 0.78f);
    dc_->DrawTextLayout({ r.left, r.top }, l.Get(), Br(c));
    DWRITE_TEXT_METRICS tm{}; l->GetMetrics(&tm);
    return tm.height;
}

float Painter::Measure(std::wstring_view s, TextStyle st) {
    if (s.empty()) return 0;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(MakeLayout(s, st, 100000.0f, 100.0f, &l))) return 0;
    DWRITE_TEXT_METRICS tm{}; l->GetMetrics(&tm);
    return std::ceil(tm.widthIncludingTrailingWhitespace);
}

// --- ikonlar (24 birimlik ızgarada tanımlı, r kutusuna ölçeklenir) ----------
void Painter::DrawIcon(Icon ic, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float sw) {
    const float k = (r.right - r.left) / 24.0f;
    auto P = [&](float x, float y) { return D2D1::Point2F(r.left + x * k, r.top + y * k); };
    auto L = [&](float x0, float y0, float x1, float y1) { dc_->DrawLine(P(x0, y0), P(x1, y1), Br(c), sw, round_.Get()); };
    auto Poly = [&](std::initializer_list<D2D1_POINT_2F> pts, bool closed) {
        ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> s;
        if (FAILED(d2d_->CreatePathGeometry(&g)) || FAILED(g->Open(&s))) return;
        auto it = pts.begin();
        s->BeginFigure(P(it->x, it->y), D2D1_FIGURE_BEGIN_HOLLOW);
        for (++it; it != pts.end(); ++it) s->AddLine(P(it->x, it->y));
        s->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        s->Close();
        dc_->DrawGeometry(g.Get(), Br(c), sw, round_.Get());
    };
    switch (ic) {
    case Icon::Search:
        dc_->DrawEllipse(D2D1::Ellipse(P(11, 11), 7 * k, 7 * k), Br(c), sw);
        L(16.5f, 16.5f, 20, 20); break;
    case Icon::ChevronRight: Poly({ {9, 6}, {15, 12}, {9, 18} }, false); break;
    case Icon::ChevronDown:  Poly({ {6, 9}, {12, 15}, {18, 9} }, false); break;
    case Icon::Check:        Poly({ {5, 12}, {10, 17}, {20, 7} }, false); break;
    case Icon::Close:        L(6, 6, 18, 18); L(18, 6, 6, 18); break;
    case Icon::Plus:         L(12, 5, 12, 19); L(5, 12, 19, 12); break;
    case Icon::Minus:        L(5, 12, 19, 12); break;
    case Icon::ArrowRight:   L(5, 12, 19, 12); Poly({ {13, 6}, {19, 12}, {13, 18} }, false); break;
    case Icon::Minimize:     L(0, 12, 24, 12); break;
    case Icon::Maximize:
        dc_->DrawRoundedRectangle(D2D1::RoundedRect({ P(1, 1).x, P(1, 1).y, P(23, 23).x, P(23, 23).y }, 3.6f * k, 3.6f * k), Br(c), sw);
        break;
    case Icon::Warning:
        Poly({ {12, 3}, {22, 21}, {2, 21} }, true);
        L(12, 10, 12, 14); dc_->FillEllipse(D2D1::Ellipse(P(12, 17.5f), sw * 0.6f, sw * 0.6f), Br(c));
        break;
    case Icon::Sun: {
        dc_->DrawEllipse(D2D1::Ellipse(P(12, 12), 4 * k, 4 * k), Br(c), sw);
        for (int i = 0; i < 8; ++i) {
            float a = i * 3.14159265f / 4.0f, cs = std::cos(a), sn = std::sin(a);
            L(12 + cs * 7.5f, 12 + sn * 7.5f, 12 + cs * 9.5f, 12 + sn * 9.5f);
        }
        break;
    }
    case Icon::Moon: case Icon::Reset: {
        ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> s;
        if (FAILED(d2d_->CreatePathGeometry(&g)) || FAILED(g->Open(&s))) break;
        if (ic == Icon::Moon) {
            s->BeginFigure(P(20, 14.5f), D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddArc(D2D1::ArcSegment(P(9.5f, 4), { 8 * k, 8 * k }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            s->AddArc(D2D1::ArcSegment(P(20, 14.5f), { 8 * k, 8 * k }, 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
        } else { // varsayılana dön: açık çember + ok
            s->BeginFigure(P(4.5f, 9.3f), D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddArc(D2D1::ArcSegment(P(4.5f, 14.7f), { 8 * k, 8 * k }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            s->EndFigure(D2D1_FIGURE_END_OPEN);
        }
        s->Close();
        dc_->DrawGeometry(g.Get(), Br(c), sw, round_.Get());
        if (ic == Icon::Reset) Poly({ {4, 4}, {4, 9.5f}, {9.5f, 9.5f} }, false);
        break;
    }
    }
}

// --- bileşenler ------------------------------------------------------------
void Painter::Button(const D2D1_RECT_F& r, std::wstring_view label, ButtonKind k, State s) {
    const Theme& t = *theme_;
    float a = s.disabled ? 0.42f : 1.0f;
    float rad = m::BtnRadius;
    D2D1_COLOR_F fg = t.text;
    if (s.focus) Stroke({ r.left - 3, r.top - 3, r.right + 3, r.bottom + 3 }, t.acSoft, rad + 3, m::FocusRing);
    switch (k) {
    case ButtonKind::Primary: {
        D2D1_COLOR_F bg = t.ac;
        if (!s.disabled && s.pressed) bg = Mix(bg, Rgb(0x000000), 0.10f);
        else if (!s.disabled && s.hover) bg = Mix(bg, Rgb(0xFFFFFF), 0.08f);
        Fill(r, WithAlpha(bg, a), rad);
        fg = t.onAccent;
        break;
    }
    case ButtonKind::Secondary:
        Fill(r, WithAlpha((s.hover && !s.disabled) ? t.sunk : t.surface, a), rad);
        Stroke(r, WithAlpha(t.line, a), rad);
        break;
    case ButtonKind::Ghost:
        if (s.hover && !s.disabled) Fill(r, t.hover, rad);
        fg = t.acFg;
        break;
    }
    Text(label, r, k == ButtonKind::Primary ? TextStyle::BodySemi : TextStyle::BodyMedium, WithAlpha(fg, a), HAlign::Center);
}

void Painter::MenuButton(const D2D1_RECT_F& r, std::wstring_view label, bool active, State s) {
    const Theme& t = *theme_;
    if (active) Fill(r, t.sunk, 7);
    else if (s.hover) Fill(r, t.hover, 7);
    Text(label, r, active ? TextStyle::BodySemi : TextStyle::BodyMedium,
         (active || s.hover) ? t.text : t.text2, HAlign::Center);
}

void Painter::CaptionButton(const D2D1_RECT_F& r, Icon ic, bool isClose, State s) {
    const Theme& t = *theme_;
    D2D1_COLOR_F fg = t.text2;
    if (s.hover) {
        if (isClose) { Fill(r, t.closeHover); fg = Rgb(0xFFFFFF); }
        else Fill(r, t.hover);
    }
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    bool glyph = ic == Icon::Minimize || ic == Icon::Maximize || ic == Icon::Close;
    float sz = glyph ? 10.0f : 17.0f;
    DrawIcon(ic, { cx - sz / 2, cy - sz / 2, cx + sz / 2, cy + sz / 2 }, fg, glyph ? 1.0f : 1.8f);
}

float Painter::TabWidth(std::wstring_view label, int count) {
    float w = Measure(label, TextStyle::BodySemi);
    if (count >= 0) w += m::CountGap + Measure(std::to_wstring(count), TextStyle::Count);
    return w + 2;
}

void Painter::Tab(const D2D1_RECT_F& r, std::wstring_view label, int count, bool active, State s) {
    const Theme& t = *theme_;
    TextStyle st = active ? TextStyle::BodySemi : TextStyle::BodyMedium;
    float lw = Measure(label, st);
    Text(label, { r.left + 1, r.top, r.left + 1 + lw + 1, r.bottom }, st, (active || s.hover) ? t.text : t.text2);
    if (count >= 0) {
        std::wstring n = std::to_wstring(count);
        float x = r.left + 1 + lw + m::CountGap;
        Text(n, { x, r.top, x + Measure(n, TextStyle::Count) + 1, r.bottom }, TextStyle::Count, count > 0 ? t.acFg : t.text3);
    }
    if (active) Fill({ r.left, r.bottom - m::TabUnderlineH, r.right, r.bottom }, t.ac, 1);
}

void Painter::GroupCaption(const D2D1_RECT_F& r, std::wstring_view upperText) {
    Text(upperText, r, TextStyle::GroupCap, theme_->text3);
}

void Painter::SearchBox(const D2D1_RECT_F& r, std::wstring_view placeholder, std::wstring_view text,
                        std::wstring_view shortcut, State s) {
    const Theme& t = *theme_;
    if (s.focus) Stroke({ r.left - 3, r.top - 3, r.right + 3, r.bottom + 3 }, t.acSoft, m::InputRadius + 3, m::FocusRing);
    Fill(r, t.surface, m::InputRadius);
    Stroke(r, s.focus ? t.ac : (s.hover ? t.line2 : t.line), m::InputRadius);
    float cy = (r.top + r.bottom) * 0.5f;
    DrawIcon(Icon::Search, { r.left + 12, cy - 7.5f, r.left + 27, cy + 7.5f }, t.text3, 2.0f);
    float right = r.right - 8;
    if (!shortcut.empty()) {
        float kw = Measure(shortcut, TextStyle::Caption) + 12;
        D2D1_RECT_F kr{ right - kw, cy - 10, right, cy + 10 };
        Stroke(kr, t.line, 5);
        Text(shortcut, kr, TextStyle::Caption, t.text3, HAlign::Center);
        right = kr.left - 8;
    }
    D2D1_RECT_F tr{ r.left + 36, r.top, right, r.bottom };
    if (text.empty()) Text(placeholder, tr, TextStyle::Body, t.text3);
    else Text(text, tr, TextStyle::Body, t.text);
}

void Painter::Pill(const D2D1_RECT_F& r, std::wstring_view label, bool on, State s) {
    const Theme& t = *theme_;
    float rad = (r.bottom - r.top) * 0.5f;
    if (on) { Fill(r, t.text, rad); Text(label, r, TextStyle::SmallSemi, t.bg, HAlign::Center); }
    else {
        Stroke(r, t.line, rad);
        Text(label, r, TextStyle::Small, s.hover ? t.text : t.text2, HAlign::Center);
    }
}

void Painter::Checkbox(const D2D1_RECT_F& r, bool checked, bool mixed, State s) {
    const Theme& t = *theme_;
    if (s.focus) Stroke({ r.left - 4, r.top - 4, r.right + 4, r.bottom + 4 }, t.ac, m::CheckRadius + 4, 2);
    if (checked || mixed) {
        Fill(r, t.ac, m::CheckRadius);
        D2D1_RECT_F in = Inset(r, 2.5f);
        DrawIcon(checked ? Icon::Check : Icon::Minus, in, t.onAccent, 2.0f);
    } else {
        Fill(r, t.surface, m::CheckRadius);
        Stroke(r, s.hover ? t.text3 : t.line2, m::CheckRadius, 1.5f);
    }
}

void Painter::Switch(const D2D1_RECT_F& r, float anim, State s) {
    const Theme& t = *theme_;
    float h = r.bottom - r.top, rad = h * 0.5f;
    if (s.focus) Stroke({ r.left - 4, r.top - 4, r.right + 4, r.bottom + 4 }, t.ac, rad + 4, 2);
    Fill(r, Mix(t.line2, t.ac, anim), rad);
    float d = m::SwitchThumb, pad = (h - d) * 0.5f;
    float x = r.left + pad + anim * ((r.right - r.left) - 2 * pad - d);
    float cy = r.top + h * 0.5f;
    dc_->FillEllipse(D2D1::Ellipse({ x + d / 2, cy + 1 }, d / 2, d / 2), Br(Rgb(0x000000, 0.18f)));
    dc_->FillEllipse(D2D1::Ellipse({ x + d / 2, cy }, d / 2, d / 2), Br(Rgb(0xFFFFFF)));
}

void Painter::Segmented(const D2D1_RECT_F& r, const std::wstring_view* labels, int n, int selected, int hoverIndex) {
    const Theme& t = *theme_;
    Fill(r, t.sunk, m::SegRadius);
    if (n <= 0) return;
    float p = m::SegPad, w = (r.right - r.left - 2 * p - (n - 1) * 2) / n;
    for (int i = 0; i < n; ++i) {
        D2D1_RECT_F c{ r.left + p + i * (w + 2), r.top + p, r.left + p + i * (w + 2) + w, r.bottom - p };
        bool on = i == selected;
        if (on) { Fill(c, t.surface, m::SegInnerRadius); Stroke(c, t.line2, m::SegInnerRadius); }
        Text(labels[i], c, on ? TextStyle::SmallSemi : TextStyle::Small,
             (on || i == hoverIndex) ? t.text : t.text2, HAlign::Center);
    }
}

void Painter::Progress(const D2D1_RECT_F& r, float v) {
    const Theme& t = *theme_;
    float rad = (r.bottom - r.top) * 0.5f;
    Fill(r, t.line, rad);
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    if (v > 0) Fill({ r.left, r.top, r.left + (r.right - r.left) * v, r.bottom }, t.ac, rad);
}

float Painter::Badge(float x, float y, std::wstring_view s, const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg) {
    float w = Measure(s, TextStyle::Badge) + 16;
    D2D1_RECT_F r{ x, y, x + w, y + 22 };
    Fill(r, bg, m::BadgeRadius);
    Text(s, r, TextStyle::Badge, fg, HAlign::Center);
    return w;
}

float Painter::StatusText(float x, const D2D1_RECT_F& row, std::wstring_view s,
                          const D2D1_COLOR_F& dot, const D2D1_COLOR_F& fg) {
    float cy = (row.top + row.bottom) * 0.5f;
    Dot(x + m::DotSize * 0.5f, cy, m::DotSize, dot);
    float tw = Measure(s, TextStyle::Small);
    Text(s, { x + m::DotSize + 7, row.top, x + m::DotSize + 7 + tw + 1, row.bottom }, TextStyle::Small, fg);
    return m::DotSize + 7 + tw;
}

void Painter::RowBackground(const D2D1_RECT_F& r, bool selected, bool hover, bool bottomLine) {
    const Theme& t = *theme_;
    if (selected) {
        Fill(r, t.acSoft);
        Fill({ r.left, r.top + 10, r.left + m::RowSelBarW, r.bottom - 10 }, t.ac, 1);
    } else if (hover) {
        Fill(r, t.hover);
    }
    if (bottomLine) HLine(r.left, r.right, r.bottom - 1, t.line);
}

} // namespace wl
