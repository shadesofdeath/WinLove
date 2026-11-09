#include "app/shell/PageView.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {
// D-109, measured on the reference tool at 175 %: ribbon buttons start 12 in and sit 2 apart; the card's body
// keeps 8 at the sides and 6 above and below; a separator has 6 on each side.
constexpr float kRibbonX = 12.0f;
constexpr float kActionGap = 2.0f;
constexpr float kSeparatorRoom = 6.0f;
constexpr float kBodyPadX = 8.0f;
constexpr float kBodyPadY = 6.0f;
constexpr float kTabPadX = 7.5f;
constexpr float kTabGap = 2.0f;
constexpr float kHintPadX = 24.0f;
} // namespace

// ---- PageViewport ------------------------------------------------------------------------------

PageViewport::PageViewport() {
    m_bar = &add<ui::ScrollBar>();
    m_bar->setVisible(false);
    m_bar->onScroll = [this](float offset) { setOffset(offset); };
}

void PageViewport::setBody(ui::Widget* body) {
    m_body = body;
    m_offset = 0;
    bringToFront(m_bar);
    layout();
}

void PageViewport::layout() {
    const RectF b = bounds();
    if (!m_body) {
        return;
    }
    // Laid out at the room first: that tells a scrollable body how tall it is at this width.
    m_body->setBounds(b);
    const auto* scrollable = dynamic_cast<const ScrollableBody*>(m_body);
    m_content = scrollable ? scrollable->contentHeight() : 0.0f;
    const bool scrolls = m_content > b.height + 0.5f;
    m_offset = scrolls ? std::clamp(m_offset, 0.0f, m_content - b.height) : 0.0f;
    if (scrolls) {
        m_body->setBounds({b.x, b.y - m_offset, b.width, m_content});
    }
    m_bar->setVisible(scrolls);
    m_bar->setBounds({b.right() - ui::ScrollBar::kWidth + 6, b.y, ui::ScrollBar::kWidth, b.height});
    m_bar->setRange(scrolls ? m_content : b.height, b.height);
    m_bar->setOffset(m_offset);
}

void PageViewport::setOffset(float offset) {
    m_offset = offset;
    layout();
    invalidate();
}

bool PageViewport::onWheel(ui::PointF /*p*/, float lines) {
    if (!m_bar->visible()) {
        return false;
    }
    setOffset(m_offset - lines * 3 * size::tableRow);
    return true;
}

// ---- CardTabs ----------------------------------------------------------------------------------

CardTabs::CardTabs() {
    setFocusable(false);
}

void CardTabs::setTabs(std::vector<std::wstring> labels, int selected) {
    m_labels = std::move(labels);
    m_selected = std::clamp(selected, 0, std::max(static_cast<int>(m_labels.size()) - 1, 0));
    m_hover = -1;
    setFocusable(m_labels.size() > 1);
    setAccessible(ui::AccessRole::TabItem, m_labels.empty() ? std::wstring{} : m_labels[static_cast<std::size_t>(m_selected)]);
    invalidate();
}

std::vector<RectF> CardTabs::plates() const {
    std::vector<RectF> out;
    const RectF b = bounds();
    float x = b.x;
    for (const auto& label : m_labels) {
        const float w = std::ceil(textWidth(label, TypeStyle::Body, 60.0f) + 2 * kTabPadX);
        out.push_back({x, b.y, w, b.height});
        x += w + kTabGap;
    }
    return out;
}

void CardTabs::paint(ui::Canvas& canvas) {
    const auto rects = plates();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const RectF r = rects[i];
        const bool chosen = static_cast<int>(i) == m_selected;
        const bool hot = static_cast<int>(i) == m_hover && m_labels.size() > 1;
        const float radius = ui::tokens::radius::r3;
        if (chosen) {
            // D-114: the chosen tab is the card's own top (its colour, no seam), rounded at the top,
            // with a concave fillet into the card on its right — a tab of the card, not a button on it.
            canvas.pushClip({r.x, r.y, r.width + radius, r.height});
            canvas.fillRoundRect({r.x, r.y, r.width, r.height + radius}, radius, Color::BgPanel);
            canvas.fillRect({r.right(), r.bottom() - radius, radius, radius}, Color::BgPanel);
            canvas.fillEllipse({r.right() + radius, r.bottom() - radius}, radius, Color::BgBase);
            canvas.popClip();
        } else if (hot) {
            canvas.pushClip({r.x, r.y + 3, r.width, r.height - 6});
            canvas.fillRoundRect({r.x, r.y + 3, r.width, r.height - 6}, radius, Color::BgHover);
            canvas.popClip();
        }
        canvas.drawText(m_labels[i], {r.x + kTabPadX, r.y, r.width - kTabPadX, r.height}, TypeStyle::Body,
                        chosen || hot ? Color::TextPrimary : Color::TextSecondary);
    }
}

void CardTabs::onPointerMove(ui::PointF p) {
    int hover = -1;
    const auto rects = plates();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
}

void CardTabs::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hover = -1;
    }
    invalidate();
}

ui::Cursor CardTabs::cursor() const {
    return m_labels.size() > 1 && m_hover >= 0 && m_hover != m_selected ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void CardTabs::onClick() {
    if (m_labels.size() > 1 && m_hover >= 0 && m_hover != m_selected && onPick) {
        auto pick = onPick;
        pick(m_hover);
    }
}

bool CardTabs::onKeyDown(const ui::KeyEvent& key) {
    if (m_labels.size() < 2 || !onPick) {
        return false;
    }
    int next = m_selected;
    if (key.virtualKey == VK_LEFT) {
        next = std::max(m_selected - 1, 0);
    } else if (key.virtualKey == VK_RIGHT) {
        next = std::min(m_selected + 1, static_cast<int>(m_labels.size()) - 1);
    } else {
        return false;
    }
    if (next != m_selected) {
        auto pick = onPick;
        pick(next);
    }
    return true;
}

// ---- PageView ----------------------------------------------------------------------------------

PageView::PageView(std::wstring title, std::wstring description)
    : m_title(std::move(title)), m_description(std::move(description)) {
    m_tabs = &add<CardTabs>();
    m_tabs->onPick = [this](int index) {
        if (auto pick = m_onSub) {
            pick(index);
        }
    };
    m_viewport = &add<PageViewport>();
    updateTabs();
    if (m_enter.animateTo(1.0f, ui::tokens::motion::enterMs, ui::tokens::motion::decelerate)) {
        animate();
    }
    setAccessible(ui::AccessRole::Group, m_title);
}

void PageView::updateTabs() {
    if (m_subLabels.size() > 1) {
        m_tabs->setTabs(m_subLabels, m_subSelected);
    } else {
        m_tabs->setTabs({m_title}, 0);
    }
}

ui::Button& PageView::addAction(ui::ButtonKind /*kind*/, std::wstring label, std::optional<ui::icons::Icon> icon) {
    // Every page action is a ribbon button, as in the reference tool (D-109).
    ui::Button& button = add<ui::Button>(ui::ButtonKind::Ribbon, std::move(label), icon);
    m_actions.push_back(&button);
    layout();
    return button;
}

void PageView::addSeparator() {
    m_separators.push_back(m_actions.size());
    layout();
}

void PageView::setSubNav(std::vector<std::wstring> labels, int selected, std::function<void(int)> onPick) {
    m_subLabels = std::move(labels);
    m_subSelected = selected;
    m_onSub = std::move(onPick);
    updateTabs();
    layout();
    invalidate();
}

void PageView::setHeader(std::wstring title, std::wstring description) {
    if (title == m_title && description == m_description) {
        return;
    }
    m_title = std::move(title);
    m_description = std::move(description);
    updateTabs();
    layout();
    invalidate();
}

void PageView::setColumns(float left, float right) {
    if (left != m_left || right != m_right) {
        m_left = left;
        m_right = right;
        layout();
    }
}

float PageView::contentTop() const {
    return bounds().y + size::ribbon + size::hint;
}

RectF PageView::cardRect() const {
    const RectF b = bounds();
    const float top = contentTop();
    const float x = b.x + std::max(m_left, size::contentPad);
    const float right = b.right() - size::contentPad - (m_right > 0 ? m_right + size::contentPad : 0.0f);
    return {x, top, std::max(right - x, 0.0f), std::max(b.bottom() - size::contentPad - top, 0.0f)};
}

void PageView::layout() {
    const RectF b = bounds();
    // The ribbon: buttons left to right, vertically centered; a separator takes its room.
    float x = b.x + kRibbonX;
    m_separatorX.clear();
    for (std::size_t i = 0; i < m_actions.size(); ++i) {
        if (std::ranges::find(m_separators, i) != m_separators.end() && i > 0) {
            m_separatorX.push_back(x + kSeparatorRoom - kActionGap);
            x += 2 * kSeparatorRoom;
        }
        ui::Button* action = m_actions[i];
        if (!action->visible()) {
            continue;
        }
        const ui::SizeF s = action->measure({});
        action->setBounds({x, b.y + std::round((size::ribbon - s.height) / 2), s.width, s.height});
        x += s.width + kActionGap;
    }
    const RectF card = cardRect();
    const float tabWidth = card.width;
    m_tabs->setBounds({card.x, card.y, tabWidth, size::tab});
    const float top = card.y + size::tab;
    m_viewport->setBounds({card.x + kBodyPadX, top + kBodyPadY, std::max(card.width - 2 * kBodyPadX, 0.0f),
                           std::max(card.bottom() - top - 2 * kBodyPadY, 0.0f)});
}

void PageView::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgBase);
    // The ribbon band and the soft edge under it.
    canvas.fillRect({b.x, b.y, b.width, size::ribbon}, Color::BgChrome);
    canvas.hairlineH(b.x, b.y + size::ribbon, b.width, Color::LineSubtle);
    for (const float sx : m_separatorX) {
        canvas.hairlineV(sx, b.y + 10, size::ribbon - 20, Color::LineStrong);
    }
    // The hint: the page's description, centered.
    if (!m_description.empty()) {
        canvas.drawText(m_description, {b.x + kHintPadX, b.y + size::ribbon, std::max(b.width - 2 * kHintPadX, 0.0f), size::hint},
                        TypeStyle::Body, Color::TextSecondary, ui::TextAlign::Center);
    }
    // The card body under its tabs.
    const RectF card = cardRect();
    canvas.fillRoundRect({card.x, card.y + size::tab, card.width, std::max(card.height - size::tab, 0.0f)},
                         ui::tokens::radius::r3, Color::BgPanel);
    // D-114: the first tab sits on the card's top-left corner: square it so the two are one shape.
    canvas.fillRect({card.x, card.y + size::tab, ui::tokens::radius::r3, ui::tokens::radius::r3}, Color::BgPanel);
}

bool PageView::tick(double now) {
    const bool running = m_enter.tick(now);
    invalidate();
    return running;
}

} // namespace wl::app
