#include "ui/render/Canvas.h"
#include "ui/render/IconTone.h"

#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace wl::ui {

namespace {

D2D1_RECT_F toD2D(RectF r) {
    return D2D1::RectF(r.x, r.y, r.right(), r.bottom());
}

// File icons by (device, file, index, pixels); a failed load is kept too (nullptr), so a missing
// icon is not looked for again every frame. Bitmaps belong to their device: another device
// (after a device loss) starts a fresh cache.
struct FileIconCache {
    ID2D1Device* device = nullptr;
    std::map<std::tuple<std::wstring, int, int>, ComPtr<ID2D1Bitmap>> bitmaps;
};
FileIconCache& fileIcons() {
    static FileIconCache cache;
    return cache;
}

// A picture file (PNG, JPEG …) fitted into pixels × pixels — what an icon picked as a picture becomes.
ComPtr<ID2D1Bitmap> loadPictureIcon(ID2D1DeviceContext2* context, const std::wstring& file, int pixels) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT w = 0;
    UINT h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
        return nullptr;
    }
    const double scale = std::min(static_cast<double>(pixels) / w, static_cast<double>(pixels) / h);
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<ID2D1Bitmap> out;
    if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame.Get(), std::max<UINT>(1, static_cast<UINT>(w * scale + 0.5)),
                                     std::max<UINT>(1, static_cast<UINT>(h * scale + 0.5)),
                                     WICBitmapInterpolationModeHighQualityCubic)) &&
        SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeMedianCut))) {
        context->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &out);
    }
    return out;
}

ComPtr<ID2D1Bitmap> loadFileIcon(ID2D1DeviceContext2* context, const std::wstring& file, int index, int pixels) {
    HICON icon = nullptr;
    UINT id = 0;
    if (PrivateExtractIconsW(file.c_str(), index, pixels, pixels, &icon, &id, 1, LR_DEFAULTCOLOR) != 1 || !icon) {
        return index == 0 ? loadPictureIcon(context, file, pixels) : nullptr;
    }
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<ID2D1Bitmap> out;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bitmap)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeMedianCut))) {
        context->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &out);
    }
    DestroyIcon(icon);
    return out;
}

} // namespace

Canvas::Canvas(ID2D1DeviceContext2* context, ThemeKind theme, float scale, const TextStyles& text, IconCache& icons)
    : m_context(context), m_theme(theme), m_scale(scale), m_text(text), m_icons(icons) {
    m_context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_brush);
}

Rgba Canvas::resolve(Ink ink) const noexcept {
    const Rgba a = ui::color(m_theme, ink.from);
    if (ink.t <= 0.0f || ink.from == ink.to) {
        return {a.r, a.g, a.b, a.a * ink.opacity};
    }
    const Rgba b = ui::color(m_theme, ink.to);
    const float t = std::min(ink.t, 1.0f);
    auto mix = [t](float x, float y) { return x + (y - x) * t; };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a) * ink.opacity};
}

ID2D1SolidColorBrush* Canvas::brush(Ink ink) {
    const Rgba c = resolve(ink);
    m_brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
    return m_brush.Get();
}

void Canvas::clear(tokens::Color color) {
    const Rgba c = ui::color(m_theme, color);
    m_context->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a));
}

void Canvas::fillRect(RectF rect, Ink ink) {
    m_context->FillRectangle(toD2D(rect), brush(ink));
}

void Canvas::fillRoundRect(RectF rect, float radius, Ink ink) {
    if (radius <= 0) {
        fillRect(rect, ink);
        return;
    }
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), brush(ink));
}

void Canvas::fillRoundRect(RectF rect, float radius, Rgba color) {
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), m_brush.Get());
}

void Canvas::strokeRoundRect(RectF rect, float radius, Ink ink, float widthPx) {
    // Snap the outer edge to pixels, then inset half the stroke so the line lands on whole pixels.
    const float w = widthPx * px();
    const RectF snapped{snap(rect.x, m_scale), snap(rect.y, m_scale), snap(rect.width, m_scale),
                        snap(rect.height, m_scale)};
    const RectF inner = snapped.inset(w / 2, w / 2);
    const float r = std::max(radius - w / 2, 0.0f);
    if (r > 0) {
        m_context->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(inner), r, r), brush(ink), w);
    } else {
        m_context->DrawRectangle(toD2D(inner), brush(ink), w);
    }
}

void Canvas::hairlineH(float x, float y, float width, Ink ink) {
    fillRect({snap(x, m_scale), snap(y, m_scale), snap(width, m_scale), px()}, ink);
}

void Canvas::hairlineV(float x, float y, float height, Ink ink) {
    fillRect({snap(x, m_scale), snap(y, m_scale), px(), snap(height, m_scale)}, ink);
}

void Canvas::line(PointF from, PointF to, Ink ink, float widthPx) {
    m_context->DrawLine({from.x, from.y}, {to.x, to.y}, brush(ink), widthPx * px(), m_icons.strokeStyle());
}

void Canvas::drawGlyph(Glyph glyph, RectF box, Ink ink, float strokeDip) {
    // Painter::DrawIcon of the package, on its 24 grid.
    const float k = box.width / 24.0f;
    auto P = [&](float x, float y) { return D2D1::Point2F(box.x + x * k, box.y + y * k); };
    ID2D1Brush* b = brush(ink);
    ID2D1StrokeStyle* round = m_icons.strokeStyle();
    ComPtr<ID2D1Factory> factory;
    m_context->GetFactory(&factory);
    auto path = [&](auto&& build) {
        ComPtr<ID2D1PathGeometry> g;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) {
            return;
        }
        build(sink.Get());
        sink->Close();
        m_context->DrawGeometry(g.Get(), b, strokeDip, round);
    };
    switch (glyph) {
    case Glyph::Sun:
        m_context->DrawEllipse(D2D1::Ellipse(P(12, 12), 4 * k, 4 * k), b, strokeDip);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
            const float c = std::cos(a);
            const float s = std::sin(a);
            m_context->DrawLine(P(12 + c * 7.5f, 12 + s * 7.5f), P(12 + c * 9.5f, 12 + s * 9.5f), b, strokeDip, round);
        }
        break;
    case Glyph::Moon:
        path([&](ID2D1GeometrySink* s) {
            s->BeginFigure(P(20, 14.5f), D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddArc(D2D1::ArcSegment(P(9.5f, 4), {8 * k, 8 * k}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            s->AddArc(D2D1::ArcSegment(P(20, 14.5f), {8 * k, 8 * k}, 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                       D2D1_ARC_SIZE_LARGE));
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        break;
    case Glyph::Reset:
        path([&](ID2D1GeometrySink* s) {
            s->BeginFigure(P(4.5f, 9.3f), D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddArc(D2D1::ArcSegment(P(4.5f, 14.7f), {8 * k, 8 * k}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            s->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        path([&](ID2D1GeometrySink* s) {
            s->BeginFigure(P(4, 4), D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddLine(P(4, 9.5f));
            s->AddLine(P(9.5f, 9.5f));
            s->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        break;
    }
}

void Canvas::fillEllipse(PointF center, float radius, Ink ink) {
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), brush(ink));
}

void Canvas::fillEllipse(PointF center, float radius, Rgba color) {
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), m_brush.Get());
}

void Canvas::strokeCircle(PointF center, float radius, Ink ink, float widthDip) {
    m_context->DrawEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), brush(ink), widthDip);
}

void Canvas::strokeArc(PointF center, float radius, float startDegrees, float sweepDegrees, Ink ink, float widthDip) {
    if (sweepDegrees >= 359.99f) {
        strokeCircle(center, radius, ink, widthDip);
        return;
    }
    if (sweepDegrees <= 0.0f || radius <= 0.0f) {
        return;
    }
    // One arc segment: a geometry this small costs a few microseconds to make, and the sweep changes
    // every frame of a turning ring, so it is not kept (as drawGlyph's).
    ComPtr<ID2D1Factory> factory;
    m_context->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) {
        return;
    }
    constexpr float kRadians = 3.14159265358979f / 180.0f;
    const float a0 = startDegrees * kRadians;
    const float a1 = (startDegrees + sweepDegrees) * kRadians;
    sink->BeginFigure({center.x + radius * std::cos(a0), center.y + radius * std::sin(a0)}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment({center.x + radius * std::cos(a1), center.y + radius * std::sin(a1)}, {radius, radius}, 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, sweepDegrees > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) {
        return;
    }
    m_context->DrawGeometry(path.Get(), brush(ink), widthDip, m_icons.strokeStyle());
}

void Canvas::fillLinearGradient(RectF rect, float radius, std::span<const Rgba> stops, bool vertical) {
    if (stops.empty() || rect.width <= 0 || rect.height <= 0) {
        return;
    }
    // At most a hue bar's seven stops: a small array, no allocation.
    std::array<D2D1_GRADIENT_STOP, 8> list{};
    const std::size_t count = std::min(stops.size(), list.size());
    for (std::size_t i = 0; i < count; ++i) {
        const Rgba c = stops[i];
        list[i] = {count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f, D2D1::ColorF(c.r, c.g, c.b, c.a)};
    }
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    const D2D1_POINT_2F end = vertical ? D2D1::Point2F(rect.x, rect.bottom()) : D2D1::Point2F(rect.right(), rect.y);
    if (FAILED(m_context->CreateGradientStopCollection(list.data(), static_cast<UINT32>(count), &collection)) ||
        FAILED(m_context->CreateLinearGradientBrush({{rect.x, rect.y}, end}, collection.Get(), &gradient))) {
        return;
    }
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), gradient.Get());
}

void Canvas::strokeRoundRect(RectF rect, float radius, Rgba color, float widthPx) {
    const float w = widthPx * px();
    const RectF inner = rect.inset(w / 2, w / 2);
    const float r = std::max(radius - w / 2, 0.0f);
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(inner), r, r), m_brush.Get(), w);
}

void Canvas::drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, Rgba color, TextAlign align) {
    auto layout = m_text.layout(text, style, rect.width, rect.height, align);
    if (!layout) {
        return;
    }
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->DrawTextLayout({rect.x, rect.y}, layout->Get(), m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::drawIcon(icons::Icon icon, PointF topLeft, Rgba color, IconVariant variant, float size) {
    const auto entry = m_icons.get(icon, variant);
    if (!entry.geometry) {
        return;
    }
    const float k = (size > 0 ? size : entry.gridSize) / entry.gridSize;
    D2D1_MATRIX_3X2_F previous;
    m_context->GetTransform(&previous);
    m_context->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                            D2D1::Matrix3x2F::Translation(snap(topLeft.x, m_scale), snap(topLeft.y, m_scale)) * previous);
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    if (entry.filled) {
        m_context->FillGeometry(entry.geometry, m_brush.Get());
    }
    m_context->DrawGeometry(entry.geometry, m_brush.Get(), entry.strokeWidth, m_icons.strokeStyle());
    m_context->SetTransform(previous);
}

void Canvas::fillGradient(RectF rect, float radius, Rgba from, Rgba to) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, D2D1::ColorF(from.r, from.g, from.b, from.a)},
                                        {1.0f, D2D1::ColorF(to.r, to.g, to.b, to.a)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    if (FAILED(m_context->CreateGradientStopCollection(stops, 2, &collection)) ||
        FAILED(m_context->CreateLinearGradientBrush({{rect.x, rect.y}, {rect.right(), rect.bottom()}}, collection.Get(), &gradient))) {
        return;
    }
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), gradient.Get());
}

void Canvas::fillGlow(PointF center, float radiusX, float radiusY, Rgba color) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, D2D1::ColorF(color.r, color.g, color.b, color.a)},
                                        {0.5f, D2D1::ColorF(color.r, color.g, color.b, color.a * 0.4f)},
                                        {1.0f, D2D1::ColorF(color.r, color.g, color.b, 0.0f)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> glow;
    if (FAILED(m_context->CreateGradientStopCollection(stops, 3, &collection)) ||
        FAILED(m_context->CreateRadialGradientBrush({{center.x, center.y}, {0, 0}, radiusX, radiusY}, collection.Get(), &glow))) {
        return;
    }
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radiusX, radiusY), glow.Get());
}

void Canvas::fillPolygon(std::span<const PointF> points, Rgba color) {
    if (points.size() < 3) {
        return;
    }
    ComPtr<ID2D1Factory> factory;
    m_context->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(factory->CreatePathGeometry(&path))) {
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) {
        return;
    }
    sink->BeginFigure({points[0].x, points[0].y}, D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < points.size(); ++i) {
        sink->AddLine({points[i].x, points[i].y});
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return;
    }
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->FillGeometry(path.Get(), m_brush.Get());
}

void Canvas::panel(RectF rect, float radius, std::span<const tokens::Shadow> elevation) {
    dropShadow(rect, radius, elevation);
    fillRoundRect(rect, radius, tokens::Color::BgOverlay);
    strokeRoundRect(rect, radius, tokens::Color::LineStrong);
}

void Canvas::dropShadow(RectF rect, float radius, std::span<const tokens::Shadow> layers) {
    // Record the shape once, then blur it per layer with the D2D shadow effect.
    ComPtr<ID2D1CommandList> shape;
    if (FAILED(m_context->CreateCommandList(&shape))) {
        return;
    }
    ComPtr<ID2D1Image> previousTarget;
    m_context->GetTarget(&previousTarget);
    m_context->SetTarget(shape.Get());
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), brush(tokens::Color::TextPrimary));
    m_context->SetTarget(previousTarget.Get());
    if (FAILED(shape->Close())) {
        return;
    }
    for (const auto& layer : layers) {
        ComPtr<ID2D1Effect> shadow;
        if (FAILED(m_context->CreateEffect(CLSID_D2D1Shadow, &shadow))) {
            return;
        }
        const Rgba c = ui::color(m_theme, layer.color);
        shadow->SetInput(0, shape.Get());
        // Design blur radius (CSS-like) ≈ 2 standard deviations.
        shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, layer.blurRadius / 2.0f);
        shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(c.r, c.g, c.b, c.a));
        const D2D1_POINT_2F offset{layer.offsetX, layer.offsetY};
        m_context->DrawImage(shadow.Get(), &offset);
    }
}

bool nineSlices(float size, float corner, RectF dest, std::span<NineSlice, 8> out) noexcept {
    if (dest.width < 2 * corner || dest.height < 2 * corner || size < 2 * corner) {
        return false;
    }
    // Columns / rows: the corner, the stretched middle, the corner.
    const float sx[4] = {0, corner, size - corner, size};
    const float sy[4] = {0, corner, size - corner, size};
    const float dx[4] = {dest.x, dest.x + corner, dest.right() - corner, dest.right()};
    const float dy[4] = {dest.y, dest.y + corner, dest.bottom() - corner, dest.bottom()};
    std::size_t n = 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (row == 1 && column == 1) {
                continue; // under the card
            }
            out[n++] = {{sx[column], sy[row], sx[column + 1] - sx[column], sy[row + 1] - sy[row]},
                        {dx[column], dy[row], dx[column + 1] - dx[column], dy[row + 1] - dy[row]}};
        }
    }
    return true;
}

namespace {

// D-136: card shadows, blurred once per (device, theme, scale, radius, layers) into a small bitmap:
// a rounded square with room for its corners, the shadow's reach around it and a one-pixel middle
// row and column. Bitmaps belong to their device: another device starts a fresh cache.
struct CardShadowBitmap {
    ThemeKind theme;
    float scale;
    float radius;
    const tokens::Shadow* layers;
    std::size_t layerCount;
    ComPtr<ID2D1Bitmap1> bitmap; // null: nothing to draw (no layer has any colour: high contrast)
    float size = 0;              // px, square
    float corner = 0;            // px: a corner piece
    float margin = 0;            // px: the shadow's reach outside the card
    float inner = 0;             // px: how far a corner piece reaches inside the card
};
struct CardShadowCache {
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> baker; // its own context: the frame's target and clips stay as they are
    std::vector<CardShadowBitmap> kept;
};
CardShadowCache& cardShadows() {
    static CardShadowCache cache;
    return cache;
}
constexpr std::size_t kCardShadowsMost = 16; // themes × scales × radii seen in one run; emptied when full

} // namespace

std::size_t Canvas::cardShadowsKept() noexcept {
    return cardShadows().kept.size();
}

void Canvas::cardShadow(RectF rect, float radius) {
    const std::span<const tokens::Shadow> layers = tokens::elevation::card;
    auto& cache = cardShadows();
    ComPtr<ID2D1Device> device;
    m_context->GetDevice(&device);
    if (!device) {
        return;
    }
    if (cache.device.Get() != device.Get()) {
        cache = {};
        cache.device = device;
    }
    const auto found = std::ranges::find_if(cache.kept, [&](const CardShadowBitmap& k) {
        return k.theme == m_theme && k.scale == m_scale && k.radius == radius && k.layers == layers.data() &&
               k.layerCount == layers.size();
    });
    const CardShadowBitmap* shadow = found != cache.kept.end() ? &*found : nullptr;
    if (!shadow) {
        // Bake: once for this theme / scale / radius.
        if (cache.kept.size() >= kCardShadowsMost) {
            cache.kept.clear();
        }
        CardShadowBitmap baked{m_theme, m_scale, radius, layers.data(), layers.size()};
        float reach = 0; // the blur's reach (3 standard deviations; blurRadius ≈ 2 of them) plus the offset
        bool visible = false;
        for (const auto& layer : layers) {
            reach = std::max(reach, 1.5f * layer.blurRadius + std::max(std::abs(layer.offsetX), std::abs(layer.offsetY)));
            visible = visible || ui::color(m_theme, layer.color).a > 0.0f;
        }
        baked.margin = std::ceil(reach * m_scale) + 1;
        baked.inner = std::ceil((radius + reach) * m_scale) + 1;
        baked.corner = baked.margin + baked.inner;
        const float shapePx = 2 * baked.inner + 1;
        baked.size = shapePx + 2 * baked.margin;
        if (visible && !cache.baker &&
            FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &cache.baker))) {
            cache.baker = nullptr;
        }
        if (visible && cache.baker) {
            ID2D1DeviceContext* baker = cache.baker.Get();
            baker->SetDpi(96.0f, 96.0f); // the bitmap in pixels: 1 DIP = 1 px
            baker->SetTransform(D2D1::Matrix3x2F::Identity());
            const auto size = static_cast<UINT32>(baked.size);
            ComPtr<ID2D1CommandList> shape;
            ComPtr<ID2D1SolidColorBrush> solid;
            if (SUCCEEDED(baker->CreateBitmap({size, size}, nullptr, 0,
                                              D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                                                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                                        D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                                      96.0f, 96.0f),
                                              &baked.bitmap)) &&
                SUCCEEDED(baker->CreateCommandList(&shape)) &&
                SUCCEEDED(baker->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &solid))) {
                baker->SetTarget(shape.Get());
                baker->BeginDraw();
                const float r = radius * m_scale;
                baker->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(baked.margin, baked.margin, baked.margin + shapePx, baked.margin + shapePx), r, r),
                    solid.Get());
                const bool recorded = SUCCEEDED(baker->EndDraw()) && SUCCEEDED(shape->Close());
                baker->SetTarget(baked.bitmap.Get());
                baker->BeginDraw();
                baker->Clear(D2D1::ColorF(0, 0, 0, 0));
                for (const auto& layer : layers) {
                    ComPtr<ID2D1Effect> blur;
                    if (!recorded || FAILED(baker->CreateEffect(CLSID_D2D1Shadow, &blur))) {
                        break;
                    }
                    const Rgba c = ui::color(m_theme, layer.color);
                    blur->SetInput(0, shape.Get());
                    blur->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, layer.blurRadius / 2.0f * m_scale);
                    blur->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(c.r, c.g, c.b, c.a));
                    const D2D1_POINT_2F offset{layer.offsetX * m_scale, layer.offsetY * m_scale};
                    baker->DrawImage(blur.Get(), &offset);
                }
                if (FAILED(baker->EndDraw()) || !recorded) {
                    baked.bitmap = nullptr;
                }
                baker->SetTarget(nullptr);
            } else {
                baked.bitmap = nullptr;
            }
        }
        cache.kept.push_back(std::move(baked));
        shadow = &cache.kept.back();
    }
    if (!shadow->bitmap) {
        return;
    }
    // The card on the pixel grid, the bitmap's pieces round it at one bitmap pixel per screen pixel.
    const RectF card{std::round(rect.x * m_scale), std::round(rect.y * m_scale), 0, 0};
    const RectF dest{card.x - shadow->margin, card.y - shadow->margin,
                     std::round(rect.right() * m_scale) - card.x + 2 * shadow->margin,
                     std::round(rect.bottom() * m_scale) - card.y + 2 * shadow->margin};
    std::array<NineSlice, 8> pieces{};
    if (!nineSlices(shadow->size, shadow->corner, dest, pieces)) {
        dropShadow(rect, radius, layers); // smaller than the corners: blurred as it is
        return;
    }
    const float k = 1.0f / m_scale;
    for (const NineSlice& piece : pieces) {
        const D2D1_RECT_F to = D2D1::RectF(piece.dest.x * k, piece.dest.y * k, piece.dest.right() * k, piece.dest.bottom() * k);
        const D2D1_RECT_F from = toD2D(piece.source);
        m_context->DrawBitmap(shadow->bitmap.Get(), &to, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &from);
    }
}

void Canvas::raisedCard(RectF rect, float radius, Ink fill) {
    const RectF card = snapRect(rect, m_scale);
    if (card.width <= 0 || card.height <= 0) {
        return;
    }
    cardShadow(card, radius);
    fillRoundRect(card, radius, tokens::Color::LineSubtle);
    fillRoundRect(card.inset(px(), px()), std::max(radius - px(), 0.0f), fill);
}

void Canvas::drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, Ink ink, TextAlign align) {
    auto layout = m_text.layout(text, style, rect.width, rect.height, align);
    if (!layout) {
        return;
    }
    m_context->DrawTextLayout({rect.x, rect.y}, layout->Get(), brush(ink), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::drawTextWrapped(std::wstring_view text, RectF rect, tokens::TypeStyle style, Ink ink, TextAlign align) {
    auto layout = m_text.wrappedLayout(text, style, rect.width, align);
    if (!layout) {
        return;
    }
    (*layout)->SetMaxHeight(rect.height);
    m_context->DrawTextLayout({rect.x, rect.y}, layout->Get(), brush(ink), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::drawTextLayout(IDWriteTextLayout* layout, PointF origin, Ink ink, std::span<const InkRange> inks) {
    if (!layout) {
        return;
    }
    // A brush per range, set on the layout for this draw only: the theme (and the device) may be
    // another one at the next frame, so nothing of it is kept between frames.
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushes;
    brushes.reserve(inks.size());
    for (const auto& range : inks) {
        const Rgba c = resolve(range.ink);
        ComPtr<ID2D1SolidColorBrush> b;
        if (SUCCEEDED(m_context->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &b))) {
            layout->SetDrawingEffect(b.Get(), {range.start, range.length});
            brushes.push_back(std::move(b));
        }
    }
    m_context->DrawTextLayout({origin.x, origin.y}, layout, brush(ink), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    for (const auto& range : inks) {
        layout->SetDrawingEffect(nullptr, {range.start, range.length});
    }
}

void Canvas::progressBar(RectF track, float fraction, Ink fill) {
    // D-107 (Painter::Progress): a rounded line-colored track, the accent fill rounded too.
    const float radius = track.height / 2;
    fillRoundRect(track, radius, tokens::Color::LineSubtle);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0) {
        fillRoundRect({track.x, track.y, std::max(std::round(track.width * f), track.height), track.height}, radius, fill);
    }
}

void Canvas::drawIcon(icons::Icon icon, PointF topLeft, Ink ink, IconVariant variant, float size, float rotationDegrees) {
    const auto entry = m_icons.get(icon, variant);
    if (!entry.geometry) {
        return;
    }
    const float target = size > 0 ? size : entry.gridSize;
    const float k = target / entry.gridSize;
    D2D1_MATRIX_3X2_F previous;
    m_context->GetTransform(&previous);
    const float half = entry.gridSize / 2;
    const auto rotation = rotationDegrees != 0 ? D2D1::Matrix3x2F::Rotation(rotationDegrees, {half, half})
                                               : D2D1::Matrix3x2F::Identity();
    m_context->SetTransform(rotation * D2D1::Matrix3x2F::Scale(k, k) *
                            D2D1::Matrix3x2F::Translation(snap(topLeft.x, m_scale), snap(topLeft.y, m_scale)) *
                            previous);
    // D-114: an icon in the accent ink takes its own colour (two tones: a soft fill under the stroke).
    const bool toned = ink.from == tokens::Color::AccentBase && ink.to == tokens::Color::AccentBase && ink.opacity >= 1.0f &&
                       m_theme != ThemeKind::HighContrast && !systemHighContrast();
    if (toned) {
        const IconTone tone = iconTone(icon);
        if (tone.fill && !entry.filled) {
            m_context->FillGeometry(entry.geometry, brush(Ink(tone.color, tone.color, 0.0f, kIconFillOpacity)));
        }
        auto* b = brush(tone.color);
        if (entry.filled) {
            m_context->FillGeometry(entry.geometry, b);
        }
        m_context->DrawGeometry(entry.geometry, b, entry.strokeWidth, m_icons.strokeStyle());
        m_context->SetTransform(previous);
        return;
    }
    auto* b = brush(ink);
    if (entry.filled) {
        m_context->FillGeometry(entry.geometry, b);
    }
    m_context->DrawGeometry(entry.geometry, b, entry.strokeWidth, m_icons.strokeStyle());
    m_context->SetTransform(previous);
}

void Canvas::forgetFileIcons() {
    fileIcons().bitmaps.clear();
}

bool Canvas::drawFileIcon(const std::wstring& file, int index, RectF rect, float opacity) {
    if (file.empty() || rect.width <= 0) {
        return false;
    }
    auto& cache = fileIcons();
    ComPtr<ID2D1Device> device;
    m_context->GetDevice(&device);
    if (cache.device != device.Get()) {
        cache.bitmaps.clear();
        cache.device = device.Get();
    }
    const int pixels = std::clamp(static_cast<int>(std::lround(rect.width * m_scale)), 8, 256);
    const auto key = std::make_tuple(file, index, pixels);
    auto it = cache.bitmaps.find(key);
    if (it == cache.bitmaps.end()) {
        // Bounded (audit D11): far more than one screen shows, so what is on screen is loaded once
        // more at worst; without it a long session over many images only ever grew.
        constexpr std::size_t kMostKept = 2048;
        if (cache.bitmaps.size() >= kMostKept) {
            cache.bitmaps.clear();
        }
        it = cache.bitmaps.emplace(key, loadFileIcon(m_context, file, index, pixels)).first;
    }
    if (!it->second) {
        return false;
    }
    const D2D1_RECT_F target = D2D1::RectF(snap(rect.x, m_scale), snap(rect.y, m_scale), snap(rect.x, m_scale) + rect.width,
                                           snap(rect.y, m_scale) + rect.height);
    m_context->DrawBitmap(it->second.Get(), target, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    return true;
}

void Canvas::pushClip(RectF rect) {
    m_context->PushAxisAlignedClip(toD2D(rect), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::popClip() {
    m_context->PopAxisAlignedClip();
}

void Canvas::pushOpacity(float opacity) {
    D2D1_LAYER_PARAMETERS1 params = D2D1::LayerParameters1();
    params.opacity = opacity;
    m_context->PushLayer(params, nullptr);
}

void Canvas::pushOpacity(float opacity, RectF bounds) {
    D2D1_LAYER_PARAMETERS1 params = D2D1::LayerParameters1();
    params.contentBounds = toD2D(bounds);
    params.opacity = opacity;
    m_context->PushLayer(params, nullptr);
}

void Canvas::popOpacity() {
    m_context->PopLayer();
}

} // namespace wl::ui
