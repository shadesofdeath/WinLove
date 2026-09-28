#include "ui/widgets/Kbd.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

namespace {
constexpr float kCapHeight = 14.0f;
constexpr float kCapPadding = 3.0f;
constexpr float kCapGap = 2.0f;

float capWidth(const TextStyles& text, const std::wstring& key) {
    return std::ceil(text.measure(key, tokens::TypeStyle::Kbd)) + 2 * kCapPadding + 2; // + 1px border each side
}
} // namespace

Kbd::Kbd(std::vector<std::wstring> keys) : m_keys(std::move(keys)) {
    setHitTestVisible(false);
}

float Kbd::keysWidth(const TextStyles& text, const std::vector<std::wstring>& keys) {
    float width = 0;
    for (const auto& key : keys) {
        width += capWidth(text, key);
    }
    return width + kCapGap * static_cast<float>(keys.empty() ? 0 : keys.size() - 1);
}

float Kbd::paintKeys(Canvas& canvas, const std::vector<std::wstring>& keys, float right, float centerY) {
    // Laid out right to left so callers can right-align without measuring first.
    const float y = std::round(centerY - kCapHeight / 2);
    float x = right;
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        const float w = capWidth(canvas.text(), *it);
        const RectF cap{x - w, y, w, kCapHeight};
        canvas.strokeRoundRect(cap, tokens::radius::r1, tokens::Color::LineStrong);
        canvas.drawText(*it, cap, tokens::TypeStyle::Kbd, tokens::Color::TextTertiary, TextAlign::Center);
        x = cap.x - kCapGap;
    }
    return x + kCapGap; // left edge
}

SizeF Kbd::measure(SizeF /*available*/) {
    return {host() ? keysWidth(host()->text(), m_keys) : 0.0f, kCapHeight};
}

void Kbd::paint(Canvas& canvas) {
    const RectF b = bounds();
    paintKeys(canvas, m_keys, b.right(), b.y + b.height / 2);
}

} // namespace wl::ui
