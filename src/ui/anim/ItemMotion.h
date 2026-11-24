#pragma once
// Motion of one item among many inside a single widget (tabs, segments, table rows), so every
// list-like widget fades and slides the same way a Button does (motion tokens, Tween).
// Under reduced / instant motion both jump to their end state at once: a still frame (--render)
// looks exactly as it did before these existed.
#include "ui/Geometry.h"
#include "ui/anim/Tween.h"

namespace wl::ui {

// The hover highlight of the item under the pointer: it fades in while the item the pointer left
// fades out, both in motion.fast like Button's hover overlay.
class ItemHover {
public:
    // The item now under the pointer (-1: none). True while a fade needs frames (Widget::animate()).
    bool set(int index) noexcept;
    // To `index` at once, no fade (rows rebuilt, a list opened on its current item).
    void snap(int index) noexcept;
    // Advances both fades; true while one still runs.
    bool tick(double now) noexcept;
    // How much of the highlight `index` shows: 1 for the hovered item at rest, 0 for the others.
    [[nodiscard]] float amount(int index) const noexcept;
    [[nodiscard]] int index() const noexcept { return m_current; }
    [[nodiscard]] bool running() const noexcept { return m_in.running() || m_out.running(); }

private:
    int m_current = -1;
    int m_previous = -1;
    Tween m_in;  // m_current: 0 -> 1
    Tween m_out; // m_previous: what it had -> 0
};

// A selection indicator moving to another item (a tab's underline, a segment's raised pill):
// where it started, and how far along it is. The rectangle is kept relative to the widget's own
// origin, so a widget scrolled while its indicator moves (a form row) takes it along.
class SelectionMove {
public:
    // The selection left item `from`, whose indicator was at `rect` in a widget at `origin`.
    // True while frames are needed.
    bool start(int from, RectF rect, PointF origin, float durationMs = tokens::motion::baseMs) noexcept;
    void stop() noexcept; // at the end at once (a selection set by code)
    bool tick(double now) noexcept { return m_progress.tick(now); }
    [[nodiscard]] bool moving() const noexcept { return m_progress.running(); }
    // The item the selection left while moving, else -1.
    [[nodiscard]] int from() const noexcept { return moving() ? m_from : -1; }
    // 0 at the start, 1 at the end (and at rest).
    [[nodiscard]] float progress() const noexcept { return m_progress.value(); }
    // Where the indicator is now on its way to `to` (window DIPs); `to` itself at rest.
    [[nodiscard]] RectF at(RectF to, PointF origin) const noexcept;

private:
    int m_from = -1;
    RectF m_fromRect{}; // relative to the widget's origin
    Tween m_progress{1.0f};
};

} // namespace wl::ui
