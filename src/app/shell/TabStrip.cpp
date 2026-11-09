#include "app/shell/TabStrip.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

constexpr float kCountGap = 5.0f;      // CountGap
constexpr float kUnderline = 2.0f;     // TabUnderlineH
constexpr float kSeparatorShort = 6.0f; // the group line stops above the tabs' bottom
constexpr float kTextFallback = 64.0f;

} // namespace

TabStrip::TabStrip() : m_counts(static_cast<std::size_t>(PageId::Count), 0) {
    setFocusable(true);
    setAccessible(ui::AccessRole::TabItem, L"");
}

void TabStrip::setGroups(std::vector<Group> groups) {
    m_groups = std::move(groups);
    m_hover = -1;
    layout();
    invalidate();
}

void TabStrip::setActive(PageId page) {
    m_active = page;
    for (const auto& group : m_groups) {
        for (const auto& tab : group.tabs) {
            if (tab.page == page) {
                setAccessible(ui::AccessRole::TabItem, tab.label);
            }
        }
    }
    invalidate();
}

void TabStrip::setCount(PageId page, int count) {
    const auto i = static_cast<std::size_t>(page);
    if (i < m_counts.size() && m_counts[i] != count) {
        m_counts[i] = count;
        invalidate(); // a wider count can move the tabs after it
        layout();
    }
}

int TabStrip::count(PageId page) const {
    const auto i = static_cast<std::size_t>(page);
    return i < m_counts.size() ? m_counts[i] : 0;
}

float TabStrip::tabWidth(const Tab& tab) const {
    float w = textWidth(tab.label, TypeStyle::BodySemi, kTextFallback);
    // D-108: a count only when there is something (a row of zeros was noise).
    if (tab.counted && count(tab.page) > 0) {
        w += kCountGap + textWidth(std::to_wstring(count(tab.page)), TypeStyle::Counter, 8.0f);
    }
    return std::ceil(w) + 2;
}

const TabStrip::Tab& TabStrip::tabAt(const Placed& placed) const {
    return m_groups[static_cast<std::size_t>(placed.group)].tabs[static_cast<std::size_t>(placed.tab)];
}

TabStrip::Flow TabStrip::flow(float x0, float y0, float width) const {
    // Groups flow left to right; one that does not fit starts a new row. A group wider than the
    // whole strip breaks between its tabs (the rest continues on the next row, no caption).
    Flow f;
    const float right = x0 + std::max(width, 200.0f);
    float x = x0;
    float y = y0;
    bool rowEmpty = true;
    auto newRow = [&] {
        y += kRowHeight + kRowGap;
        x = x0;
        rowEmpty = true;
    };
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        const auto& group = m_groups[g];
        std::vector<float> widths;
        float groupWidth = 0;
        for (const auto& tab : group.tabs) {
            widths.push_back(tabWidth(tab));
            groupWidth += widths.back() + (widths.size() > 1 ? size::tabGap : 0.0f);
        }
        const float captionWidth = textWidth(group.caption, TypeStyle::GroupCap, 40.0f) + 2;
        groupWidth = std::max(groupWidth, captionWidth);
        const float lead = rowEmpty ? 0.0f : 2 * size::groupGap + 1; // the separator and its room
        if (!rowEmpty && x + lead + groupWidth > right) {
            newRow();
        }
        if (!rowEmpty) {
            const float sx = x + size::groupGap;
            f.separators.push_back({sx, y, 1.0f, kRowHeight - kSeparatorShort});
            x = sx + 1 + size::groupGap;
        }
        f.captions.push_back({{x, y, captionWidth, size::groupCaption}, static_cast<int>(g)});
        for (std::size_t t = 0; t < group.tabs.size(); ++t) {
            if (t > 0) {
                if (x + size::tabGap + widths[t] > right) {
                    newRow(); // the group continues without its caption
                } else {
                    x += size::tabGap;
                }
            }
            f.tabs.push_back({{x, y + size::groupCaption, widths[t], size::tab}, static_cast<int>(g), static_cast<int>(t)});
            x += widths[t];
            rowEmpty = false;
        }
    }
    f.height = m_groups.empty() ? 0.0f : (y - y0) + kRowHeight;
    return f;
}

float TabStrip::heightFor(float width) const {
    return flow(0, 0, width - 2 * size::contentPad).height;
}

void TabStrip::layout() {
    const RectF b = bounds();
    m_flow = flow(b.x + size::contentPad, b.y, b.width - 2 * size::contentPad);
}

int TabStrip::indexOf(PageId page) const {
    for (std::size_t i = 0; i < m_flow.tabs.size(); ++i) {
        if (tabAt(m_flow.tabs[i]).page == page) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TabStrip::paint(ui::Canvas& canvas) {
    for (const auto& caption : m_flow.captions) {
        canvas.drawText(m_groups[static_cast<std::size_t>(caption.group)].caption, caption.rect, TypeStyle::GroupCap,
                        Color::TextTertiary);
    }
    for (const auto& line : m_flow.separators) {
        canvas.hairlineV(line.x, line.y, line.height, Color::LineSubtle);
    }
    for (std::size_t i = 0; i < m_flow.tabs.size(); ++i) {
        const auto& placed = m_flow.tabs[i];
        const Tab& tab = tabAt(placed);
        const bool active = tab.page == m_active;
        const bool hot = static_cast<int>(i) == m_hover;
        const RectF r = placed.rect;
        const TypeStyle style = active ? TypeStyle::BodySemi : TypeStyle::BodyStrong;
        const float lw = std::ceil(canvas.text().measure(tab.label, style)) + 1;
        canvas.drawText(tab.label, {r.x + 1, r.y, lw, r.height}, style, active || hot ? Color::TextPrimary : Color::TextSecondary);
        if (const int n = count(tab.page); tab.counted && n > 0) {
            const std::wstring text = std::to_wstring(n);
            const float x = r.x + 1 + lw + kCountGap - 1;
            canvas.drawText(text, {x, r.y, std::max(r.right() - x + 2, 8.0f), r.height}, TypeStyle::Counter, Color::AccentText);
        }
        if (active) {
            canvas.fillRoundRect({r.x, r.bottom() - kUnderline, r.width, kUnderline}, 1.0f, Color::AccentBase);
        }
    }
}

void TabStrip::onPointerMove(ui::PointF p) {
    int hover = -1;
    for (std::size_t i = 0; i < m_flow.tabs.size(); ++i) {
        // The hit area reaches half way into the gap on each side.
        const RectF r = m_flow.tabs[i].rect;
        if (RectF{r.x - size::tabGap / 2, r.y, r.width + size::tabGap, r.height}.contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        if (hover >= 0) {
            setTooltip({});
        }
        invalidate();
    }
}

void TabStrip::onHoverChanged(bool hovered) {
    if (!hovered && m_hover >= 0) {
        m_hover = -1;
    }
    invalidate();
}

ui::Cursor TabStrip::cursor() const {
    return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void TabStrip::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_flow.tabs.size())) {
        return;
    }
    const PageId page = tabAt(m_flow.tabs[static_cast<std::size_t>(index)]).page;
    if (onSelect) {
        auto handler = onSelect; // the shell rebuilds pages (and may rebuild this strip's groups)
        handler(page);
    }
}

void TabStrip::onClick() {
    if (m_hover >= 0) {
        select(m_hover);
    }
}

bool TabStrip::onKeyDown(const ui::KeyEvent& key) {
    const int current = indexOf(m_active);
    const int last = static_cast<int>(m_flow.tabs.size()) - 1;
    int next = current;
    switch (key.virtualKey) {
    case VK_LEFT: next = std::max(current - 1, 0); break;
    case VK_RIGHT: next = std::min(current + 1, last); break;
    case VK_HOME: next = 0; break;
    case VK_END: next = last; break;
    default: return false;
    }
    if (next != current) {
        select(next);
    }
    return true;
}

RectF TabStrip::focusRect() const {
    const int i = indexOf(m_active);
    if (i < 0) {
        return bounds();
    }
    const RectF r = m_flow.tabs[static_cast<std::size_t>(i)].rect;
    return {r.x - 4, r.y + 6, r.width + 8, r.height - 10};
}

} // namespace wl::app
