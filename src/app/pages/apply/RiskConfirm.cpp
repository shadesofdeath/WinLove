#include "app/pages/apply/RiskConfirm.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kCheckGap = 8.0f;
constexpr float kSizeWidth = 80.0f;
} // namespace

RiskConfirm::RiskConfirm(std::vector<Item> items, std::wstring ack) : m_items(std::move(items)) {
    m_check = &add<ui::CheckField>(std::move(ack), false);
    m_check->onChange = [this](bool on) {
        if (onAck) {
            onAck(on);
        }
    };
    m_scroll = &add<ui::ScrollBar>();
    m_scroll->onScroll = [this](float offset) { scrollTo(offset); };
}

ui::RectF RiskConfirm::listRect() const {
    const RectF b = bounds();
    return {b.x, b.y, b.width, std::max(b.height - kCheckGap - kRow, 0.0f)};
}

void RiskConfirm::layout() {
    const RectF b = bounds();
    const RectF list = listRect();
    const float content = kRow * static_cast<float>(m_items.size());
    m_offset = std::clamp(m_offset, 0.0f, std::max(content - list.height, 0.0f));
    m_check->setBounds({b.x, list.bottom() + kCheckGap, b.width, kRow});
    m_scroll->setBounds({list.right() - ui::ScrollBar::kWidth, list.y, ui::ScrollBar::kWidth, list.height});
    m_scroll->setRange(content, list.height);
    m_scroll->setOffset(m_offset);
    m_scroll->setVisible(m_scroll->needed());
}

void RiskConfirm::scrollTo(float offset) {
    const float content = kRow * static_cast<float>(m_items.size());
    offset = std::clamp(offset, 0.0f, std::max(content - listRect().height, 0.0f));
    if (offset != m_offset) {
        m_offset = offset;
        m_scroll->setOffset(m_offset);
        invalidate();
    }
}

bool RiskConfirm::onWheel(ui::PointF /*p*/, float lines) {
    if (!m_scroll->needed()) {
        return false;
    }
    scrollTo(m_offset - lines * kRow);
    return true;
}

void RiskConfirm::paint(ui::Canvas& canvas) {
    const RectF list = listRect();
    // Leave the overlay bar its lane so it never covers the sizes.
    const float right = list.right() - (m_scroll->needed() ? ui::ScrollBar::kWidth : 0.0f);
    canvas.pushClip(list);
    const auto first = static_cast<std::size_t>(m_offset / kRow);
    float y = list.y + kRow * static_cast<float>(first) - m_offset;
    for (std::size_t i = first; i < m_items.size() && y < list.bottom(); ++i, y += kRow) {
        const auto& item = m_items[i];
        canvas.hairlineH(list.x, y, list.width, Color::LineSubtle);
        canvas.drawIcon(ui::icons::Icon::ShieldWarning, {list.x, y + 4}, Color::StatusError);
        const float x = list.x + ui::tokens::size::icon + 8;
        canvas.drawText(item.name, {x, y, right - x - kSizeWidth, kRow}, TypeStyle::Body, Color::TextPrimary);
        canvas.drawText(item.size, {right - kSizeWidth, y, kSizeWidth, kRow}, TypeStyle::Mono, Color::TextSecondary,
                        ui::TextAlign::Trailing);
    }
    canvas.popClip();
    canvas.hairlineH(list.x, std::min(y, list.bottom()), list.width, Color::LineSubtle);
}

} // namespace wl::app
