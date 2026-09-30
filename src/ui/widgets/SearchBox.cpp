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
// Bare (command palette) variant.
constexpr float kBarePadding = 16.0f;
constexpr float kBareIconGap = 8.0f;
constexpr float kBareHintGap = 12.0f;
constexpr float kBareLine = 20.0f;      // selection band
constexpr float kBareCaret = 16.0f;
constexpr float kCompletionGap = 3.0f;  // caret → greyed continuation

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
    m_completion.clear();
    m_caret = m_anchor = m_text.size();
    m_scroll = 0;
    invalidate();
}

SizeF SearchBox::measure(SizeF /*available*/) {
    return {m_width, tokens::size::control};
}

void SearchBox::setCompletion(std::wstring rest) {
    if (rest != m_completion) {
        m_completion = std::move(rest);
        invalidate();
    }
}

bool SearchBox::acceptCompletion() {
    if (m_completion.empty() || hasSelection() || m_caret != m_text.size()) {
        return false;
    }
    const std::wstring rest = std::move(m_completion);
    m_completion.clear();
    replaceSelection(rest);
    return true;
}

RectF SearchBox::textRect() const {
    const RectF b = bounds();
    if (m_bare) {
        const float left = b.x + kBarePadding + tokens::size::icon + kBareIconGap;
        const float hint = m_hintKeys.empty() || !host() ? 0.0f : Kbd::keysWidth(host()->text(), m_hintKeys) + kBareHintGap;
        return {left, b.y, std::max(b.right() - kBarePadding - hint - left, 0.0f), b.height};
    }
    const float left = b.x + kPadding + (m_plain ? 0.0f : tokens::size::icon + kIconGap);
    const float right = b.right() - kPadding - (m_text.empty() || m_plain ? 0.0f : kClearSize + 4);
    return {left, b.y, std::max(right - left, 0.0f), b.height};
}

RectF SearchBox::clearRect() const {
    const RectF b = bounds();
    return {b.right() - kPadding - kClearSize, b.y + (b.height - kClearSize) / 2, kClearSize, kClearSize};
}

std::wstring SearchBox::displayText() const {
    return m_password ? std::wstring(m_text.size(), L'•') : m_text;
}

float SearchBox::offsetOf(std::size_t index) const {
    if (!host() || index == 0) {
        return 0.0f;
    }
    const std::wstring shown = displayText();
    return host()->text().measure(std::wstring_view(shown).substr(0, index), TypeStyle::Body);
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
    m_completion.clear(); // the owner offers a new one from onChange
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
    if (!m_plain && !m_bare && !m_text.empty() && clearRect().contains(p)) {
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
        } else if (key.shift || !acceptCompletion()) {
            moveCaret(key.ctrl ? wordRight(m_caret) : next(m_caret), key.shift);
        }
        return true;
    case VK_HOME: moveCaret(0, key.shift); return true;
    case VK_END:
        if (key.shift || !acceptCompletion()) {
            moveCaret(m_text.size(), key.shift);
        }
        return true;
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
        if (m_text.empty() || m_bare) {
            return false; // let the page (or the palette) handle Esc
        }
        setText({});
        if (onChange) {
            onChange(m_text);
        }
        return true;
    case VK_RETURN:
        // A copy: the handler may close the dialog this box lives in (and destroy the box).
        if (const auto submit = onSubmit) {
            submit();
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
            if (hasSelection() && !m_password) {
                setClipboardText(std::wstring_view(m_text).substr(from, to - from));
            }
            return true;
        case 'X':
            if (hasSelection() && !m_password) {
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
    if (m_bare) {
        canvas.drawIcon(icons::Icon::Search, {b.x + kBarePadding, b.y + (b.height - tokens::size::icon) / 2},
                        Color::TextTertiary);
        if (!m_hintKeys.empty()) {
            Kbd::paintKeys(canvas, m_hintKeys, b.right() - kBarePadding, b.y + b.height / 2);
        }
    } else {
        const Color border = focused() ? Color::AccentBase : hovered() ? Color::TextTertiary : Color::LineStrong;
        canvas.fillRoundRect(b, tokens::radius::r2, Color::BgInput);
        canvas.strokeRoundRect(b, tokens::radius::r2, border);
        if (!m_plain) {
            canvas.drawIcon(icons::Icon::Search, {b.x + kPadding, b.y + (b.height - tokens::size::icon) / 2},
                            Color::TextTertiary);
        }
    }

    const RectF area = textRect();
    if (m_text.empty()) {
        canvas.drawText(m_placeholder, area, TypeStyle::Body, Color::TextTertiary);
        if (!m_bare && !m_hintKeys.empty() && !focused()) {
            Kbd::paintKeys(canvas, m_hintKeys, b.right() - kPadding, b.y + b.height / 2);
        }
    } else if (!m_plain && !m_bare) {
        canvas.drawIcon(icons::Icon::Close, {clearRect().x, clearRect().y}, Color::TextTertiary);
    }

    // The bare box is as tall as its row (40px): keep the selection and the caret text-sized.
    const float band = m_bare ? kBareLine : b.height - 8;
    const float caret = m_bare ? kBareCaret : b.height - 10;
    canvas.pushClip(area);
    if (focused() && hasSelection()) {
        const float x0 = area.x - m_scroll + offsetOf(std::min(m_anchor, m_caret));
        const float x1 = area.x - m_scroll + offsetOf(std::max(m_anchor, m_caret));
        canvas.fillRect({x0, b.y + (b.height - band) / 2, x1 - x0, band}, Color::AccentSubtle);
    }
    const float end = area.x - m_scroll + offsetOf(m_text.size());
    if (!m_text.empty()) {
        canvas.drawText(displayText(), {area.x - m_scroll, b.y, offsetOf(m_text.size()) + 8, b.height}, TypeStyle::Body,
                        Color::TextPrimary);
        if (!m_completion.empty() && !hasSelection() && m_caret == m_text.size()) {
            const float x = end + kCompletionGap;
            canvas.drawText(m_completion, {x, b.y, std::max(area.right() - x, 0.0f), b.height}, TypeStyle::Body,
                            Color::TextTertiary);
        }
    }
    if (focused()) {
        const float x = std::round(area.x - m_scroll + offsetOf(m_caret));
        canvas.fillRect({x, b.y + (b.height - caret) / 2, 1.0f / canvas.scale(), caret},
                        m_bare ? Color::AccentBase : Color::TextPrimary);
    }
    canvas.popClip();
}

} // namespace wl::ui
