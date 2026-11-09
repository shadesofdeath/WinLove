#include "app/shell/ImageHeader.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

// D-108: the package's header at the compact scale.
constexpr float kTop = 12.0f;
constexpr float kTitleH = 26.0f;
constexpr float kFactsTop = 30.0f;  // under the name
constexpr float kFactsH = 16.0f;
constexpr float kBadgeH = 18.0f;
constexpr float kBadgePadX = 7.0f;
constexpr float kFactGap = 16.0f;   // between the facts (the package spaces them out)
constexpr float kBarWidth = 150.0f;
constexpr float kBarTop = 34.0f;

} // namespace

ImageHeader::ImageHeader() {
    setAccessible(ui::AccessRole::Text, L"");
}

void ImageHeader::set(Model model) {
    m_model = std::move(model);
    setAccessible(ui::AccessRole::Text, m_model.title);
    invalidate();
}

void ImageHeader::onPointerMove(ui::PointF p) {
    m_pointer = p;
    const bool over = !m_model.link.empty() && m_linkRect.contains(p);
    if (over != m_overLink) {
        m_overLink = over;
        invalidate();
    }
}

void ImageHeader::onHoverChanged(bool hovered) {
    if (!hovered && m_overLink) {
        m_overLink = false;
        invalidate();
    }
}

ui::Cursor ImageHeader::cursor() const {
    return m_overLink ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void ImageHeader::onClick() {
    if (m_overLink && onLink) {
        auto link = onLink;
        link();
    }
}

void ImageHeader::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float pad = size::contentPad;
    const float y = b.y + kTop;
    const auto& text = canvas.text();

    // Right block first: the facts line stops before it.
    float rightLeft = b.right() - pad;
    if (const auto& s = m_model.sizes) {
        const float rx = b.right() - pad;
        if (s->after.empty()) {
            const float w = std::ceil(text.measure(s->before, TypeStyle::Title));
            canvas.drawText(s->before, {rx - w - 1, y + 2, w + 2, 22}, TypeStyle::Title, Color::TextPrimary);
            rightLeft = rx - w;
        } else {
            const float dw = std::ceil(text.measure(s->delta, TypeStyle::BodySemi));
            const float aw = std::ceil(text.measure(s->after, TypeStyle::Title));
            const float bw = std::ceil(text.measure(s->before, TypeStyle::Body));
            float x = rx - dw;
            canvas.drawText(s->delta, {x - 1, y + 4, dw + 2, 20}, TypeStyle::BodySemi, Color::AccentText);
            x -= 10 + aw;
            canvas.drawText(s->after, {x - 1, y + 2, aw + 2, 22}, TypeStyle::Title, Color::TextPrimary);
            x -= 8 + 12;
            canvas.drawIcon(ui::icons::Icon::ArrowRight, {x, y + 7}, Color::TextTertiary, ui::IconVariant::Regular16, 12);
            x -= 8 + bw;
            canvas.drawText(s->before, {x - 1, y + 4, bw + 2, 20}, TypeStyle::Body, Color::TextTertiary);
            rightLeft = x;
        }
        const RectF bar{rx - kBarWidth, y + kBarTop, kBarWidth, size::progress};
        canvas.fillRoundRect(bar, 1.5f, Color::LineSubtle);
        const float fill = std::round(kBarWidth * std::clamp(s->ratio, 0.0f, 1.0f));
        if (fill > 0) {
            canvas.fillRoundRect({bar.x, bar.y, std::max(fill, 4.0f), bar.height}, 1.5f, Color::AccentBase);
        }
        rightLeft = std::min(rightLeft, bar.x);
    }
    const float maxRight = rightLeft - 24;

    // The name, then the badge beside it.
    const float x0 = b.x + pad;
    const float nameRoom = std::max(maxRight - x0 - 120, 80.0f);
    const float tw = std::min(std::ceil(text.measure(m_model.title, TypeStyle::Display)) + 2, nameRoom);
    canvas.drawText(m_model.title, {x0, y, tw, kTitleH}, TypeStyle::Display,
                    m_model.placeholder ? Color::TextTertiary : Color::TextPrimary);
    if (const auto& badge = m_model.badge) {
        const float bw = std::ceil(text.measure(badge->text, TypeStyle::Badge)) + 2 * kBadgePadX;
        const RectF r{x0 + tw + 10, y + std::round((kTitleH - kBadgeH) / 2) + 1, bw, kBadgeH};
        Color fill = Color::BgRaised;
        Color ink = Color::TextSecondary;
        if (badge->kind == BadgeKind::Mounted) {
            fill = Color::AccentSubtle;
            ink = Color::AccentText;
        } else if (badge->kind == BadgeKind::Working) {
            fill = Color::StatusWarningSubtle;
            ink = Color::StatusWarning;
        }
        canvas.fillRoundRect(r, ui::tokens::radius::badge, fill);
        canvas.drawText(badge->text, r, TypeStyle::Badge, ink, ui::TextAlign::Center);
    }

    // Facts: version, architecture, language, the source; then the link.
    float x = x0;
    const float fy = y + kFactsTop;
    m_linkRect = {};
    for (std::size_t i = 0; i < m_model.facts.size() && x < maxRight; ++i) {
        const auto& fact = m_model.facts[i];
        const bool last = i + 1 == m_model.facts.size();
        const float linkRoom = m_model.link.empty() ? 0.0f : std::ceil(text.measure(m_model.link, TypeStyle::SmallSemi)) + kFactGap;
        const float room = std::max(maxRight - x - (last ? linkRoom : 0.0f), 0.0f);
        const float w = std::min(std::ceil(text.measure(fact, TypeStyle::Small)) + 1, room);
        canvas.drawText(fact, {x, fy, w, kFactsH}, TypeStyle::Small, Color::TextSecondary);
        x += w + kFactGap;
    }
    if (!m_model.link.empty() && x < maxRight) {
        const float w = std::ceil(text.measure(m_model.link, TypeStyle::SmallSemi)) + 1;
        m_linkRect = {x - 4, fy - 2, w + 8, kFactsH + 4};
        canvas.drawText(m_model.link, {x, fy, w, kFactsH}, TypeStyle::SmallSemi, Color::AccentText);
        if (m_overLink) {
            canvas.fillRect({x, fy + kFactsH - 2, w - 1, 1.0f}, Color::AccentText);
        }
    }
}

} // namespace wl::app
