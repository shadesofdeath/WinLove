#pragma once
// SearchBox per 03_components/textbox.md: 24px, bg.input, 1px line.strong (hover text.tertiary,
// focus accent.base), search icon left, keycap hint right ("Ctrl F") — replaced by a clear
// button while there is text. Single-line editing: caret, selection (Shift+arrows, Ctrl+A,
// drag), Home/End, Backspace/Delete (Ctrl = word), Ctrl+C/X/V, Esc clears.
#include "ui/widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

class SearchBox : public Widget {
public:
    SearchBox(std::wstring placeholder, std::vector<std::wstring> hintKeys = {});
    // Plain TextBox (textbox.md): no search icon, no clear button, no keycap hint.
    void setPlain(bool plain) noexcept { m_plain = plain; }

    std::function<void(const std::wstring&)> onChange;
    std::function<void()> onSubmit; // Enter

    [[nodiscard]] const std::wstring& text() const noexcept { return m_text; }
    void setText(std::wstring text); // no onChange
    void setWidth(float width) noexcept { m_width = width; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::IBeam; }
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }

    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;
    bool onChar(wchar_t ch) override;
    void onFocusChanged(bool focused) override;

private:
    [[nodiscard]] RectF textRect() const;
    [[nodiscard]] RectF clearRect() const;
    [[nodiscard]] std::size_t indexAt(float x) const;
    [[nodiscard]] float offsetOf(std::size_t index) const; // text-space x of a caret index
    [[nodiscard]] bool hasSelection() const noexcept { return m_anchor != m_caret; }
    void replaceSelection(std::wstring_view with);
    void moveCaret(std::size_t to, bool extend);
    void changed();
    void ensureCaretVisible();

    std::wstring m_placeholder;
    std::vector<std::wstring> m_hintKeys;
    std::wstring m_text;
    std::size_t m_caret = 0;
    std::size_t m_anchor = 0;
    float m_scroll = 0; // horizontal text scroll so the caret stays visible
    float m_width = 240.0f;
    bool m_dragging = false;
    bool m_plain = false;
};

} // namespace wl::ui
