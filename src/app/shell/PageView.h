#pragma once
// Page frame in the reference tool's layout (D-109): under the title bar a ribbon band (size::ribbon, bg.chrome)
// carries the page's actions left to right as ribbon buttons; under it the page's description is
// one centered hint line (size::hint, text.secondary). The rest is the page's card: the left
// column is kept for the shell's NavPanel, the right one for a side card (the page's inspector);
// the card has a header of tabs (size::tab: the page title, or the tab's pages as tabs) over the
// body (bg.panel, radius r3). The body is laid out 8 inside the card, in a viewport: a body that
// is taller than the room (ScrollableBody) scrolls in it.
#include "ui/widgets/Button.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>

#include <string>
#include <vector>

namespace wl::app {

// A page body that may be taller than the page: its height at the width it was last laid out at.
class ScrollableBody {
public:
    virtual ~ScrollableBody() = default;
    [[nodiscard]] virtual float contentHeight() const = 0;

protected:
    // At the end of the body's own layout(): when its height changed (another tab of the page, more
    // rows), the viewport lays it out again with the new scroll range.
    void contentChanged(ui::Widget& self, float before) const {
        if ((before - contentHeight() > 0.5f || contentHeight() - before > 0.5f) && self.parent()) {
            self.parent()->layout();
        }
    }
};

// Clips the body and scrolls it (wheel, overlay scroll bar) when it reports more height than fits.
class PageViewport : public ui::Widget {
public:
    PageViewport();
    void setBody(ui::Widget* body);
    void layout() override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    bool onWheel(ui::PointF p, float lines) override;

private:
    void setOffset(float offset);
    ui::Widget* m_body = nullptr;
    ui::ScrollBar* m_bar = nullptr;
    float m_offset = 0;
    float m_content = 0;
};

// The card's header: one plate per tab (bg.chrome on the chosen one, rounded at the top), text
// 7.5 in. One tab = the page's title; several = the pages of the nav item (Güncellemeler · Diller).
class CardTabs : public ui::Widget {
public:
    CardTabs();
    std::function<void(int)> onPick;
    void setTabs(std::vector<std::wstring> labels, int selected);
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] ui::Cursor cursor() const override;

private:
    [[nodiscard]] std::vector<ui::RectF> plates() const;
    std::vector<std::wstring> m_labels;
    int m_selected = 0;
    int m_hover = -1;
};

class PageView : public ui::Widget {
public:
    PageView(std::wstring title, std::wstring description);

    // Takes ownership; the body fills the card under its header.
    template <class T, class... Args>
    T& setBody(Args&&... args) {
        if (m_body) {
            m_viewport->removeChild(m_body);
        }
        T& body = m_viewport->add<T>(std::forward<Args>(args)...);
        m_body = &body;
        m_viewport->setBody(&body);
        layout();
        return body;
    }

    // Live header text (e.g. "Uygulanıyor · 3 / 8 işlem · 42%"): the card's tab says it.
    void setHeader(std::wstring title, std::wstring description);

    // The other pages of the nav item (Güncellemeler · Diller …) as the card's tabs.
    void setSubNav(std::vector<std::wstring> labels, int selected, std::function<void(int)> onPick);

    // Ribbon buttons, left to right.
    ui::Button& addAction(ui::ButtonKind kind, std::wstring label, std::optional<ui::icons::Icon> icon = std::nullopt);
    // A thin line between two groups of ribbon buttons.
    void addSeparator();

    // The shell's columns: `left` from the window's edge to the card (the nav and its margins),
    // `right` for a side card (0 = none).
    void setColumns(float left, float right);
    // Where the card starts (the nav and a side card line up with it).
    [[nodiscard]] float contentTop() const;
    [[nodiscard]] ui::RectF cardRect() const;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    // D-091: a page fades in when it is shown (motion.enter; at once with reduced motion).
    bool tick(double now) override;
    [[nodiscard]] float paintOpacity() const override { return m_enter.value(); }

private:
    ui::Tween m_enter{0.0f};
    void updateTabs();

    std::wstring m_title;
    std::wstring m_description;
    std::vector<std::wstring> m_subLabels;
    int m_subSelected = 0;
    std::function<void(int)> m_onSub;
    float m_left = 0;
    float m_right = 0;
    PageViewport* m_viewport = nullptr;
    CardTabs* m_tabs = nullptr;
    ui::Widget* m_body = nullptr;
    std::vector<ui::Button*> m_actions;
    std::vector<std::size_t> m_separators; // a line before m_actions[i]
    std::vector<float> m_separatorX;
};

} // namespace wl::app
