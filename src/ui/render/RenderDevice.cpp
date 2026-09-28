#include "ui/render/RenderDevice.h"

#include "base/Hresult.h"

namespace wl::ui {

namespace {

HRESULT createD3D(D3D_DRIVER_TYPE type, ComPtr<ID3D11Device>& device) {
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    return D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr,
                             nullptr);
}

} // namespace

Result<std::unique_ptr<RenderDevice>> RenderDevice::create() {
    auto device = std::make_unique<RenderDevice>();
    constexpr auto code = ErrorCode::RenderFailure;

    if (FAILED(createD3D(D3D_DRIVER_TYPE_HARDWARE, device->m_d3d))) {
        WL_TRY_HR(createD3D(D3D_DRIVER_TYPE_WARP, device->m_d3d), code, L"creating D3D11 device");
        device->m_software = true;
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    WL_TRY_HR(device->m_d3d.As(&dxgiDevice), code, L"querying DXGI device");
    ComPtr<IDXGIAdapter> adapter;
    WL_TRY_HR(dxgiDevice->GetAdapter(&adapter), code, L"getting DXGI adapter");
    WL_TRY_HR(adapter->GetParent(IID_PPV_ARGS(&device->m_dxgiFactory)), code, L"getting DXGI factory");

    D2D1_FACTORY_OPTIONS options{};
#ifndef NDEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
    WL_TRY_HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &options,
                                reinterpret_cast<void**>(device->m_d2dFactory.GetAddressOf())),
              code, L"creating D2D factory");
    WL_TRY_HR(device->m_d2dFactory->CreateDevice(dxgiDevice.Get(), &device->m_d2dDevice), code,
              L"creating D2D device");

    WL_TRY_HR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6),
                                  reinterpret_cast<IUnknown**>(device->m_dwrite.GetAddressOf())),
              code, L"creating DirectWrite factory");

    WL_TRY_HR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&device->m_wic)),
              code, L"creating WIC factory (is COM initialized?)");
    return device;
}

Result<ComPtr<ID2D1DeviceContext2>> RenderDevice::createContext() const {
    ComPtr<ID2D1DeviceContext2> context;
    WL_TRY_HR(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context),
              ErrorCode::RenderFailure, L"creating D2D device context");
    // Grayscale AA: no ClearType color fringes on the dark theme (d2d-rendering-guide.md).
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return context;
}

} // namespace wl::ui
