#include "ui/widgets/SearchBox.h"

#include "ui/platform/Clipboard.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;

constexpr float kPadding = 6.0f;
constexpr float kIconGap = 6.0f;
constexpr float kClearSize = 16.0f;

bool isWordChar(wchar_t c) {
    return std::iswalnum(c) || c == L'_';
}

} // namespace

SearchBox::SearchBox(std::wstring placeholder, std::vector<std::wstring> hintKeys)
    : m_placeholder(std::move(placeholder)), m_hintKeys(std::move(hintKeys)) {
    setFocusable(true);
    setAccessible(AccessRole::Edit, m_placeholder);
}

void SearchBox::setText(std::wstring text) {
    m_text = std::move(text);
    m_caret = m_anchor = m_text.size();
    m_scroll = 0;
    invalidate();
}

SizeF SearchBox::measure(SizeF /*available*/) {
    return {m_width, tokens::size::control};
}

RectF SearchBox::textRect() const {
    const RectF b = bounds();
    const float left = b.x + kPadding + (m_plain ? 0.0f : tokens::size::icon + kIconGap);
    const float right = b.right() - kPadding - (m_text.empty() || m_plain ? 0.0f : kClearSize + 4);
    return {left, b.y, std::max(right - left, 0.0f), b.height};
}

RectF SearchBox::clearRect() const {
    const RectF b = bounds();
    return {b.right() - kPadding - kClearSize, b.y + (b.height - kClearSize) / 2, kClearSize, kClearSize};
}

float SearchBox::offsetOf(std::size_t index) const {
    if (!host() || index == 0) {
        return 0.0f;
    }
    return host()->text().measure(std::wstring_view(m_text).substr(0, index), TypeStyle::Body);
}

std::size_t SearchBox::indexAt(float x) const {
    const float local = x - textRect().x + m_scroll;
    std::size_t best = 0;
    float bestDistance = std::abs(local);
    for (std::size_t i = 1; i <= m_text.size(); ++i) {
        const float d = std::abs(offsetOf(i) - local);
        if (d < bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void SearchBox::ensureCaretVisible() {
    const float width = textRect().width;
    const float caret = offsetOf(m_caret);
    if (caret - m_scroll > width - 2) {
        m_scroll = caret - width + 2;
    } else if (caret < m_scroll) {
        m_scroll = caret;
    }
    m_scroll = std::max(m_scroll, 0.0f);
}

void SearchBox::changed() {
    ensureCaretVisible();
    invalidate();
    if (onChange) {
        onChange(m_text);
    }
}

void SearchBox::replaceSelection(std::wstring_view with) {
    const std::size_t from = std::min(m_anchor, m_caret);
    const std::size_t to = std::max(m_anchor, m_caret);
    m_text.replace(from, to - from, with);
    m_caret = m_anchor = from + with.size();
    changed();
}

void SearchBox::moveCaret(std::size_t to, bool extend) {
    m_caret = std::min(to, m_text.size());
    if (!extend) {
        m_anchor = m_caret;
    }
    ensureCaretVisible();
    invalidate();
}

void SearchBox::onPointerDown(PointF p) {
    if (!m_plain && !m_text.empty() && clearRect().contains(p)) {
        setText({});
        if (onChange) {
            onChange(m_text);
        }
        return;
    }
    moveCaret(indexAt(p.x), /*extend=*/false);
    m_dragging = true;
}

void SearchBox::onPointerMove(PointF p) {
    if (m_dragging && pressed()) {
        moveCaret(indexAt(p.x), /*extend=*/true);
    } else {
        m_dragging = false;
    }
}

void SearchBox::onFocusChanged(bool focused) {
    if (!focused) {
        m_anchor = m_caret;
        m_dragging = false;
    }
    invalidate();
}

bool SearchBox::onChar(wchar_t ch) {
    replaceSelection(std::wstring_view(&ch, 1));
    return true;
}

bool SearchBox::onKeyDown(const KeyEvent& key) {
    auto wordLeft = [&](std::size_t i) {
        while (i > 0 && !isWordChar(m_text[i - 1])) {
            --i;
        }
        while (i > 0 && isWordChar(m_text[i - 1])) {
            --i;
        }
        return i;
    };
    auto wordRight = [&](std::size_t i) {
        while (i < m_text.size() && isWordChar(m_text[i])) {
            ++i;
        }
        while (i < m_text.size() && !isWordChar(m_text[i])) {
            ++i;
        }
        return i;
    };
    // One user-perceived step never splits a UTF-16 surrogate pair (emoji, rare CJK).
    auto prev = [&](std::size_t i) {
        if (i == 0) {
            return i;
        }
        --i;
        return i > 0 && IS_LOW_SURROGATE(m_text[i]) && IS_HIGH_SURROGATE(m_text[i - 1]) ? i - 1 : i;
    };
    auto next = [&](std::size_t i) {
        if (i >= m_text.size()) {
            return m_text.size();
        }
        ++i;
        return i < m_text.size() && IS_LOW_SURROGATE(m_text[i]) && IS_HIGH_SURROGATE(m_text[i - 1]) ? i + 1 : i;
    };
    switch (key.virtualKey) {
    case VK_LEFT:
        if (hasSelection() && !key.shift) {
            moveCaret(std::min(m_anchor, m_caret), false);
        } else {
            moveCaret(key.ctrl ? wordLeft(m_caret) : prev(m_caret), key.shift);
        }
        return true;
    case VK_RIGHT:
        if (hasSelection() && !key.shift) {
            moveCaret(std::max(m_anchor, m_caret), false);
        } else {
            moveCaret(key.ctrl ? wordRight(m_caret) : next(m_caret), key.shift);
        }
        return true;
    case VK_HOME: moveCaret(0, key.shift); return true;
    case VK_END: moveCaret(m_text.size(), key.shift); return true;
    case VK_BACK:
        if (!hasSelection() && m_caret > 0) {
            m_anchor = key.ctrl ? wordLeft(m_caret) : prev(m_caret);
        }
        replaceSelection({});
        return true;
    case VK_DELETE:
        if (!hasSelection() && m_caret < m_text.size()) {
            m_anchor = key.ctrl ? wordRight(m_caret) : next(m_caret);
        }
        replaceSelection({});
        return true;
    case VK_ESCAPE:
        if (m_text.empty()) {
            return false; // let the page handle Esc
        }
        setText({});
        if (onChange) {
            onChange(m_text);
        }
        return true;
    case VK_RETURN:
        if (onSubmit) {
            onSubmit();
        }
        return true;
    default: break;
    }
    if (key.ctrl && !key.alt) {
        const std::size_t from = std::min(m_anchor, m_caret);
        const std::size_t to = std::max(m_anchor, m_caret);
        switch (key.virtualKey) {
        case 'A':
            m_anchor = 0;
            moveCaret(m_text.size(), true);
            return true;
        case 'C':
            if (hasSelection()) {
                setClipboardText(std::wstring_view(m_text).substr(from, to - from));
            }
            return true;
        case 'X':
            if (hasSelection()) {
                // Only cut what actually reached the clipboard (another app may hold it open).
                if (setClipboardText(std::wstring_view(m_text).substr(from, to - from))) {
                    replaceSelection({});
                }
            }
            return true;
        case 'V': {
            std::wstring pasted = clipboardText();
            std::erase_if(pasted, [](wchar_t c) { return c == L'\r' || c == L'\n' || c == L'\t'; });
            replaceSelection(pasted);
            return true;
        }
        default: break;
        }
    }
    return false;
}

void SearchBox::paint(Canvas& canvas) {
    const RectF b = bounds();
    const Color border = focused() ? Color::AccentBase : hovered() ? Color::TextTertiary : Color::LineStrong;
    canvas.fillRoundRect(b, tokens::radius::r2, Color::BgInput);
    canvas.strokeRoundRect(b, tokens::radius::r2, border);
    if (!m_plain) {
        canvas.drawIcon(icons::Icon::Search, {b.x + kPadding, b.y + (b.height - tokens::size::icon) / 2},
                        Color::TextTertiary);
    }

    const RectF area = textRect();
    if (m_text.empty()) {
        canvas.drawText(m_placeholder, area, TypeStyle::Body, Color::TextTertiary);
        if (!m_hintKeys.empty() && !focused()) {
            Kbd::paintKeys(canvas, m_hintKeys, b.right() - kPadding, b.y + b.height / 2);
        }
    } else if (!m_plain) {
        canvas.drawIcon(icons::Icon::Close, {clearRect().x, clearRect().y}, Color::TextTertiary);
    }

    canvas.pushClip(area);
    if (focused() && hasSelection()) {
        const float x0 = area.x - m_scroll + offsetOf(std::min(m_anchor, m_caret));
        const float x1 = area.x - m_scroll + offsetOf(std::max(m_anchor, m_caret));
        canvas.fillRect({x0, b.y + 4, x1 - x0, b.height - 8}, Color::AccentSubtle);
    }
    if (!m_text.empty()) {
        canvas.drawText(m_text, {area.x - m_scroll, b.y, offsetOf(m_text.size()) + 8, b.height}, TypeStyle::Body,
                        Color::TextPrimary);
    }
    if (focused()) {
        const float x = std::round(area.x - m_scroll + offsetOf(m_caret));
        canvas.fillRect({x, b.y + 5, 1.0f / canvas.scale(), b.height - 10}, Color::TextPrimary);
    }
    canvas.popClip();
}

} // namespace wl::ui
