#pragma once
// Process-wide graphics objects: D3D11 device, D2D factory/device, DirectWrite factory, WIC.
// Device-dependent objects must be recreated when a target reports device loss.
#include "base/Result.h"

#include <d2d1_3.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <memory>

namespace wl::ui {

using Microsoft::WRL::ComPtr;

class RenderDevice {
public:
    // Hardware device, falling back to WARP (software) when no GPU is usable (VMs, CI, RDP).
    [[nodiscard]] static Result<std::unique_ptr<RenderDevice>> create();

    [[nodiscard]] ID3D11Device* d3d() const noexcept { return m_d3d.Get(); }
    [[nodiscard]] IDXGIFactory2* dxgiFactory() const noexcept { return m_dxgiFactory.Get(); }
    [[nodiscard]] ID2D1Factory3* d2dFactory() const noexcept { return m_d2dFactory.Get(); }
    [[nodiscard]] ID2D1Device2* d2dDevice() const noexcept { return m_d2dDevice.Get(); }
    [[nodiscard]] IDWriteFactory6* dwrite() const noexcept { return m_dwrite.Get(); }
    [[nodiscard]] IWICImagingFactory* wic() const noexcept { return m_wic.Get(); }
    [[nodiscard]] bool isSoftware() const noexcept { return m_software; }

    [[nodiscard]] Result<ComPtr<ID2D1DeviceContext2>> createContext() const;

private:
    ComPtr<ID3D11Device> m_d3d;
    ComPtr<IDXGIFactory2> m_dxgiFactory;
    ComPtr<ID2D1Factory3> m_d2dFactory;
    ComPtr<ID2D1Device2> m_d2dDevice;
    ComPtr<IDWriteFactory6> m_dwrite;
    ComPtr<IWICImagingFactory> m_wic;
    bool m_software = false;
};

} // namespace wl::ui
