#include "ui/render/OffscreenTarget.h"

#include "base/Hresult.h"

#include <cmath>
#include <cstring>

namespace wl::ui {

namespace {
constexpr auto kCode = ErrorCode::RenderFailure;
}

Result<std::unique_ptr<OffscreenTarget>> OffscreenTarget::create(const RenderDevice& device, SizeF size,
                                                                float scale) {
    auto target = std::make_unique<OffscreenTarget>();
    target->m_device = &device;
    target->m_widthPx = static_cast<UINT>(std::lround(size.width * scale));
    target->m_heightPx = static_cast<UINT>(std::lround(size.height * scale));

    auto context = device.createContext();
    if (!context) {
        return std::unexpected(context.error());
    }
    target->m_context = std::move(*context);

    const float dpi = 96.0f * scale;
    const D2D1_SIZE_U pixels{target->m_widthPx, target->m_heightPx};
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const auto targetProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi);
    WL_TRY_HR(target->m_context->CreateBitmap(pixels, nullptr, 0, &targetProps, &target->m_target), kCode,
              L"creating offscreen target");
    const auto readProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, dpi, dpi);
    WL_TRY_HR(target->m_context->CreateBitmap(pixels, nullptr, 0, &readProps, &target->m_readback), kCode,
              L"creating readback bitmap");
    target->m_context->SetTarget(target->m_target.Get());
    target->m_context->SetDpi(dpi, dpi);
    return target;
}

ID2D1DeviceContext2* OffscreenTarget::beginDraw() {
    m_context->BeginDraw();
    m_context->SetTransform(D2D1::Matrix3x2F::Identity());
    return m_context.Get();
}

Result<void> OffscreenTarget::endDraw() {
    WL_TRY_HR(m_context->EndDraw(), kCode, L"ending offscreen draw");
    WL_TRY_HR(m_readback->CopyFromBitmap(nullptr, m_target.Get(), nullptr), kCode, L"copying to readback");
    return {};
}

Result<std::vector<std::uint32_t>> OffscreenTarget::readPixels() const {
    D2D1_MAPPED_RECT mapped{};
    WL_TRY_HR(m_readback->Map(D2D1_MAP_OPTIONS_READ, &mapped), kCode, L"mapping readback");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(m_widthPx) * m_heightPx);
    for (UINT y = 0; y < m_heightPx; ++y) {
        std::memcpy(&pixels[static_cast<std::size_t>(y) * m_widthPx], mapped.bits + static_cast<std::size_t>(y) * mapped.pitch,
                    static_cast<std::size_t>(m_widthPx) * 4);
    }
    m_readback->Unmap();
    return pixels; // BGRA in memory == 0xAARRGGBB as little-endian uint32
}

Result<void> OffscreenTarget::savePng(const std::filesystem::path& path) const {
    auto pixels = readPixels();
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    IWICImagingFactory* wic = m_device->wic();
    ComPtr<IWICStream> stream;
    WL_TRY_HR(wic->CreateStream(&stream), kCode, L"creating WIC stream");
    WL_TRY_HR(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), ErrorCode::IoError, path.wstring());
    ComPtr<IWICBitmapEncoder> encoder;
    WL_TRY_HR(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), kCode, L"creating PNG encoder");
    WL_TRY_HR(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), kCode, L"initializing encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    WL_TRY_HR(encoder->CreateNewFrame(&frame, nullptr), kCode, L"creating PNG frame");
    WL_TRY_HR(frame->Initialize(nullptr), kCode, L"initializing PNG frame");
    WL_TRY_HR(frame->SetSize(m_widthPx, m_heightPx), kCode, L"sizing PNG frame");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    WL_TRY_HR(frame->SetPixelFormat(&format), kCode, L"setting PNG pixel format");
    const UINT stride = m_widthPx * 4;
    WL_TRY_HR(frame->WritePixels(m_heightPx, stride, stride * m_heightPx,
                                 reinterpret_cast<BYTE*>(pixels->data())),
              kCode, L"writing PNG pixels");
    WL_TRY_HR(frame->Commit(), kCode, L"committing PNG frame");
    WL_TRY_HR(encoder->Commit(), kCode, L"committing PNG");
    return {};
}

} // namespace wl::ui
