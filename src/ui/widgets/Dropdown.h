#pragma once
// ComboBox/Dropdown per 03_components/textbox.md: 24px, bg.input, 1px line.strong, label
// (text.secondary) + value (text.primary) + chevron-down (text.tertiary); open: border accent.
// The list is a Menu popup (contextmenu.md): bg.overlay, 1px line.strong, r3, elevation.menu,
// padding 4, items 24px with a check on the selected one. Keys: Enter/Space/Alt+↓ open,
// ↑↓ move, Enter picks, Esc closes.
#include "ui/widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

class Dropdown : public Widget {
public:
    Dropdown(std::wstring label, std::vector<std::wstring> items, int selected = 0);

    std::function<void(int)> onChange;

    void setItems(std::vector<std::wstring> items, int selected);
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;

    void open();
    void popupClosed() noexcept { m_open = false; invalidate(); }

private:
    std::wstring m_label;
    std::vector<std::wstring> m_items;
    int m_selected;
    bool m_open = false;
};

// The popup list; covers the window (Host::pushModal without scrim), closes on outside click.
class MenuPopup : public Widget {
public:
    MenuPopup(RectF anchor, std::vector<std::wstring> items, int selected, std::function<void(int)> picked,
              std::function<void()> closed);
    ~MenuPopup() override;

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] RectF focusRect() const override { return {}; } // no ring: the highlight shows focus
    [[nodiscard]] Widget* hitTest(PointF p) override;
    void onPointerMove(PointF p) override;
    void onPointerDown(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] int itemAt(PointF p) const;
    void close();
    void pick(int index);

    RectF m_anchor;
    RectF m_panel{};
    std::vector<std::wstring> m_items;
    int m_selected;
    int m_hover;
    std::function<void(int)> m_picked;
    std::function<void()> m_closed;
};

} // namespace wl::ui
