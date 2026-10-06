#pragma once
// Dialog per infobar-toast-dialog.md: shown through Host::pushModal (scrim + focus trap).
// Box: bg.overlay, 1px line.strong, r3, elevation.dialog, padding 16; optional 16px icon, title
// (bodyStrong), wrapped body (text.secondary), buttons bottom-right (secondary cancel + primary).
// Esc = cancel, Enter = primary, click on the scrim = cancel.
#include "ui/widgets/Button.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::ui {

class Dialog : public Widget {
public:
    Dialog(std::wstring title, std::wstring body, std::optional<icons::Icon> icon = std::nullopt,
           tokens::Color iconColor = tokens::Color::StatusWarning, float width = 480.0f);

    // Buttons appear left to right in the order added; the primary one answers Enter.
    Button& addButton(ButtonKind kind, std::wstring label, std::function<void()> onInvoke, bool primary = false);
    // Custom content between the body and the buttons (e.g. a list + a confirmation checkbox),
    // `height` tall (less when the window is too short: the content then scrolls itself), full
    // content width.
    template <class T, class... Args>
    T& setContent(float height, Args&&... args) {
        T& ref = add<T>(std::forward<Args>(args)...);
        m_content = &ref;
        m_contentHeight = height;
        layout();
        return ref;
    }
    std::function<void()> onCancel;


    void layout() override;
    void paint(Canvas& canvas) override;
    bool onKeyDown(const KeyEvent& key) override;
    void onPointerUp(PointF p) override;
    void onClick() override;

private:
    std::wstring m_title;
    std::wstring m_body;
    std::optional<icons::Icon> m_icon;
    tokens::Color m_iconColor;
    float m_width;
    RectF m_box{};
    float m_bodyHeight = 0;
    std::vector<Button*> m_buttons;
    Widget* m_content = nullptr;
    float m_contentHeight = 0;
    Button* m_primary = nullptr;
    PointF m_lastUp{};
};

} // namespace wl::ui
