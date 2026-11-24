#include "ui/widgets/ProgressRing.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

// The arc's motion (D-142), in degrees: the head runs kJump ahead in the first half of a cycle, the tail
// follows it in the second half; between the two the arc is kMinSweep (a short dash, never a dot) to
// kMinSweep + kJump long.
constexpr float kJump = 250.0f;
constexpr float kMinSweep = 20.0f;
constexpr float kTop = -90.0f; // 12 o'clock
// The still arc: three quarters, from 12 o'clock — reads as "working" without moving.
constexpr float kStillSweep = 270.0f;

std::optional<double> g_ringTime;

[[nodiscard]] bool ringMoves() noexcept {
    return !g_ringTime && !reducedMotion();
}

} // namespace

RingPose ringPose(double elapsedMs) noexcept {
    const double t = std::max(elapsedMs, 0.0);
    const double cycle = tokens::motion::ringCycleMs;
    const double k = std::floor(t / cycle);
    const float u = static_cast<float>((t - k * cycle) / cycle);
    const float head = evalBezier(tokens::motion::ring, std::clamp(u * 2.0f, 0.0f, 1.0f));
    const float tail = evalBezier(tokens::motion::ring, std::clamp(u * 2.0f - 1.0f, 0.0f, 1.0f));
    // Where the tail is: the jumps of the cycles before, this cycle's part, and the steady turn. Kept below
    // 360 in double, so hours of a job lose no precision.
    const double turn = 360.0 * t / tokens::motion::ringTurnMs;
    const double start = std::fmod(k * kJump + static_cast<double>(kJump * tail) + turn, 360.0);
    return {kTop + static_cast<float>(start), kMinSweep + kJump * (head - tail)};
}

RingPose ringStillPose() noexcept {
    return {kTop, kStillSweep};
}

RingPose ringValuePose(float fraction) noexcept {
    return {kTop, 360.0f * std::clamp(fraction, 0.0f, 1.0f)};
}

void setRingTime(std::optional<double> ms) noexcept {
    g_ringTime = ms;
}

std::optional<double> ringTime() noexcept {
    return g_ringTime;
}

void paintRing(Canvas& canvas, RectF box, RingPose pose, Ink ink) {
    const float scale = canvas.scale();
    const float strokeDip = box.width >= tokens::size::ringLarge ? tokens::size::ringStrokeLarge : tokens::size::ringStroke;
    // Whole physical pixels: the box's corner on the grid, a whole number of pixels across, a whole-pixel stroke.
    const float strokePx = std::max(1.0f, std::round(strokeDip * scale));
    const float diameterPx = std::max(std::round(box.width * scale), strokePx * 2);
    const float x = std::round(box.x * scale);
    const float y = std::round(box.y * scale);
    const PointF center{(x + diameterPx / 2) / scale, (y + diameterPx / 2) / scale};
    const float radius = (diameterPx - strokePx) / 2 / scale;
    const float width = strokePx / scale;
    if (canvas.theme() == ThemeKind::HighContrast) {
        // High contrast: no translucent track — a one-pixel line of the system's line colour, the arc in its
        // own colour and width over it.
        canvas.strokeCircle(center, radius, tokens::Color::LineStrong, 1.0f / scale);
    } else {
        Ink track = ink;
        track.opacity *= tokens::opacity::ringTrack;
        canvas.strokeCircle(center, radius, track, width);
    }
    canvas.strokeArc(center, radius, pose.startDegrees, pose.sweepDegrees, ink, width);
}

RingPose RingClock::pose(double nowMs) const noexcept {
    if (g_ringTime) {
        return ringPose(*g_ringTime);
    }
    if (reducedMotion()) {
        return ringStillPose();
    }
    return ringPose(running() ? nowMs - m_start : 0.0);
}

bool RingClock::turning() const noexcept {
    return running() && ringMoves();
}

float RingClock::tickMs(float otherwise) const noexcept {
    return running() && ringMoves() ? tokens::motion::ringFrameMs : otherwise;
}

// ---- ProgressRing ------------------------------------------------------------------------------

ProgressRing::ProgressRing(float size) : m_size(size) {
    setHitTestVisible(false);
}

SizeF ProgressRing::measure(SizeF /*available*/) {
    return {m_size, m_size};
}

void ProgressRing::setRunning(bool running) {
    if (running == m_running) {
        return;
    }
    m_running = running;
    if (running) {
        m_now = nowMs();
        m_clock.start(m_now);
        if (!m_value && ringMoves()) {
            wake();
        }
    } else {
        m_clock.stop();
    }
    invalidate();
}

void ProgressRing::setValue(std::optional<float> fraction) {
    if (fraction) {
        const float value = std::clamp(*fraction, 0.0f, 1.0f);
        if (!m_value) {
            m_shown.snapTo(value);
        } else if (m_shown.animateTo(value, tokens::motion::baseMs)) {
            wake();
        }
    }
    m_value = fraction;
    invalidate();
}

void ProgressRing::wake() {
    if (!m_turning) {
        m_turning = true;
        m_painted = true; // the first frame after a wake is drawn before it is checked
        animate();
    }
}

bool ProgressRing::tick(double now) {
    m_now = now;
    const bool sliding = m_shown.tick(now);
    const bool turns = m_running && !m_value && ringMoves();
    // Not drawn since the last frame: hidden, or outside the clip of a scrolled page. No more frames
    // until paint() sees it again.
    if ((!sliding && !turns) || !m_painted) {
        m_turning = false;
        return false;
    }
    m_painted = false;
    invalidate();
    return true;
}

float ProgressRing::tickIntervalMs() const {
    return m_shown.running() ? 0.0f : tokens::motion::ringFrameMs;
}

void ProgressRing::paint(Canvas& canvas) {
    m_painted = true;
    if (!m_running && !m_value) {
        return; // idle: the owner shows something else there
    }
    if (!m_turning && (m_shown.running() || (m_running && !m_value && ringMoves()))) {
        wake(); // drawn again after being hidden: go on
    }
    const RingPose pose = m_value ? ringValuePose(m_shown.value()) : m_clock.pose(m_now);
    const RectF b = bounds();
    paintRing(canvas, {b.x, b.y, m_size, m_size}, pose, m_ink);
}

} // namespace wl::ui
