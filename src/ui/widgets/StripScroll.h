#pragma once
// D-136: a row of tabs wider than its room (the page card's tabs, a page's TabBar — Ayarlar's ten
// categories at 13 px). The row scrolls sideways: the wheel moves it, a chosen tab is always
// brought whole into view (a click, ←/→, setSelected), and an edge that hides tabs fades them out
// under a chevron that scrolls on a click. A strip that fits has no offset, no fade, no chevron —
// it is drawn exactly as before. Logic only: the owner keeps one, tells it the extent when its
// tabs or bounds change, shifts its tab rectangles by offset() and paints the edges last.
#include "ui/render/Canvas.h"

namespace wl::ui {

class StripScroll {
public:
    // The fade over the last visible tabs and the chevron's square at the very edge.
    static constexpr float kFade = tokens::spacing::s6;
    static constexpr float kChevron = tokens::size::icon + tokens::spacing::s2;
    static constexpr float kEdge = kFade + kChevron;
    // What a wheel notch moves the row.
    static constexpr float kWheelStep = 3 * tokens::spacing::s6;

    // The row's full width and the room it has (DIP); the offset is kept within range.
    void setExtent(float content, float view) noexcept;
    [[nodiscard]] float offset() const noexcept { return m_offset; }
    [[nodiscard]] float content() const noexcept { return m_content; }
    [[nodiscard]] float view() const noexcept { return m_view; }
    [[nodiscard]] float maxOffset() const noexcept { return m_content > m_view ? m_content - m_view : 0.0f; }
    [[nodiscard]] bool overflows() const noexcept { return m_content > m_view + 0.5f; }
    [[nodiscard]] bool hidesStart() const noexcept { return overflows() && m_offset > 0.5f; }
    [[nodiscard]] bool hidesEnd() const noexcept { return overflows() && m_offset < maxOffset() - 0.5f; }

    // Moves the row by `delta` (positive: towards its end); true when it moved.
    bool scrollBy(float delta) noexcept;
    bool scrollTo(float offset) noexcept;
    // Brings [x, x + width] (row coordinates: 0 = the row's start) whole into view, clear of the
    // edges that will still hide tabs; true when it moved.
    bool reveal(float x, float width) noexcept;
    // The chevron under `x` (view coordinates: 0 = the strip's left end): -1 the start's, +1 the
    // end's, 0 none.
    [[nodiscard]] int chevronAt(float x) const noexcept;
    // A click on a chevron: a good part of the room towards that end; true when it moved.
    bool page(int direction) noexcept;
    // The fades and chevrons over the strip's edges in `bounds` (the strip lies on `surface`).
    // No allocation: the fade is a few thin fills.
    void paintEdges(Canvas& canvas, RectF bounds, tokens::Color surface, int hotChevron = 0) const;

private:
    float m_content = 0;
    float m_view = 0;
    float m_offset = 0;
};

} // namespace wl::ui
