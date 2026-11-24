#pragma once
// D-142: WinLove's progress ring, the one indicator of work going on (job strips, the Apply card and step
// list, the image overview, ISO Oluştur). Custom drawn: an arc in accent.text with round caps over a faint
// full track of the same ink (tokens::opacity::ringTrack; high contrast: a one-pixel line.strong track).
// - Indeterminate: the arc's head runs ahead, then its tail catches up (motion.ring easing, one
//   motion.ringCycleMs grow-and-shrink), while the whole ring turns once every motion.ringTurnMs. The pose
//   is a function of the time since the ring appeared, so a late frame never makes it jump.
// - Determinate: the arc is the fraction, from 12 o'clock clockwise.
// Frames: motion.ringFrameMs apart while a ring turns and is drawn, none otherwise. Windows "show
// animations" off or the app's "Hareketi azalt": a still arc and no frames. --render: the still arc, or the
// pose at --anim-time=<ms> (setRingTime), so offscreen frames are deterministic.
// Sizes: tokens::size::ring (16, rows and inline) and ringLarge (20, strips and cards).
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <optional>

namespace wl::ui {

// Degrees, 0 = 3 o'clock, clockwise (Direct2D's sense).
struct RingPose {
    float startDegrees = 0;
    float sweepDegrees = 0;
};

// The indeterminate ring `elapsedMs` after it appeared.
[[nodiscard]] RingPose ringPose(double elapsedMs) noexcept;
// What a ring shows when it must not move (reduced motion, a still render).
[[nodiscard]] RingPose ringStillPose() noexcept;
// A determinate ring at `fraction` (clamped to 0..1).
[[nodiscard]] RingPose ringValuePose(float fraction) noexcept;

// --render / --anim-time: every indeterminate ring shows ringPose(ms); nullopt (the default) leaves rings to
// the clock (and to the still pose when motion is reduced, as in every render).
void setRingTime(std::optional<double> ms) noexcept;
[[nodiscard]] std::optional<double> ringTime() noexcept;

// Draws a ring of `box.width` (a square box) in `pose`. The box is snapped to the pixel grid and the stroke
// is whole physical pixels: crisp at every scale.
void paintRing(Canvas& canvas, RectF box, RingPose pose, Ink ink = tokens::Color::AccentText);
// The square box of a ring of `size` centred on `center` (where a 16 DIP icon used to be: its centre).
[[nodiscard]] inline RectF ringBox(PointF center, float size) noexcept {
    return {center.x - size / 2, center.y - size / 2, size, size};
}

// For a widget that paints a ring among its own drawing (a strip, a card): when the ring appeared and how
// often that widget must tick while it shows one.
class RingClock {
public:
    // The first call sets the phase; later ones keep it (a second job on the same strip does not restart it).
    void start(double nowMs) noexcept {
        if (m_start < 0) {
            m_start = nowMs;
        }
    }
    void stop() noexcept { m_start = -1; }
    [[nodiscard]] bool running() const noexcept { return m_start >= 0; }
    // Running and allowed to move (not reduced motion, no fixed render time): the owner must tick.
    [[nodiscard]] bool turning() const noexcept;
    // The pose at `nowMs` (the widget's last tick): the still pose when motion is reduced, --anim-time's.
    [[nodiscard]] RingPose pose(double nowMs) const noexcept;
    // The widget's tick interval while the ring shows: motion.ringFrameMs while it turns, else `otherwise`
    // (the widget's own pace — an ETA once a second).
    [[nodiscard]] float tickMs(float otherwise) const noexcept;

private:
    double m_start = -1;
};

// A ring as a widget of its own (size × size): turns while running, visible and drawn — a ring in a page
// that is hidden or scrolled out of its clip stops asking for frames and starts again when it is drawn.
class ProgressRing : public Widget {
public:
    explicit ProgressRing(float size = tokens::size::ring);

    void setRunning(bool running);
    [[nodiscard]] bool running() const noexcept { return m_running; }
    // A known fraction (determinate; a change slides over motion.base); nullopt: indeterminate.
    void setValue(std::optional<float> fraction);
    void setInk(Ink ink) {
        m_ink = ink;
        invalidate();
    }
    // Whether it asks the host for frames right now (tests).
    [[nodiscard]] bool turning() const noexcept { return m_turning; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    bool tick(double now) override;
    [[nodiscard]] float tickIntervalMs() const override;

private:
    void wake();

    float m_size;
    Ink m_ink = tokens::Color::AccentText;
    bool m_running = false;
    std::optional<float> m_value;
    Tween m_shown; // the determinate arc as drawn (slides to m_value)
    RingClock m_clock;
    double m_now = 0;
    bool m_painted = false; // drawn since the last tick
    bool m_turning = false; // registered for ticks
};

} // namespace wl::ui
