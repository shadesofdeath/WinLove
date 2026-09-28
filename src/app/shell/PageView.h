#pragma once
// Page frame (screens.md "Ortak iskelet"): 16px padding, title 16/22 semibold, 2px, description
// caption text.secondary, then the page body. Header actions and toolbar arrive with the pages.
#include "ui/widget/Widget.h"

#include <string>

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

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    [[nodiscard]] float headerHeight() const;

    std::wstring m_title;
    std::wstring m_description;
    ui::Widget* m_body = nullptr;
};

} // namespace wl::app
