#include "app/pages/apply/RiskConfirm.h"

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

RiskConfirm::RiskConfirm(std::vector<Item> items, std::wstring ack) : m_items(std::move(items)) {
    m_check = &add<ui::CheckField>(std::move(ack), false);
    m_check->onChange = [this](bool on) {
        if (onAck) {
            onAck(on);
        }
    };
}

void RiskConfirm::layout() {
    const RectF b = bounds();
    m_check->setBounds({b.x, b.y + kRow * static_cast<float>(m_items.size()) + 8, b.width, kRow});
}

void RiskConfirm::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    float y = b.y;
    for (const auto& item : m_items) {
        canvas.hairlineH(b.x, y, b.width, Color::LineSubtle);
        canvas.drawIcon(ui::icons::Icon::ShieldWarning, {b.x, y + 4}, Color::StatusError);
        const float x = b.x + ui::tokens::size::icon + 8;
        canvas.drawText(item.name, {x, y, b.right() - x - 80, kRow}, TypeStyle::Body, Color::TextPrimary);
        canvas.drawText(item.size, {b.right() - 80, y, 80, kRow}, TypeStyle::Mono, Color::TextSecondary,
                        ui::TextAlign::Trailing);
        y += kRow;
    }
    canvas.hairlineH(b.x, y, b.width, Color::LineSubtle);
}

} // namespace wl::app
