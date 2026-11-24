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
    m_ring.start(m_now);
    animate();
}

bool FetchStrip::tick(double now) {
    m_now = now;
    invalidate();
    const bool on = visible() && m_status && m_status().has_value();
    if (!on) {
        m_ring.stop();
    }
    return on;
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
    // D-142: the progress ring, centred where the 16 DIP spinner icon was.
    ui::paintRing(canvas, ui::ringBox({b.x + 25, b.y + 20}, ui::tokens::size::ringLarge), m_ring.pose(m_now));
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
