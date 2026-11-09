#include "app/shell/StatusBar.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {
constexpr float kPadX = 12.0f;   // D-109: the reference tool's text starts ~12 in
constexpr float kTaskGap = 20.0f;
constexpr float kBar = 80.0f;
} // namespace

StatusBar::StatusBar(Labels labels) : m_labels(std::move(labels)) {
    setAccessible(ui::AccessRole::Text, m_labels.noMount);
}

void StatusBar::setMount(std::optional<std::wstring> edition, std::wstring mountPath) {
    m_edition = std::move(edition);
    m_mountPath = std::move(mountPath);
    setAccessible(ui::AccessRole::Text, m_edition ? m_labels.mounted + L" · " + *m_edition : m_labels.noMount);
    invalidate();
}

void StatusBar::setTask(std::optional<std::wstring> label, float fraction) {
    m_task = std::move(label);
    m_taskFraction = fraction;
    invalidate();
}

void StatusBar::setInfo(std::wstring info) {
    if (info != m_info) {
        m_info = std::move(info);
        invalidate();
    }
}

void StatusBar::paint(ui::Canvas& canvas) {
    // D-109 (the reference tool): the panel color, no line; left what WinLove does or did last (the mount, a
    // running task with its bar), right the image's facts — all in text.primary.
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    const auto& text = canvas.text();

    float right = b.right() - kPadX;
    if (!m_info.empty()) {
        const float w = std::ceil(text.measure(m_info, TypeStyle::Caption)) + 1;
        const float x = std::max(right - w, b.x + b.width / 2);
        canvas.drawText(m_info, {x, b.y, right - x, b.height}, TypeStyle::Caption, Color::TextPrimary);
        right = x - kTaskGap;
    }

    float x = b.x + kPadX;
    const float cy = b.y + b.height / 2;
    if (m_task) {
        const std::wstring pct = std::to_wstring(static_cast<int>(std::clamp(m_taskFraction, 0.0f, 1.0f) * 100)) + L"%";
        const float labelW = std::min(std::ceil(text.measure(*m_task, TypeStyle::Caption)) + 1, std::max(right - x - kBar - 60, 0.0f));
        canvas.drawText(*m_task, {x, b.y, labelW, b.height}, TypeStyle::Caption, Color::TextPrimary);
        x += labelW + 8;
        canvas.progressBar({x, std::round(cy - size::progress / 2), kBar, size::progress}, m_taskFraction);
        x += kBar + 8;
        canvas.drawText(pct, {x, b.y, std::max(right - x, 0.0f), b.height}, TypeStyle::Caption, Color::TextPrimary);
        return;
    }
    const std::wstring line = m_edition ? m_labels.mounted + L" - " + *m_edition + L" - " + m_mountPath : m_labels.noMount;
    canvas.drawText(line, {x, b.y, std::max(right - x, 0.0f), b.height}, TypeStyle::Caption, Color::TextPrimary);
}

} // namespace wl::app
