#include "ui/widgets/Kbd.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

namespace {

// D-107 (Painter::SearchBox shortcut): the keys in one box ("Ctrl K"), Kbd text, line border, radius 5.
constexpr float kCapHeight = 18.0f;
constexpr float kCapPadding = 5.0f;

std::wstring joined(const std::vector<std::wstring>& keys) {
    std::wstring text;
    for (const auto& key : keys) {
        text += (text.empty() ? L"" : L" ") + key;
    }
    return text;
}

} // namespace

Kbd::Kbd(std::vector<std::wstring> keys) : m_keys(std::move(keys)) {
    setHitTestVisible(false);
}

float Kbd::keysWidth(const TextStyles& text, const std::vector<std::wstring>& keys) {
    return keys.empty() ? 0.0f : std::ceil(text.measure(joined(keys), tokens::TypeStyle::Kbd)) + 2 * kCapPadding;
}

float Kbd::paintKeys(Canvas& canvas, const std::vector<std::wstring>& keys, float right, float centerY) {
    if (keys.empty()) {
        return right;
    }
    const float w = keysWidth(canvas.text(), keys);
    const RectF cap{right - w, std::round(centerY - kCapHeight / 2), w, kCapHeight};
    canvas.strokeRoundRect(cap, tokens::radius::r1, tokens::Color::LineSubtle);
    canvas.drawText(joined(keys), cap, tokens::TypeStyle::Kbd, tokens::Color::TextTertiary, TextAlign::Center);
    return cap.x; // left edge
}

SizeF Kbd::measure(SizeF /*available*/) {
    return {host() ? keysWidth(host()->text(), m_keys) : 0.0f, kCapHeight};
}

void Kbd::paint(Canvas& canvas) {
    const RectF b = bounds();
    paintKeys(canvas, m_keys, b.right(), b.y + b.height / 2);
}

} // namespace wl::ui
