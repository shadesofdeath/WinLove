#include "ui/render/SwapChainTarget.h"

#include "base/Hresult.h"

#include <algorithm>

namespace wl::ui {

namespace {
constexpr auto kCode = ErrorCode::RenderFailure;
constexpr UINT kBufferCount = 2;
} // namespace

Result<std::unique_ptr<SwapChainTarget>> SwapChainTarget::create(const RenderDevice& device, HWND hwnd,
                                                                UINT widthPx, UINT heightPx, float dpi) {
    auto target = std::make_unique<SwapChainTarget>();
    target->m_dpi = dpi;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = std::max(widthPx, 1u);
    desc.Height = std::max(heightPx, 1u);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; // opaque window, no DWM backdrop (D-001)
    WL_TRY_HR(device.dxgiFactory()->CreateSwapChainForHwnd(device.d3d(), hwnd, &desc, nullptr, nullptr,
                                                           &target->m_swapChain),
              kCode, L"creating swap chain");
    // Alt+Enter fullscreen makes no sense for a tool window.
    device.dxgiFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    auto context = device.createContext();
    if (!context) {
        return std::unexpected(context.error());
    }
    target->m_context = std::move(*context);
    if (auto bound = target->bindBackBuffer(); !bound) {
        return std::unexpected(bound.error());
    }
    return target;
}

Result<void> SwapChainTarget::bindBackBuffer() {
    ComPtr<IDXGISurface> surface;
    WL_TRY_HR(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)), kCode, L"getting back buffer");
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), m_dpi, m_dpi);
    ComPtr<ID2D1Bitmap1> bitmap;
    WL_TRY_HR(m_context->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap), kCode,
              L"wrapping back buffer");
    m_context->SetTarget(bitmap.Get());
    m_context->SetDpi(m_dpi, m_dpi);
    return {};
}

Result<void> SwapChainTarget::resize(UINT widthPx, UINT heightPx, float dpi) {
    m_dpi = dpi;
    m_context->SetTarget(nullptr); // the swap chain cannot resize while its buffers are referenced
    WL_TRY_HR(m_swapChain->ResizeBuffers(0, std::max(widthPx, 1u), std::max(heightPx, 1u),
                                         DXGI_FORMAT_UNKNOWN, 0),
              kCode, L"resizing swap chain");
    return bindBackBuffer();
}

ID2D1DeviceContext2* SwapChainTarget::beginDraw() {
    m_context->BeginDraw();
    m_context->SetTransform(D2D1::Matrix3x2F::Identity());
    return m_context.Get();
}

Result<void> SwapChainTarget::endDrawAndPresent() {
    WL_TRY_HR(m_context->EndDraw(), kCode, L"ending draw");
    WL_TRY_HR(m_swapChain->Present(1, 0), kCode, L"presenting");
    return {};
}

} // namespace wl::ui
