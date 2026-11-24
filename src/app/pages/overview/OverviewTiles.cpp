#include "app/pages/overview/OverviewTiles.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {
constexpr float kPressReleaseMs = 140.0f; // as Button's
constexpr float kTilePad = ui::tokens::spacing::s5;
constexpr float kBadgeHeight = size::icon;
constexpr float kBadgePadX = ui::tokens::spacing::s2;
constexpr float kChipPadX = ui::tokens::spacing::s4 + 2;
constexpr float kChipGap = ui::tokens::spacing::s3;
} // namespace

// ---- OverviewLink ---------------------------------------------------------------------------

OverviewLink::OverviewLink() {
    setFocusable(true);
}

void OverviewLink::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, ui::tokens::motion::fastMs)) {
        animate();
    }
    invalidate();
}

void OverviewLink::onPressedChanged(bool pressed) {
    if (m_press.animateTo(pressed ? 1.0f : 0.0f, pressed ? ui::tokens::motion::fastMs : kPressReleaseMs)) {
        animate();
    }
    invalidate();
}

void OverviewLink::onClick() {
    if (!enabled()) {
        return;
    }
    // A copy: the handler leaves the page, and this widget with it.
    if (auto invoke = onInvoke) {
        invoke();
    }
}

bool OverviewLink::onKeyDown(const ui::KeyEvent& key) {
    return activateOnKey(key);
}

bool OverviewLink::tick(double now) {
    const bool hover = m_hover.tick(now);
    const bool press = m_press.tick(now);
    invalidate();
    return hover || press;
}

float OverviewLink::hot() const {
    // Forced gallery / render states win over the idle tweens, as on a button.
    const float h = hovered() ? std::max(m_hover.value(), m_hover.running() ? 0.0f : 1.0f) : m_hover.value();
    const float p = pressed() ? std::max(m_press.value(), m_press.running() ? 0.0f : 1.0f) : m_press.value();
    return std::min(1.0f, h + p);
}

void OverviewLink::paintHot(ui::Canvas& canvas, float radius) const {
    if (const float h = hot(); h > 0) {
        canvas.fillRoundRect(bounds(), radius, ui::Ink(Color::BgHover, Color::BgHover, 0, h));
    }
}

// ---- OverviewTile ---------------------------------------------------------------------------

void OverviewTile::set(Texts texts) {
    const bool same = texts.label == m_texts.label && texts.value == m_texts.value && texts.detail == m_texts.detail &&
                      texts.badge == m_texts.badge && texts.quiet == m_texts.quiet && texts.icon == m_texts.icon;
    if (same) {
        return;
    }
    m_texts = std::move(texts);
    setAccessible(ui::AccessRole::Button, m_texts.label + L": " + m_texts.value + (m_texts.detail.empty() ? L"" : L" " + m_texts.detail));
    layout();
    invalidate();
}

void OverviewTile::layout() {
    // The widths paint() places by (measured here: the texts change with the state, not per frame).
    m_valueWidth = std::ceil(textWidth(m_texts.value, m_texts.quiet ? TypeStyle::Body : TypeStyle::Title));
    m_badgeWidth = m_texts.badge.empty() ? 0.0f : std::max(kBadgeHeight, std::ceil(textWidth(m_texts.badge, TypeStyle::Badge)) + 2 * kBadgePadX);
    // A narrow window cuts the name or the quiet line to "…": the tooltip then says it whole (D-136's rule).
    if (host() && bounds().width > 0) {
        const float inner = bounds().width - 2 * kTilePad;
        const float labelRoom = inner - size::icon - ui::tokens::spacing::s4 - (m_badgeWidth > 0 ? m_badgeWidth + ui::tokens::spacing::s4 : 0.0f);
        const float detailRoom = inner - m_valueWidth - ui::tokens::spacing::s4;
        const bool cut = textWidth(m_texts.label, TypeStyle::Body) > labelRoom ||
                         (!m_texts.detail.empty() && textWidth(m_texts.detail, TypeStyle::Caption) > detailRoom);
        setTooltip(cut ? m_texts.label + L": " + m_texts.value + (m_texts.detail.empty() ? L"" : L" " + m_texts.detail) : std::wstring());
    }
}

float OverviewTile::focusRadius() const {
    return ui::tokens::radius::r3;
}

void OverviewTile::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    paintHot(canvas, ui::tokens::radius::r3);
    const float x = b.x + kTilePad;
    const float right = b.right() - kTilePad;
    const float topY = b.y + kTilePad;
    canvas.drawIcon(m_texts.icon, {x, topY + 1}, Color::AccentBase);
    float labelRight = right;
    if (m_badgeWidth > 0) {
        const RectF badge{right - m_badgeWidth, topY + 1, m_badgeWidth, kBadgeHeight};
        canvas.fillRoundRect(badge, ui::tokens::radius::badge, Color::AccentSubtle);
        canvas.drawText(m_texts.badge, badge, TypeStyle::Badge, Color::AccentText, ui::TextAlign::Center);
        labelRight = badge.x - ui::tokens::spacing::s4;
    }
    const float labelX = x + size::icon + ui::tokens::spacing::s4;
    canvas.drawText(m_texts.label, {labelX, topY - 1, std::max(labelRight - labelX, 0.0f), 20}, TypeStyle::Body, Color::TextSecondary);
    // The count (or the list's state), then the quiet line after it on the same baseline.
    const float valueY = b.bottom() - kTilePad - 22;
    const float valueWidth = std::min(m_valueWidth + 1, right - x);
    canvas.drawText(m_texts.value, {x, valueY, valueWidth, 22}, m_texts.quiet ? TypeStyle::Body : TypeStyle::Title,
                    m_texts.quiet ? Color::TextTertiary : Color::TextPrimary);
    if (!m_texts.detail.empty()) {
        const float dx = x + valueWidth + ui::tokens::spacing::s4;
        if (dx < right) {
            canvas.drawText(m_texts.detail, {dx, valueY + 2, right - dx, 22}, TypeStyle::Caption, Color::TextTertiary);
        }
    }
}

// ---- OverviewChip ---------------------------------------------------------------------------

OverviewChip::OverviewChip(std::wstring label, std::wstring count) : m_label(std::move(label)), m_count(std::move(count)) {
    setAccessible(ui::AccessRole::Button, m_label + L" " + m_count);
}

ui::SizeF OverviewChip::measure(ui::SizeF /*available*/) {
    if (m_labelWidth < 0 && host()) {
        m_labelWidth = std::ceil(textWidth(m_label, TypeStyle::Body));
        m_countWidth = std::max(kBadgeHeight, std::ceil(textWidth(m_count, TypeStyle::Badge)) + 2 * kBadgePadX);
    }
    return {kChipPadX + std::max(m_labelWidth, 0.0f) + kChipGap + std::max(m_countWidth, 0.0f) + kChipPadX - 4, kHeight};
}

float OverviewChip::focusRadius() const {
    return ui::tokens::radius::button;
}

void OverviewChip::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.strokeRoundRect(b, ui::tokens::radius::button, Color::LineStrong);
    paintHot(canvas, ui::tokens::radius::button);
    const float labelWidth = std::max(m_labelWidth, 0.0f);
    canvas.drawText(m_label, {b.x + kChipPadX, b.y, labelWidth + 1, b.height}, TypeStyle::Body, Color::TextPrimary);
    const RectF badge{b.x + kChipPadX + labelWidth + kChipGap, b.y + std::round((b.height - kBadgeHeight) / 2),
                      std::max(m_countWidth, 0.0f), kBadgeHeight};
    canvas.fillRoundRect(badge, ui::tokens::radius::badge, Color::AccentSubtle);
    canvas.drawText(m_count, badge, TypeStyle::Badge, Color::AccentText, ui::TextAlign::Center);
}

} // namespace wl::app
