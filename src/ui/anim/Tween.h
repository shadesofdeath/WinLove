#pragma once
// Time-based float animation with the design's cubic-bezier easings (05_motion/motion.md).
// Widgets hold Tweens for hover/press/expand amounts and ask the Host for frames while any runs.
#include "ui/generated/Tokens.g.h"

namespace wl::ui {

// Monotonic milliseconds (QueryPerformanceCounter).
[[nodiscard]] double nowMs() noexcept;

// Windows "Show animations" setting off => every duration becomes 0 (motion.md "Reduce motion").
[[nodiscard]] bool reducedMotion() noexcept;
void refreshReducedMotion() noexcept; // call on WM_SETTINGCHANGE
// The app's own "Hareketi azalt": true = always reduced; false = follow the Windows setting.
void setReducedMotionForced(bool forced) noexcept;
// Still-frame rendering (--render): every tween jumps to its target immediately.
void forceInstantMotion(bool instant) noexcept;

// y for x on a CSS-style cubic-bezier(x1, y1, x2, y2).
[[nodiscard]] float evalBezier(const tokens::CubicBezier& curve, float x) noexcept;

class Tween {
public:
    explicit Tween(float initial = 0.0f) noexcept : m_from(initial), m_to(initial), m_value(initial) {}

    // Animate from the current value to `target`. Returns true if a frame is needed.
    bool animateTo(float target, float durationMs, const tokens::CubicBezier& easing = tokens::motion::standard) noexcept;
    void snapTo(float value) noexcept;

    // Advance to `now`; returns true while still running.
    bool tick(double now) noexcept;

    [[nodiscard]] float value() const noexcept { return m_value; }
    [[nodiscard]] float target() const noexcept { return m_to; }
    [[nodiscard]] bool running() const noexcept { return m_running; }

private:
    float m_from;
    float m_to;
    float m_value;
    double m_start = 0;
    float m_duration = 0;
    tokens::CubicBezier m_easing = tokens::motion::standard;
    bool m_running = false;
};

} // namespace wl::ui
