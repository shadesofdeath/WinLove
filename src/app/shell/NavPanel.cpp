#include "app/shell/NavPanel.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

// Measured on the reference tool at 175 % (D-109), in DIP: the caption text 8 in, the icon at 18, the label at
// 44, the chosen row 23 high and 7 in from the card's sides, 10 between groups, 6 under the last.
constexpr float kCaptionX = 8.0f;
constexpr float kIconX = 18.0f;
constexpr float kLabelX = 44.0f;
constexpr float kCountRight = 14.0f;
constexpr float kFillInset = 7.0f;
constexpr float kFillHeight = 23.0f;
constexpr float kGroupGap = 10.0f;
constexpr float kBottom = 6.0f;
constexpr float kCompactGroupGap = 8.0f;

} // namespace

NavPanel::NavPanel() : m_counts(static_cast<std::size_t>(PageId::Count), 0) {
    setFocusable(true);
    setAccessible(ui::AccessRole::TabItem, L"");
    m_bar = &add<ui::ScrollBar>();
    m_bar->setVisible(false);
    m_bar->onScroll = [this](float offset) { scrollTo(offset); };
}

float NavPanel::maxScroll() const {
    return std::max(preferredHeight() - bounds().height, 0.0f);
}

void NavPanel::scrollTo(float offset) {
    const float next = std::clamp(std::round(offset), 0.0f, maxScroll());
    if (next != m_scroll) {
        m_scroll = next;
        m_bar->setOffset(m_scroll);
        place();
        invalidate();
    }
}

void NavPanel::keepInView(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    const RectF b = bounds();
    const RectF r = m_items[static_cast<std::size_t>(index)].rect;
    if (r.y < b.y) {
        scrollTo(m_scroll - (b.y - r.y) - size::navLabel); // its group caption too
    } else if (r.bottom() > b.bottom()) {
        scrollTo(m_scroll + (r.bottom() - b.bottom()) + kBottom);
    }
}

bool NavPanel::onWheel(ui::PointF p, float lines) {
    if (maxScroll() <= 0) {
        return false;
    }
    scrollTo(m_scroll - lines * size::navItem);
    onPointerMove(p); // no mouse-move follows a wheel: the row under the pointer changed
    return true;
}

void NavPanel::setGroups(std::vector<Group> groups) {
    m_groups = std::move(groups);
    m_hover = -1;
    layout(); // the scroll range follows the rows
    invalidate();
}

void NavPanel::setActive(PageId page) {
    m_active = page;
    if (const int i = indexOf(page); i >= 0) {
        setAccessible(ui::AccessRole::TabItem, itemAt(m_items[static_cast<std::size_t>(i)]).label);
        keepInView(i);
    }
    invalidate();
}

void NavPanel::setCount(PageId page, int n) {
    const auto i = static_cast<std::size_t>(page);
    if (i < m_counts.size() && m_counts[i] != n) {
        m_counts[i] = n;
        invalidate();
    }
}

int NavPanel::count(PageId page) const {
    const auto i = static_cast<std::size_t>(page);
    return i < m_counts.size() ? m_counts[i] : 0;
}

void NavPanel::setCompact(bool compact) {
    if (compact != m_compact) {
        m_compact = compact;
        layout();
        invalidate();
    }
}

float NavPanel::preferredWidth() const {
    return m_compact ? kCompactWidth : size::nav;
}

float NavPanel::preferredHeight() const {
    float h = 0;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        if (g > 0) {
            h += m_compact ? kCompactGroupGap : kGroupGap;
        }
        h += (m_compact ? 4.0f : size::navLabel) + static_cast<float>(m_groups[g].items.size()) * size::navItem;
    }
    return h + kBottom;
}

void NavPanel::place() {
    m_items.clear();
    m_captions.clear();
    const RectF b = bounds();
    float y = b.y - m_scroll;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        if (g > 0) {
            y += m_compact ? kCompactGroupGap : kGroupGap;
        }
        const float caption = m_compact ? 4.0f : size::navLabel;
        m_captions.push_back({{b.x + kCaptionX, y, std::max(b.width - 2 * kCaptionX, 0.0f), caption}, static_cast<int>(g)});
        y += caption;
        for (std::size_t i = 0; i < m_groups[g].items.size(); ++i) {
            m_items.push_back({{b.x, y, b.width, size::navItem}, static_cast<int>(g), static_cast<int>(i)});
            y += size::navItem;
        }
    }
}

void NavPanel::layout() {
    const RectF b = bounds();
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll());
    m_bar->setRange(preferredHeight(), b.height);
    m_bar->setOffset(m_scroll);
    m_bar->setVisible(m_bar->needed());
    m_bar->setBounds({b.right() - ui::ScrollBar::kWidth, b.y + 4, ui::ScrollBar::kWidth, std::max(b.height - 8, 0.0f)});
    place();
    keepInView(indexOf(m_active));
}

const NavPanel::Item& NavPanel::itemAt(const Placed& placed) const {
    return m_groups[static_cast<std::size_t>(placed.group)].items[static_cast<std::size_t>(placed.item)];
}

int NavPanel::indexOf(PageId page) const {
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (itemAt(m_items[i]).page == page) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void NavPanel::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgChrome);
    canvas.pushClip(b); // rows scrolled out of the card are not drawn over the window
    if (!m_compact) {
        for (const auto& caption : m_captions) {
            canvas.drawText(m_groups[static_cast<std::size_t>(caption.group)].caption, caption.rect, TypeStyle::GroupCap,
                            Color::TextTertiary);
        }
    } else {
        for (std::size_t i = 1; i < m_captions.size(); ++i) {
            const RectF c = m_captions[i].rect;
            canvas.hairlineH(b.x + 10, c.y - kCompactGroupGap / 2, b.width - 20, Color::LineStrong);
        }
    }
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const Placed& placed = m_items[i];
        const Item& item = itemAt(placed);
        const RectF r = placed.rect;
        const bool active = item.page == m_active;
        const bool hot = static_cast<int>(i) == m_hover;
        const RectF fill{r.x + kFillInset, r.y + std::round((r.height - kFillHeight) / 2), std::max(r.width - 2 * kFillInset, 0.0f),
                         kFillHeight};
        if (active) {
            canvas.fillRoundRect(fill, ui::tokens::radius::r2, Color::BgPressed);
        } else if (hot) {
            canvas.fillRoundRect(fill, ui::tokens::radius::r2, Color::BgHover);
        }
        const float iconX = m_compact ? r.x + std::round((r.width - size::icon) / 2) : r.x + kIconX;
        canvas.drawIcon(item.icon, {iconX, r.y + std::round((r.height - size::icon) / 2)},
                        item.finish ? Color::StatusSuccess : Color::AccentBase);
        if (m_compact) {
            continue;
        }
        float right = r.right() - kCountRight;
        if (const int n = count(item.page); item.counted && n > 0) {
            const std::wstring text = std::to_wstring(n);
            const float w = std::ceil(canvas.text().measure(text, TypeStyle::Counter)) + 1;
            canvas.drawText(text, {right - w, r.y, w, r.height}, TypeStyle::Counter, Color::TextSecondary);
            right -= w + 8;
        }
        canvas.drawText(item.label, {r.x + kLabelX, r.y, std::max(right - r.x - kLabelX, 0.0f), r.height}, TypeStyle::Body,
                        Color::TextPrimary);
    }
    canvas.popClip();
}

void NavPanel::onPointerMove(ui::PointF p) {
    int hover = -1;
    const bool overBar = m_bar->visible() && m_bar->bounds().contains(p);
    for (std::size_t i = 0; i < m_items.size() && bounds().contains(p) && !overBar; ++i) {
        if (m_items[i].rect.contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        setTooltip(m_compact && hover >= 0 ? itemAt(m_items[static_cast<std::size_t>(hover)]).label : std::wstring{});
        invalidate();
    }
}

void NavPanel::onHoverChanged(bool hovered) {
    if (!hovered && m_hover >= 0) {
        m_hover = -1;
    }
    invalidate();
}

ui::Cursor NavPanel::cursor() const {
    return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void NavPanel::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    const PageId page = itemAt(m_items[static_cast<std::size_t>(index)]).page;
    if (onSelect) {
        auto handler = onSelect; // the shell rebuilds the page (and may rebuild these groups)
        handler(page);
    }
}

void NavPanel::onClick() {
    if (m_hover >= 0) {
        select(m_hover);
    }
}

bool NavPanel::onKeyDown(const ui::KeyEvent& key) {
    const int current = indexOf(m_active);
    const int last = static_cast<int>(m_items.size()) - 1;
    int next = current;
    switch (key.virtualKey) {
    case VK_UP: next = std::max(current - 1, 0); break;
    case VK_DOWN: next = std::min(current + 1, last); break;
    case VK_HOME: next = 0; break;
    case VK_END: next = last; break;
    default: return false;
    }
    if (next != current) {
        select(next);
    }
    return true;
}

RectF NavPanel::focusRect() const {
    const int i = indexOf(m_active);
    if (i < 0) {
        return bounds();
    }
    const RectF r = m_items[static_cast<std::size_t>(i)].rect;
    return {r.x + kFillInset, r.y + std::round((r.height - kFillHeight) / 2), r.width - 2 * kFillInset, kFillHeight};
}

} // namespace wl::app
