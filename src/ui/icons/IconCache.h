#pragma once
// Lazily builds and caches one ID2D1PathGeometry per (icon, variant) from the generated table.
// Geometries are device-independent: they survive device loss.
#include "ui/generated/Icons.g.h"
#include "ui/render/RenderDevice.h"

#include <array>

namespace wl::ui {

enum class IconVariant : std::uint8_t { Regular16, Filled16, Regular24 };

class IconCache {
public:
    [[nodiscard]] static Result<std::unique_ptr<IconCache>> create(ID2D1Factory3* factory);

    struct Entry {
        ID2D1PathGeometry* geometry = nullptr;
        float strokeWidth = 0;
        bool filled = false;
        float gridSize = 16; // viewBox size the path data is written in
    };
    // Null geometry if the path failed to parse (logged once by the caller).
    [[nodiscard]] Entry get(icons::Icon icon, IconVariant variant);

    [[nodiscard]] ID2D1StrokeStyle* strokeStyle() const noexcept { return m_stroke.Get(); }

private:
    ComPtr<ID2D1Factory3> m_factory;
    ComPtr<ID2D1StrokeStyle> m_stroke;
    std::array<ComPtr<ID2D1PathGeometry>, icons::kIconCount * 3> m_geometries;
    std::array<bool, icons::kIconCount * 3> m_failed{};
};

} // namespace wl::ui
