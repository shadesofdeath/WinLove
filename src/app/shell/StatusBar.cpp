#include "app/shell/StatusBar.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;
namespace spacing = ui::tokens::spacing;

namespace {
constexpr float kPadX = 12.0f;               // D-109: the reference tool's text starts ~12 in
constexpr float kSegPadX = spacing::s3;      // a segment's hover area around its text
constexpr float kSegGap = spacing::s4;       // between two segments' areas, the line in its middle
constexpr float kSegInsetY = spacing::s1;    // the hover fill 2 in from the bar's edges
constexpr float kDotGap = spacing::s3;       // the image's dot, then its text
constexpr float kSeparatorInset = spacing::s3; // the line: 10 high in the 22-high bar
constexpr float kTaskGap = spacing::s7;
constexpr float kTaskInner = spacing::s4;    // label, bar and percent apart
constexpr float kBar = 80.0f;
constexpr float kMinText = 40.0f;            // narrower than this a segment is left out, not cut to "…"

ui::Ink faded(Color color, float opacity) {
    return ui::Ink(color, color, 0.0f, opacity);
}
} // namespace

StatusBar::StatusBar() {
    setAccessible(ui::AccessRole::Text, L"");
}

const std::wstring& StatusBar::textOf(std::size_t segment) const {
    switch (static_cast<Segment>(segment)) {
    case Segment::Image: return m_model.image;
    case Segment::Changes: return m_model.changes;
    case Segment::Update: return m_model.update;
    case Segment::Size: break;
    }
    return m_model.size;
}

const std::wstring& StatusBar::tipOf(std::size_t segment) const {
    switch (static_cast<Segment>(segment)) {
    case Segment::Image: return m_model.imageTip;
    case Segment::Changes: return m_model.changesTip;
    case Segment::Update: return m_model.updateTip;
    case Segment::Size: break;
    }
    return m_model.sizeTip;
}

void StatusBar::setModel(Model model) {
    if (model == m_model) {
        return;
    }
    m_model = std::move(model);
    std::wstring name = m_model.image;
    for (const std::wstring* part : {&m_model.changes, &m_model.size, &m_model.update}) {
        if (!part->empty()) {
            name += L", " + *part;
        }
    }
    setAccessible(ui::AccessRole::Text, std::move(name));
    layout();
    invalidate();
}

void StatusBar::setTask(std::optional<std::wstring> label, float fraction) {
    m_task = std::move(label);
    // Below 0: work with no measure of its own (a DISM remount, taking a mount over) — its words alone,
    // not a bar that stays at 0 % (2026-11-11: it read as hung).
    m_taskMeasured = fraction >= 0.0f;
    m_taskFraction = std::clamp(fraction, 0.0f, 1.0f);
    m_taskPercent = m_task && m_taskMeasured ? std::to_wstring(static_cast<int>(m_taskFraction * 100)) + L"%" : std::wstring{};
    layout();
    invalidate();
}

void StatusBar::setInfo(std::vector<std::wstring> info) {
    if (info != m_info) {
        m_info = std::move(info);
        layout();
        invalidate();
    }
}

RectF StatusBar::segmentRect(Segment segment) const {
    return m_placed[static_cast<std::size_t>(segment)].rect;
}

void StatusBar::layout() {
    const RectF b = bounds();
    const float cy = b.y + b.height / 2;

    // Right first: the facts, right to left, never past the middle (the leftmost ones give way).
    m_infoRects.assign(m_info.size(), RectF{});
    m_infoSeparators.clear();
    m_infoRect = {};
    float right = b.right() - kPadX;
    float separator = -1; // the line before the part placed last, drawn once another part is placed
    const float half = b.x + b.width / 2;
    for (std::size_t n = m_info.size(); n-- > 0;) {
        if (m_info[n].empty()) {
            continue;
        }
        const float w = textWidth(m_info[n], TypeStyle::Caption) + 1;
        const float x = std::max(right - w, half);
        if (right - x < kMinText) {
            break;
        }
        if (separator > 0) {
            m_infoSeparators.push_back(separator);
        }
        m_infoRects[n] = {x, b.y, right - x, b.height};
        m_infoRect = m_infoRect.width > 0 ? RectF{x, b.y, m_infoRect.right() - x, b.height} : m_infoRects[n];
        if (x <= half) {
            break; // cut short: nothing more fits
        }
        separator = x - kSegPadX - kSegGap / 2;
        right = x - 2 * kSegPadX - kSegGap;
    }
    const float limit = m_infoRect.width > 0 ? m_infoRect.x - kTaskGap : b.right() - kPadX;

    // The segments, left to right.
    float end = -1; // where the last drawn segment's text ends
    bool full = false;
    for (std::size_t s = 0; s < kSegments; ++s) {
        Placed& placed = m_placed[s];
        placed = {};
        const std::wstring& text = textOf(s);
        if (text.empty() || full) {
            continue;
        }
        const float x = end < 0 ? b.x + kPadX : end + 2 * kSegPadX + kSegGap;
        const float lead = static_cast<Segment>(s) == Segment::Image    ? size::dot + kDotGap
                           : static_cast<Segment>(s) == Segment::Update ? size::icon + kDotGap
                                                                        : 0.0f;
        const float room = limit - x;
        if (room < lead + kMinText) {
            full = true;
            continue;
        }
        const float w = std::min(lead + textWidth(text, TypeStyle::Caption) + 1, room);
        placed.rect = {x - kSegPadX, b.y + kSegInsetY, w + 2 * kSegPadX, std::max(b.height - 2 * kSegInsetY, 0.0f)};
        placed.text = {x + lead, b.y, w - lead, b.height};
        placed.separator = end < 0 ? 0.0f : end + kSegPadX + kSegGap / 2;
        end = x + w;
    }

    // The running task after them.
    m_taskLabel = m_taskBar = m_taskPercentRect = {};
    if (m_task) {
        const float x = end < 0 ? b.x + kPadX : end + kTaskGap;
        const float pct = textWidth(m_taskPercent, TypeStyle::Caption) + 1;
        const float labelW = std::min(textWidth(*m_task, TypeStyle::Caption) + 1,
                                      std::max(limit - x - kBar - pct - 2 * kTaskInner, 0.0f));
        m_taskLabel = {x, b.y, labelW, b.height};
        m_taskBar = {x + labelW + kTaskInner, std::round(cy - size::progress / 2), kBar, size::progress};
        const float px = m_taskBar.right() + kTaskInner;
        m_taskPercentRect = {px, b.y, std::max(std::min(pct, limit - px), 0.0f), b.height};
    }

    if (m_hover >= 0 && m_placed[static_cast<std::size_t>(m_hover)].rect.width <= 0) {
        m_hover = -1; // the segment under the pointer has nothing to say any more
        m_hoverFade.snap(-1);
        setTooltip({});
    }
}

void StatusBar::paint(ui::Canvas& canvas) {
    // D-137: part of the window's frame, as the title bar (was the card's panel color, D-109); no line,
    // text in text.primary.
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgTitleBar);
    const float cy = b.y + b.height / 2;
    const float lineY = b.y + kSeparatorInset;
    const float lineH = std::max(b.height - 2 * kSeparatorInset, 0.0f);

    for (std::size_t s = 0; s < kSegments; ++s) {
        const Placed& placed = m_placed[s];
        if (placed.rect.width <= 0) {
            continue;
        }
        if (placed.separator > 0) {
            canvas.hairlineV(placed.separator, lineY, lineH, Color::LineStrong);
        }
        if (const float hot = m_hoverFade.amount(static_cast<int>(s)); hot > 0) {
            canvas.fillRoundRect(placed.rect, ui::tokens::radius::r2, faded(Color::BgHover, hot));
        }
        if (static_cast<Segment>(s) == Segment::Image) {
            canvas.fillEllipse({placed.text.x - kDotGap - size::dot / 2, cy}, size::dot / 2,
                               m_model.mounted ? Color::AccentBase : Color::TextTertiary);
        }
        const bool update = static_cast<Segment>(s) == Segment::Update;
        if (update) { // D-140: a newer WinLove, in the readable accent like every "will change" word
            canvas.drawIcon(ui::icons::Icon::UpdateDownload, {placed.text.x - kDotGap - size::icon, std::round(cy - size::icon / 2)},
                            Color::AccentText);
        }
        canvas.drawText(textOf(s), placed.text, TypeStyle::Caption, update ? Color::AccentText : Color::TextPrimary);
    }

    if (m_task) {
        canvas.drawText(*m_task, m_taskLabel, TypeStyle::Caption, Color::TextPrimary);
        if (m_taskMeasured) {
            canvas.progressBar(m_taskBar, m_taskFraction);
            canvas.drawText(m_taskPercent, m_taskPercentRect, TypeStyle::Caption, Color::TextPrimary);
        }
    }

    for (const float x : m_infoSeparators) {
        canvas.hairlineV(x, lineY, lineH, Color::LineStrong);
    }
    for (std::size_t i = 0; i < m_info.size(); ++i) {
        if (m_infoRects[i].width > 0) {
            canvas.drawText(m_info[i], m_infoRects[i], TypeStyle::Caption, Color::TextPrimary);
        }
    }
}

bool StatusBar::tick(double now) {
    const bool running = m_hoverFade.tick(now);
    invalidate();
    return running;
}

void StatusBar::hoverTo(int segment) {
    if (segment == m_hover) {
        return;
    }
    m_hover = segment;
    setTooltip(segment >= 0 ? tipOf(static_cast<std::size_t>(segment)) : std::wstring{});
    if (m_hoverFade.set(segment)) {
        animate();
    }
    invalidate();
}

void StatusBar::onPointerMove(ui::PointF p) {
    int hover = -1;
    if (onSegment) {
        for (std::size_t s = 0; s < kSegments; ++s) {
            if (m_placed[s].rect.width > 0 && m_placed[s].rect.contains(p)) {
                hover = static_cast<int>(s);
            }
        }
    }
    hoverTo(hover);
}

void StatusBar::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1);
    }
    invalidate();
}

void StatusBar::onClick() {
    if (m_hover >= 0 && onSegment) {
        auto handler = onSegment; // shows another page
        handler(static_cast<Segment>(m_hover));
    }
}

RectF StatusBar::tooltipAnchor() const {
    return m_hover >= 0 ? m_placed[static_cast<std::size_t>(m_hover)].rect : bounds();
}

ui::Cursor StatusBar::cursor() const {
    return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

} // namespace wl::app
