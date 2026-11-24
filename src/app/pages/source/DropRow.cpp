#include "app/pages/source/DropRow.h"

#include "app/pages/source/RecentList.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::Ink;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace tk = ui::tokens;

namespace {
constexpr float kLine = tk::kTypeStyles[static_cast<std::size_t>(TypeStyle::Body)].lineHeight; // the sentence's line
constexpr float kIcon = tk::size::iconLarge;
constexpr float kSentenceGap = tk::spacing::s5; // the sentence to the formats
} // namespace

DropRow::DropRow(std::wstring title, std::wstring formats, std::wstring unsupported, std::wstring loading)
    : m_title(std::move(title)), m_formats(std::move(formats)), m_unsupported(std::move(unsupported)),
      m_opening(std::move(loading)) {
    setFocusable(true);
    setAccessible(ui::AccessRole::Button, m_title);
}

void DropRow::layout() {
    if (m_measured) { // the texts never change: once, as soon as there is a host to measure with
        return;
    }
    m_titleWidth = std::ceil(textWidth(m_title, TypeStyle::Body, 180.0f));
    m_unsupportedWidth = std::ceil(textWidth(m_unsupported, TypeStyle::Body, 140.0f));
    m_openingWidth = std::ceil(textWidth(m_opening, TypeStyle::Body, 80.0f));
    m_measured = host() != nullptr;
}

void DropRow::setDragState(DragState state) {
    if (m_drag != state) {
        m_drag = state;
        invalidate();
    }
}

void DropRow::setLoading(bool loading) {
    m_loading = loading;
    invalidate();
}

void DropRow::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, tk::motion::fastMs)) {
        animate();
    }
    invalidate();
}

bool DropRow::tick(double now) {
    const bool running = m_hover.tick(now);
    invalidate();
    return running;
}

void DropRow::onClick() {
    if (!m_loading && onInvoke) {
        onInvoke();
    }
}

bool DropRow::onKeyDown(const ui::KeyEvent& key) {
    return activateOnKey(key);
}

void DropRow::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float radius = tk::radius::r3;
    // At rest under the pointer (a still frame, --hover-at) the hover is whole.
    const float h = hovered() && !m_hover.running() ? 1.0f : m_hover.value();

    Ink border = Ink(Color::LineStrong, Color::TextTertiary, h);
    Ink icon = Ink(Color::TextTertiary, Color::TextSecondary, h);
    Ink title = Color::TextPrimary;
    std::wstring_view sentence = m_loading ? m_opening : m_title;
    float sentenceWidth = m_loading ? m_openingWidth : m_titleWidth;
    ui::icons::Icon glyph = ui::icons::Icon::Download;
    if (m_drag == DragState::Valid) {
        canvas.fillRoundRect(b, radius, Color::AccentSubtle);
        border = Color::AccentBase;
        icon = Color::AccentBase;
    } else if (m_drag == DragState::Invalid) {
        canvas.fillRoundRect(b, radius, Color::StatusErrorSubtle);
        border = Color::StatusError;
        icon = Color::StatusError;
        title = Color::StatusError;
        sentence = m_unsupported;
        sentenceWidth = m_unsupportedWidth;
        glyph = ui::icons::Icon::ErrorOctagon;
    } else if (h > 0) {
        canvas.fillRoundRect(b, radius, Ink(Color::BgHover, Color::BgHover, 0.0f, h));
    }
    canvas.strokeRoundRect(b, radius, border);

    // The icon and the sentence line up with the recent rows' icons and names under it.
    canvas.drawIcon(glyph, {b.x + RecentList::kInset, b.y + std::round((b.height - kIcon) / 2)}, icon, ui::IconVariant::Regular16, kIcon);
    const float x = b.x + RecentList::kTextX;
    const float right = b.right() - RecentList::kInset;
    const float y = b.y + std::round((b.height - kLine) / 2);
    const float w = std::min(sentenceWidth + 1, std::max(right - x, 0.0f));
    canvas.drawText(sentence, {x, y, w, kLine}, TypeStyle::Body, m_loading && m_drag == DragState::None ? Ink(Color::TextSecondary) : title);
    const float formatsX = x + w + kSentenceGap;
    if (formatsX < right) {
        canvas.drawText(m_formats, {formatsX, y, right - formatsX, kLine}, TypeStyle::Caption, Color::TextTertiary);
    }
}

} // namespace wl::app
