#pragma once
// Renders without a window and saves PNG — used by `WinLove.exe --render-*`, render tests and
// design comparison (docs/TESTING.md "Görsel doğrulama").
#include "ui/Geometry.h"
#include "ui/render/RenderDevice.h"

#include <filesystem>
#include <vector>

namespace wl::ui {

class OffscreenTarget {
public:
    // size in DIPs; pixel size = size * scale.
    [[nodiscard]] static Result<std::unique_ptr<OffscreenTarget>> create(const RenderDevice& device, SizeF size,
                                                                        float scale);

    [[nodiscard]] ID2D1DeviceContext2* beginDraw();
    [[nodiscard]] Result<void> endDraw();

    // Pixels as 0xAARRGGBB, row-major (for tests).
    [[nodiscard]] Result<std::vector<std::uint32_t>> readPixels() const;
    [[nodiscard]] Result<void> savePng(const std::filesystem::path& path) const;

    [[nodiscard]] UINT widthPx() const noexcept { return m_widthPx; }
    [[nodiscard]] UINT heightPx() const noexcept { return m_heightPx; }

private:
    const RenderDevice* m_device = nullptr;
    ComPtr<ID2D1DeviceContext2> m_context;
    ComPtr<ID2D1Bitmap1> m_target;
    ComPtr<ID2D1Bitmap1> m_readback;
    UINT m_widthPx = 0;
    UINT m_heightPx = 0;
};

} // namespace wl::ui
