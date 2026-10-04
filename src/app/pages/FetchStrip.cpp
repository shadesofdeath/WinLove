#include "app/pages/FetchStrip.h"

#include "ui/anim/Tween.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

FetchStrip::FetchStrip(const Localization& strings, std::function<std::optional<Status>()> status) : m_status(std::move(status)) {
    m_stop = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StatusStop), ui::icons::Icon::Stop);
    m_stop->onInvoke = [this] {
        if (onStop) {
            onStop();
        }
    };
}

void FetchStrip::start() {
    m_now = ui::nowMs();
    animate();
}

bool FetchStrip::tick(double now) {
    m_now = now;
    invalidate();
    return visible() && m_status && m_status().has_value();
}

void FetchStrip::layout() {
    const RectF b = bounds();
    const ui::SizeF size = m_stop->measure({});
    m_stop->setBounds({b.right() - 8 - size.width, b.y + (b.height - size.height) / 2, size.width, size.height});
}

void FetchStrip::paint(ui::Canvas& canvas) {
    const auto status = m_status ? m_status() : std::nullopt;
    if (!status) {
        return;
    }
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    const float angle = static_cast<float>(static_cast<int>(m_now / 100.0) % 8) * 45.0f;
    canvas.drawIcon(ui::icons::Icon::Spinner, {b.x + 17, b.y + 12}, Color::AccentBase, ui::IconVariant::Regular16, 0,
                    ui::reducedMotion() ? 0.0f : angle);
    const float x = b.x + 40;
    const float width = std::max(m_stop->bounds().x - x - 16, 0.0f);
    canvas.drawText(status->title, {x, b.y + 10, width, 16}, TypeStyle::BodyStrong, Color::TextPrimary);
    if (status->fraction >= 0) {
        const float fraction = std::clamp(static_cast<float>(status->fraction), 0.0f, 1.0f);
        const float barWidth = std::max(width - 48, 0.0f);
        canvas.progressBar({x, b.y + 34, barWidth, 2}, fraction);
        canvas.drawText(std::to_wstring(static_cast<int>(fraction * 100)) + L"%", {x + barWidth + 8, b.y + 27, 40, 16}, TypeStyle::Mono,
                        Color::TextSecondary, ui::TextAlign::Trailing);
    }
}

} // namespace wl::app
