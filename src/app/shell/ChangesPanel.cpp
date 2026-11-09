#include "app/shell/ChangesPanel.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

constexpr float kPad = size::panelPad; // 24
constexpr float kTitleH = 20.0f;
constexpr float kListTop = 32.0f;      // under the title
constexpr float kEntryH = 30.0f;
constexpr float kFooterInfo = 40.0f;   // the line above the button: mount folder / stage
constexpr float kButton = size::controlXL; // 44

} // namespace

ChangesPanel::ChangesPanel(Labels labels) : m_labels(std::move(labels)) {
    m_button = &add<ui::Button>(ui::ButtonKind::Primary, m_labels.apply);
    m_button->setHeight(kButton);
    m_button->onInvoke = [this] {
        if (auto apply = onApply) {
            apply();
        }
    };
    setAccessible(ui::AccessRole::Group, m_labels.title);
}

void ChangesPanel::set(std::vector<Entry> entries, std::wstring operations, std::optional<std::wstring> mountPath,
                       std::optional<Run> run, PageId current, bool canApply) {
    m_entries = std::move(entries);
    m_operations = std::move(operations);
    m_mountPath = std::move(mountPath);
    m_run = std::move(run);
    m_current = current;
    m_hover = -1;
    // Durdur while the run goes: the same button, so its place and size do not jump.
    m_button->setText(m_run ? m_labels.stop : m_labels.apply);
    m_button->setEnabled(m_run || canApply);
    layout();
    invalidate();
}

float ChangesPanel::footerTop() const {
    const RectF b = bounds();
    return b.bottom() - kPad - kButton - kFooterInfo;
}

RectF ChangesPanel::entryRect(std::size_t index) const {
    const RectF b = bounds();
    return {b.x + kPad - 8, b.y + kPad + kListTop + kEntryH * static_cast<float>(index), b.width - 2 * kPad + 16, kEntryH};
}

void ChangesPanel::layout() {
    const RectF b = bounds();
    m_button->setBounds({b.x + kPad, b.bottom() - kPad - kButton, std::max(b.width - 2 * kPad, 0.0f), kButton});
}

void ChangesPanel::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.x, b.y, b.height, Color::LineSubtle);
    const float left = b.x + kPad;
    const float right = b.right() - kPad;
    const float y = b.y + kPad;
    canvas.drawText(m_labels.title, {left, y, right - left, kTitleH}, TypeStyle::Panel, Color::TextPrimary);
    if (!m_operations.empty()) {
        canvas.drawText(m_operations, {left, y, right - left, kTitleH}, TypeStyle::Small, Color::TextTertiary,
                        ui::TextAlign::Trailing);
    }

    const float bottom = footerTop() - 12;
    if (m_entries.empty()) {
        canvas.drawText(m_labels.empty, {left, y + kListTop, right - left, 20}, TypeStyle::BodyStrong, Color::TextSecondary);
        canvas.drawTextWrapped(m_labels.emptyHint, {left, y + kListTop + 26, right - left, std::max(bottom - y - kListTop - 26, 0.0f)},
                               TypeStyle::Small, Color::TextTertiary);
    }
    canvas.pushClip({b.x, y + kListTop, b.width, std::max(bottom - (y + kListTop), 0.0f)});
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const RectF r = entryRect(i);
        if (r.y > bottom) {
            break;
        }
        const auto& e = m_entries[i];
        const bool current = e.page == m_current;
        if (current) {
            canvas.fillRoundRect(r, ui::tokens::radius::menu, Color::BgRaised);
        } else if (static_cast<int>(i) == m_hover) {
            canvas.fillRoundRect(r, ui::tokens::radius::menu, Color::BgHover);
        }
        const std::wstring n = std::to_wstring(e.count);
        canvas.drawText(e.label, {left, r.y, right - left - 40, r.height}, TypeStyle::BodySemi, Color::TextPrimary);
        canvas.drawText(n, {left, r.y, right - left, r.height}, TypeStyle::BodyStrong, Color::TextTertiary, ui::TextAlign::Trailing);
    }
    canvas.popClip();

    // Footer: a line, then the mount folder (or the run), then the button.
    const float fy = footerTop();
    canvas.hairlineH(left, fy, right - left, Color::LineSubtle);
    const RectF info{left, fy + 12, right - left, 20};
    if (m_run) {
        const std::wstring pct = std::to_wstring(static_cast<int>(std::round(std::clamp(m_run->fraction, 0.0f, 1.0f) * 100))) + L"%";
        const float pw = std::ceil(canvas.text().measure(pct, TypeStyle::BodySemi)) + 1;
        canvas.drawText(m_run->stage, {info.x, info.y - 2, info.width - pw - 8, info.height}, TypeStyle::BodySemi, Color::TextPrimary);
        canvas.drawText(pct, {info.x, info.y - 2, info.width, info.height}, TypeStyle::BodySemi, Color::AccentText,
                        ui::TextAlign::Trailing);
        canvas.progressBar({info.x, info.y + 24, info.width, size::progress}, m_run->fraction);
    } else {
        const std::wstring& label = m_mountPath ? m_labels.mounted : m_labels.noMount;
        const float lw = std::ceil(canvas.text().measure(label, TypeStyle::Small)) + 1;
        canvas.drawText(label, {info.x, info.y, lw, info.height}, TypeStyle::Small, Color::TextTertiary);
        if (m_mountPath) {
            const float x = info.x + lw + 12;
            canvas.drawText(*m_mountPath, {x, info.y, info.right() - x, info.height}, TypeStyle::Mono, Color::TextSecondary,
                            ui::TextAlign::Trailing);
        }
    }
}

void ChangesPanel::onPointerMove(ui::PointF p) {
    int hover = -1;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const RectF r = entryRect(i);
        if (r.y + r.height > footerTop() - 12) {
            break;
        }
        if (r.contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
}

void ChangesPanel::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hover = -1;
    }
    invalidate();
}

ui::Cursor ChangesPanel::cursor() const {
    return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void ChangesPanel::onClick() {
    if (m_hover >= 0 && m_hover < static_cast<int>(m_entries.size()) && onPage) {
        auto open = onPage;
        open(m_entries[static_cast<std::size_t>(m_hover)].page);
    }
}

} // namespace wl::app
