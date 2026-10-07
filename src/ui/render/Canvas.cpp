#include "ui/render/Canvas.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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

void Canvas::fillEllipse(PointF center, float radius, Ink ink) {
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), brush(ink));
}

void Canvas::fillEllipse(PointF center, float radius, Rgba color) {
    m_brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), m_brush.Get());
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

void Canvas::progressBar(RectF track, float fraction, Ink fill) {
    fillRect(track, tokens::Color::LineStrong);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0) {
        fillRect({track.x, track.y, std::round(track.width * f), track.height}, fill);
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

void Canvas::popOpacity() {
    m_context->PopLayer();
}

} // namespace wl::ui
