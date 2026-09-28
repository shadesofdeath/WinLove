#pragma once
// Page frame (screens.md "Ortak iskelet"): 16px side padding; header = title 16/22 semibold,
// 2px, description caption text.secondary; header action buttons right-aligned (y 49, 4 gap).
// The body starts right under the description; each page adds its own top spacing (toolbar
// pages 12, the Source page 20 — per the handoff screens).
#include "ui/widgets/Button.h"

#include <string>
#include <vector>

namespace wl::app {

class PageView : public ui::Widget {
public:
    PageView(std::wstring title, std::wstring description);

    // Takes ownership; the body fills the area under the header.
    template <class T, class... Args>
    T& setBody(Args&&... args) {
        if (m_body) {
            removeChild(m_body);
        }
        T& body = add<T>(std::forward<Args>(args)...);
        m_body = &body;
        layout();
        return body;
    }

    // Live header text (e.g. "Uygulanıyor · 3 / 8 işlem · 42%").
    void setHeader(std::wstring title, std::wstring description);

    // Header buttons, added left to right; the group is right-aligned.
    ui::Button& addAction(ui::ButtonKind kind, std::wstring label, std::optional<ui::icons::Icon> icon = std::nullopt);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    [[nodiscard]] float headerHeight() const;

    std::wstring m_title;
    std::wstring m_description;
    ui::Widget* m_body = nullptr;
    std::vector<ui::Button*> m_actions;
};

} // namespace wl::app
