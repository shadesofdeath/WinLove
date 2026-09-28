#include "ui/anim/Tween.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace wl::ui {

namespace {

std::atomic<int> g_reducedMotion{-1}; // -1 unknown, 0 no, 1 yes
std::atomic<bool> g_instant{false};

float bezierAt(float t, float p1, float p2) {
    // B(t) for P0 = 0, P3 = 1.
    const float u = 1.0f - t;
    return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
}

float bezierSlope(float t, float p1, float p2) {
    const float u = 1.0f - t;
    return 3 * u * u * p1 + 6 * u * t * (p2 - p1) + 3 * t * t * (1 - p2);
}

} // namespace

double nowMs() noexcept {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

void refreshReducedMotion() noexcept {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    g_reducedMotion = animations ? 0 : 1;
}

void forceInstantMotion(bool instant) noexcept {
    g_instant = instant;
}

bool reducedMotion() noexcept {
    if (g_instant.load()) {
        return true;
    }
    if (g_reducedMotion.load() < 0) {
        refreshReducedMotion();
    }
    return g_reducedMotion.load() == 1;
}

float evalBezier(const tokens::CubicBezier& c, float x) noexcept {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    // Solve bezierX(t) = x with Newton's method, falling back to bisection.
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float err = bezierAt(t, c.x1, c.x2) - x;
        if (std::abs(err) < 1e-5f) {
            return bezierAt(t, c.y1, c.y2);
        }
        const float slope = bezierSlope(t, c.x1, c.x2);
        if (std::abs(slope) < 1e-6f) {
            break;
        }
        t -= err / slope;
    }
    float lo = 0.0f;
    float hi = 1.0f;
    t = x;
    for (int i = 0; i < 30; ++i) {
        const float v = bezierAt(t, c.x1, c.x2);
        if (std::abs(v - x) < 1e-5f) break;
        (v < x ? lo : hi) = t;
        t = (lo + hi) / 2;
    }
    return bezierAt(t, c.y1, c.y2);
}

bool Tween::animateTo(float target, float durationMs, const tokens::CubicBezier& easing) noexcept {
    if (target == m_to && (m_running || m_value == target)) {
        return m_running;
    }
    if (durationMs <= 0.0f || reducedMotion()) {
        snapTo(target);
        return false;
    }
    m_from = m_value;
    m_to = target;
    m_easing = easing;
    m_duration = durationMs;
    m_start = nowMs();
    m_running = true;
    return true;
}

void Tween::snapTo(float value) noexcept {
    m_from = m_to = m_value = value;
    m_running = false;
}

bool Tween::tick(double now) noexcept {
    if (!m_running) {
        return false;
    }
    const float progress = static_cast<float>(std::clamp((now - m_start) / m_duration, 0.0, 1.0));
    m_value = m_from + (m_to - m_from) * evalBezier(m_easing, progress);
    if (progress >= 1.0f) {
        m_value = m_to;
        m_running = false;
    }
    return m_running;
}

} // namespace wl::ui
