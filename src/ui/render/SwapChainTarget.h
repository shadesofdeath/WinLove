#pragma once
// Flip-model swap chain bound to an HWND; the D2D context draws in DIPs (DPI set on the context).
#include "ui/render/RenderDevice.h"

#include <windows.h>

namespace wl::ui {

class SwapChainTarget {
public:
    [[nodiscard]] static Result<std::unique_ptr<SwapChainTarget>> create(const RenderDevice& device, HWND hwnd,
                                                                        UINT widthPx, UINT heightPx, float dpi);

    [[nodiscard]] Result<void> resize(UINT widthPx, UINT heightPx, float dpi);

    // Returns the context with the back buffer bound and BeginDraw called.
    [[nodiscard]] ID2D1DeviceContext2* beginDraw();
    // EndDraw + Present. ErrorCode::RenderFailure with DXGI_ERROR_DEVICE_REMOVED/RESET means the
    // caller must rebuild the RenderDevice and this target.
    [[nodiscard]] Result<void> endDrawAndPresent();

private:
    [[nodiscard]] Result<void> bindBackBuffer();

    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID2D1DeviceContext2> m_context;
    float m_dpi = 96.0f;
};

} // namespace wl::ui
