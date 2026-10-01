#include "ui/widgets/LogConsole.h"

#include "ui/platform/Clipboard.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;

constexpr float kPadX = 12.0f;
constexpr float kPadY = 8.0f;
constexpr float kChip = 20.0f;
constexpr float kChipMargin = 12.0f;

Color levelInk(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return Color::TextTertiary;
    case LogLevel::Info: return Color::StatusInfo;
    case LogLevel::Warn: return Color::StatusWarning;
    case LogLevel::Error: return Color::StatusError;
    }
    return Color::TextSecondary;
}

const wchar_t* levelText(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return L"DBG";
    case LogLevel::Info: return L"INFO";
    case LogLevel::Warn: return L"WARN";
    case LogLevel::Error: return L"ERR";
    }
    return L"";
}

std::wstring lowered(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

} // namespace

LogConsole::LogConsole() {
    setFocusable(true);
    setAccessible(AccessRole::List, L"Log");
    m_scrollBar = &add<ScrollBar>();
    m_scrollBar->onScroll = [this](float offset) { setOffset(offset, /*byUser=*/true); };
}

RectF LogConsole::viewport() const {
    const RectF b = bounds();
    return {b.x + kPadX, b.y + kPadY, std::max(b.width - 2 * kPadX, 0.0f), std::max(b.height - 2 * kPadY, 0.0f)};
}

float LogConsole::maxOffset() const {
    return std::max(static_cast<float>(m_lines.size()) * kRow - viewport().height, 0.0f);
}

void LogConsole::layout() {
    const RectF b = bounds();
    m_scrollBar->setBounds({b.right() - ScrollBar::kWidth - 1, b.y + 1, ScrollBar::kWidth, b.height - 2});
    m_scrollBar->setRange(static_cast<float>(m_lines.size()) * kRow, viewport().height);
    if (m_autoScroll) {
        m_offset = maxOffset();
    }
    m_offset = std::clamp(m_offset, 0.0f, maxOffset());
    m_scrollBar->setOffset(m_offset);
}

void LogConsole::setLines(std::vector<LogLine> lines) {
    m_lines = std::move(lines);
    m_anchor = m_selFrom = m_selTo = -1;
    m_unseen = 0;
    layout();
    invalidate();
}

void LogConsole::append(std::span<const LogLine> lines) {
    if (lines.empty()) {
        return;
    }
    m_lines.insert(m_lines.end(), lines.begin(), lines.end());
    if (!m_autoScroll) {
        m_unseen += lines.size();
    }
    layout();
    invalidate();
}

void LogConsole::setHighlight(std::wstring needle) {
    m_needle = lowered(needle);
    invalidate();
}

void LogConsole::setAutoScroll(bool on) {
    if (m_autoScroll == on) {
        return;
    }
    m_autoScroll = on;
    if (on) {
        m_unseen = 0;
        setOffset(maxOffset(), /*byUser=*/false);
    }
    invalidate();
}

void LogConsole::scrollToEnd() {
    const bool wasOn = m_autoScroll;
    setAutoScroll(true);
    if (!wasOn && onAutoScrollChanged) {
        onAutoScrollChanged(true);
    }
}

void LogConsole::setOffset(float offset, bool byUser) {
    m_offset = std::clamp(offset, 0.0f, maxOffset());
    m_scrollBar->setOffset(m_offset);
    if (byUser) {
        const bool atEnd = m_offset >= maxOffset() - 0.5f;
        // Scrolling up stops following; the user turns it back on (toggle / chip / End).
        if (!atEnd && m_autoScroll) {
            m_autoScroll = false;
            if (onAutoScrollChanged) {
                onAutoScrollChanged(false);
            }
        }
        if (atEnd) {
            m_unseen = 0;
        }
    }
    invalidate();
}

bool LogConsole::onWheel(PointF /*p*/, float lines) {
    setOffset(m_offset - lines * kRow, /*byUser=*/true);
    return true;
}

int LogConsole::rowAt(PointF p) const {
    const RectF v = viewport();
    const int row = static_cast<int>(std::floor((p.y - v.y + m_offset) / kRow));
    return std::clamp(row, 0, std::max(static_cast<int>(m_lines.size()) - 1, 0));
}

RectF LogConsole::chipRect(Canvas* canvas) const {
    const RectF b = bounds();
    float width = m_chipWidth;
    if (canvas && newLinesText) {
        width = std::ceil(canvas->text().measure(newLinesText(m_unseen), TypeStyle::Caption)) + 16;
    }
    return {std::round(b.x + (b.width - width) / 2), b.bottom() - kChipMargin - kChip, width, kChip};
}

void LogConsole::select(int from, int to) {
    m_selFrom = std::min(from, to);
    m_selTo = std::max(from, to);
    invalidate();
}

void LogConsole::revealRow(int row) {
    const RectF v = viewport();
    const float top = static_cast<float>(row) * kRow;
    if (top < m_offset) {
        setOffset(top, true);
    } else if (top + kRow > m_offset + v.height) {
        setOffset(top + kRow - v.height, true);
    }
}

void LogConsole::onPointerDown(PointF p) {
    if (m_unseen > 0 && !m_autoScroll && chipRect(nullptr).contains(p)) {
        scrollToEnd();
        return;
    }
    if (m_lines.empty() || !viewport().contains(p)) {
        return;
    }
    const int row = rowAt(p);
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift && m_anchor >= 0) {
        select(m_anchor, row);
    } else {
        m_anchor = row;
        select(row, row);
    }
    m_dragging = true;
}

void LogConsole::onPointerMove(PointF p) {
    if (m_dragging && captured() && m_anchor >= 0) {
        select(m_anchor, rowAt(p));
    } else {
        m_dragging = false;
    }
}

std::wstring LogConsole::selectedText() const {
    std::wstring out;
    if (m_selFrom < 0) {
        return out;
    }
    for (int i = m_selFrom; i <= m_selTo && i < static_cast<int>(m_lines.size()); ++i) {
        const auto& l = m_lines[static_cast<std::size_t>(i)];
        out += copyFormat ? copyFormat(l) : l.time + L" " + levelText(l.level) + L" " + l.source + L" " + l.message;
        out += L"\r\n";
    }
    return out;
}

bool LogConsole::onKeyDown(const KeyEvent& key) {
    const int count = static_cast<int>(m_lines.size());
    const int page = std::max(static_cast<int>(viewport().height / kRow) - 1, 1);
    if (key.ctrl && key.virtualKey == 'C') {
        if (m_selFrom >= 0) {
            setClipboardText(selectedText());
        }
        return true;
    }
    if (key.ctrl && key.virtualKey == 'A') {
        if (count > 0) {
            m_anchor = 0;
            select(0, count - 1);
        }
        return true;
    }
    auto moveTo = [&](int row) {
        if (count == 0) {
            return;
        }
        row = std::clamp(row, 0, count - 1);
        if (key.shift && m_anchor >= 0) {
            select(m_anchor, row);
        } else {
            m_anchor = row;
            select(row, row);
        }
        revealRow(row);
    };
    const int current = m_selTo >= 0 ? (m_anchor == m_selFrom ? m_selTo : m_selFrom) : -1;
    switch (key.virtualKey) {
    case VK_DOWN: moveTo(current + 1); return true;
    case VK_UP: moveTo(current < 0 ? count - 1 : current - 1); return true;
    case VK_NEXT: moveTo(current + page); return true;
    case VK_PRIOR: moveTo(current - page); return true;
    case VK_HOME:
        if (key.ctrl || current < 0) {
            setOffset(0, true);
        }
        moveTo(0);
        return true;
    case VK_END:
        moveTo(count - 1);
        scrollToEnd();
        return true;
    case VK_ESCAPE:
        if (m_selFrom >= 0) {
            m_anchor = m_selFrom = m_selTo = -1;
            invalidate();
            return true;
        }
        return false;
    default: return false;
    }
}

void LogConsole::paint(Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgInput);
    canvas.strokeRoundRect(b, 0.0f, Color::LineSubtle);
    const RectF v = viewport();
    if (m_lines.empty()) {
        return;
    }

    // Column widths from the mono advance: time 8 ch, level 5 ch, source 7 ch (+ gaps).
    const float ch = canvas.text().measure(L"0000000000", TypeStyle::Mono) / 10.0f;
    const float timeX = v.x;
    const float levelX = std::round(timeX + ch * 8 + 12);
    const float sourceX = std::round(levelX + ch * 5 + 6);
    const float messageX = std::round(sourceX + ch * 7 + 12);
    const float right = b.right() - ScrollBar::kWidth;

    canvas.pushClip({b.x + 1, b.y + 1, b.width - 2, b.height - 2});
    const int first = std::max(static_cast<int>(m_offset / kRow), 0);
    const int last = std::min(static_cast<int>((m_offset + v.height) / kRow) + 1, static_cast<int>(m_lines.size()) - 1);
    for (int i = first; i <= last; ++i) {
        const auto& line = m_lines[static_cast<std::size_t>(i)];
        const float y = std::round(v.y + static_cast<float>(i) * kRow - m_offset);
        const RectF row{b.x + 1, y, b.width - 2, kRow};
        const bool selected = i >= m_selFrom && i <= m_selTo && m_selFrom >= 0;
        if (line.level == LogLevel::Error) {
            canvas.fillRect(row, Color::StatusErrorSubtle);
        }
        if (selected) {
            canvas.fillRect(row, Ink(Color::BgPressed, Color::BgPressed, 0.0f, focused() ? 1.0f : 0.7f));
        }
        canvas.drawText(line.time, {timeX, y, levelX - timeX, kRow}, TypeStyle::Mono, Color::TextTertiary);
        canvas.drawText(levelText(line.level), {levelX, y, sourceX - levelX, kRow}, TypeStyle::Mono,
                        levelInk(line.level));
        canvas.drawText(line.source, {sourceX, y, messageX - sourceX - 4, kRow}, TypeStyle::Mono, Color::TextSecondary);
        if (!m_needle.empty()) {
            const std::wstring hay = lowered(line.message);
            for (std::size_t at = hay.find(m_needle); at != std::wstring::npos; at = hay.find(m_needle, at + 1)) {
                const float x0 = messageX + canvas.text().measure(std::wstring_view(line.message).substr(0, at), TypeStyle::Mono);
                const float x1 = messageX + canvas.text().measure(
                                                std::wstring_view(line.message).substr(0, at + m_needle.size()), TypeStyle::Mono);
                if (x0 > right) {
                    break;
                }
                canvas.fillRect({x0, y + 2, std::min(x1, right) - x0, kRow - 4}, Color::AccentSubtle);
            }
        }
        canvas.drawText(line.message, {messageX, y, right - messageX, kRow}, TypeStyle::Mono,
                        line.level == LogLevel::Error ? Color::TextPrimary : Color::TextSecondary);
    }

    // "↓ n new lines" chip while not following.
    if (!m_autoScroll && m_unseen > 0 && newLinesText) {
        const RectF chip = chipRect(&canvas);
        m_chipWidth = chip.width;
        canvas.dropShadow(chip, tokens::radius::r3, tokens::elevation::menu);
        canvas.fillRoundRect(chip, tokens::radius::r3, Color::BgOverlay);
        canvas.strokeRoundRect(chip, tokens::radius::r3, Color::LineStrong);
        canvas.drawText(newLinesText(m_unseen), chip, TypeStyle::Caption, Color::TextPrimary, TextAlign::Center);
    }
    canvas.popClip();
}

} // namespace wl::ui
