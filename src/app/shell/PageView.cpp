#include "app/shell/PageView.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {
// D-109, measured on the reference tool at 175 %: ribbon buttons start 12 in and sit 2 apart; a separator
// has 6 on each side. D-136: the card's body keeps the panel padding at the sides, a spacing step
// above and below.
constexpr float kRibbonX = 12.0f;
constexpr float kActionGap = 2.0f;
constexpr float kSeparatorRoom = 6.0f;
constexpr float kBodyPadX = size::panelPad;
constexpr float kBodyPadY = ui::tokens::spacing::s4;
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
    m_hoverFade.snap(-1);
    setFocusable(m_labels.size() > 1);
    setAccessible(ui::AccessRole::TabItem, m_labels.empty() ? std::wstring{} : m_labels[static_cast<std::size_t>(m_selected)]);
    layout();
    invalidate();
}

void CardTabs::place() {
    m_plates.clear();
    const RectF b = bounds();
    float x = b.x - m_strip.offset();
    for (const auto& label : m_labels) {
        const float w = std::ceil(textWidth(label, TypeStyle::Body, 60.0f) + 2 * kTabPadX);
        m_plates.push_back({x, b.y, w, b.height});
        x += w + kTabGap;
    }
}

void CardTabs::layout() {
    // The plates at the row's start, then the scroll that keeps the chosen one whole in view.
    m_strip.scrollTo(0);
    place();
    const RectF b = bounds();
    const float content = m_plates.empty() ? 0.0f : m_plates.back().right() - m_plates.front().x;
    m_strip.setExtent(content, b.width);
    if (m_selected >= 0 && m_selected < static_cast<int>(m_plates.size())) {
        const RectF r = m_plates[static_cast<std::size_t>(m_selected)];
        m_strip.reveal(r.x - b.x, r.width);
    }
    place();
}

bool CardTabs::onWheel(ui::PointF p, float lines) {
    if (!m_strip.overflows()) {
        return false;
    }
    if (m_strip.scrollBy(-lines * ui::StripScroll::kWheelStep)) {
        place();
        invalidate();
    }
    onPointerMove(p);
    return true;
}

void CardTabs::onPointerDown(ui::PointF p) {
    const RectF b = bounds();
    const int chevron = m_strip.chevronAt(p.x - b.x);
    m_onChevron = chevron != 0;
    if (m_onChevron && m_strip.page(chevron)) {
        place();
        onPointerMove(p);
        invalidate();
    }
}

void CardTabs::paint(ui::Canvas& canvas) {
    const std::size_t n = std::min(m_plates.size(), m_labels.size());
    const float scale = canvas.scale();
    const float px = 1.0f / scale;
    const RectF b = bounds();
    const bool clipped = m_strip.overflows();
    if (clipped) {
        // Down to the card's top rim: the chosen plate opens it under itself.
        canvas.pushClip({b.x, b.y, b.width, b.height + px});
    }
    for (std::size_t i = 0; i < n; ++i) {
        const RectF r = m_plates[i];
        const bool chosen = static_cast<int>(i) == m_selected;
        const float hot = m_labels.size() > 1 && !chosen ? m_hoverFade.amount(static_cast<int>(i)) : 0.0f;
        const float radius = ui::tokens::radius::r3;
        if (chosen) {
            // D-114: the chosen tab is the card's own top (its colour, no seam), rounded at the top,
            // with a concave fillet into the card on its right — a tab of the card, not a button on it.
            // D-136: the card's rim (PageView) runs round it: up its left side, over its top, down its
            // right side into the fillet's curve; the rim under it is opened.
            const RectF plate = ui::snapRect(r, scale);
            const ui::PointF fillet{plate.right() + radius, plate.bottom() - radius};
            canvas.pushClip({plate.x, plate.y, plate.width + radius, plate.height + px});
            canvas.fillRoundRect({plate.x, plate.y, plate.width, plate.height + radius}, radius, Color::LineSubtle);
            canvas.fillRoundRect({plate.x + px, plate.y + px, plate.width - 2 * px, plate.height + radius}, radius - px, Color::BgPanel);
            canvas.popClip();
            canvas.pushClip({plate.right() - px, fillet.y, radius + px, radius + px});
            canvas.fillRect({plate.right() - px, fillet.y, radius + px, radius + px}, Color::BgPanel);
            canvas.fillEllipse(fillet, radius + px, Color::LineSubtle);
            canvas.fillEllipse(fillet, radius, Color::BgBase);
            canvas.popClip();
        } else if (hot > 0) {
            canvas.pushClip({r.x, r.y + 3, r.width, r.height - 6});
            canvas.fillRoundRect({r.x, r.y + 3, r.width, r.height - 6}, radius, ui::Ink(Color::BgHover, Color::BgHover, 0.0f, hot));
            canvas.popClip();
        }
        // The plain colour at either end: a still frame is drawn as before the fade existed.
        const ui::Ink ink = chosen || hot >= 1.0f ? ui::Ink(Color::TextPrimary)
                            : hot <= 0.0f         ? ui::Ink(Color::TextSecondary)
                                                  : ui::Ink(Color::TextSecondary, Color::TextPrimary, hot);
        canvas.drawText(m_labels[i], {r.x + kTabPadX, r.y, r.width - kTabPadX, r.height}, TypeStyle::Body, ink);
    }
    if (clipped) {
        canvas.popClip();
        m_strip.paintEdges(canvas, b, Color::BgBase, m_hotChevron);
    }
}

bool CardTabs::tick(double now) {
    const bool running = m_hoverFade.tick(now);
    invalidate();
    return running;
}

void CardTabs::hoverTo(int index) {
    if (index == m_hover) {
        return;
    }
    m_hover = index;
    // The chosen plate shows no hover: it fades nothing in.
    if (m_hoverFade.set(index == m_selected ? -1 : index)) {
        animate();
    }
    invalidate();
}

void CardTabs::onPointerMove(ui::PointF p) {
    int hover = -1;
    const RectF b = bounds();
    const int chevron = b.contains(p) ? m_strip.chevronAt(p.x - b.x) : 0;
    if (chevron != m_hotChevron) {
        m_hotChevron = chevron;
        invalidate();
    }
    for (std::size_t i = 0; i < m_plates.size() && chevron == 0 && b.contains(p); ++i) {
        if (m_plates[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    hoverTo(hover);
}

void CardTabs::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1);
        m_hotChevron = 0;
    }
    invalidate();
}

ui::Cursor CardTabs::cursor() const {
    return m_hotChevron != 0 || (m_labels.size() > 1 && m_hover >= 0 && m_hover != m_selected) ? ui::Cursor::Hand
                                                                                                : ui::Cursor::Arrow;
}

void CardTabs::onClick() {
    if (std::exchange(m_onChevron, false)) {
        return; // the chevron scrolled on the press
    }
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
    m_ribbonShown = std::ranges::any_of(m_actions, [](const ui::Button* action) { return action->visible(); });
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
    // The ribbon band and the soft edge under it. A page with no action at all (Hakkında, Uygula
    // with nothing mounted) has no band — an empty one read as a toolbar that failed to load
    // (2026-11-10) — and its hint sits in the middle of the room the two share. The room stays:
    // the nav card and the page card start at the same height on every page.
    if (m_ribbonShown) {
        canvas.fillRect({b.x, b.y, b.width, size::ribbon}, Color::BgChrome);
        canvas.hairlineH(b.x, b.y + size::ribbon, b.width, Color::LineSubtle);
    }
    for (const float sx : m_separatorX) {
        canvas.hairlineV(sx, b.y + 10, size::ribbon - 20, Color::LineStrong);
    }
    // The hint: the page's description, centered.
    if (!m_description.empty()) {
        const float top = m_ribbonShown ? b.y + size::ribbon : b.y;
        const float height = m_ribbonShown ? size::hint : size::ribbon + size::hint;
        canvas.drawText(m_description, {b.x + kHintPadX, top, std::max(b.width - 2 * kHintPadX, 0.0f), height}, TypeStyle::Body,
                        Color::TextSecondary, ui::TextAlign::Center);
    }
    // The card body under its tabs: raised (D-136) — its shadow, a rim of line.subtle one physical
    // pixel inside the edge, the panel fill. D-114: the first tab sits on the card's top-left corner:
    // that corner is square so the two are one shape (the chosen tab draws its own rim, CardTabs).
    const RectF card = cardRect();
    const RectF body = ui::snapRect({card.x, card.y + size::tab, card.width, std::max(card.height - size::tab, 0.0f)}, canvas.scale());
    if (body.width <= 0 || body.height <= 0) {
        return;
    }
    const float radius = ui::tokens::radius::r3;
    const float px = 1.0f / canvas.scale();
    canvas.cardShadow(body, radius);
    canvas.fillRoundRect(body, radius, Color::LineSubtle);
    canvas.fillRect({body.x, body.y, radius, radius}, Color::LineSubtle);
    const RectF inner = body.inset(px, px);
    canvas.fillRoundRect(inner, radius - px, Color::BgPanel);
    canvas.fillRect({inner.x, inner.y, radius, radius}, Color::BgPanel);
}

bool PageView::tick(double now) {
    const bool running = m_enter.tick(now);
    invalidate();
    return running;
}

} // namespace wl::app
