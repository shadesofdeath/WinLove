#pragma once
// Basic geometry in DIPs (1 DIP = 1 px at 96 DPI). Physical pixels = DIP * scale.
#include <cmath>

namespace wl::ui {

struct PointF {
    float x = 0;
    float y = 0;
};

struct SizeF {
    float width = 0;
    float height = 0;
};

struct RectF {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    [[nodiscard]] float right() const noexcept { return x + width; }
    [[nodiscard]] float bottom() const noexcept { return y + height; }
    [[nodiscard]] bool contains(PointF p) const noexcept {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    [[nodiscard]] RectF inset(float dx, float dy) const noexcept {
        return {x + dx, y + dy, width - 2 * dx, height - 2 * dy};
    }
    [[nodiscard]] PointF center() const noexcept { return {x + width / 2, y + height / 2}; }
};

// Snaps a DIP coordinate to the physical pixel grid for the given scale.
[[nodiscard]] inline float snap(float dip, float scale) noexcept {
    return std::round(dip * scale) / scale;
}

} // namespace wl::ui
