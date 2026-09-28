#pragma once
// EmptyState (progress-skeleton-empty.md): centered; icon 24 text.tertiary, 16 gap, bodyStrong
// title, caption body (text.secondary), 12 gap, optional secondary button. Height <= 140.
#include "ui/widgets/Button.h"

#include <string>

namespace wl::ui {

class EmptyState : public Widget {
public:
    EmptyState(icons::Icon icon, std::wstring title, std::wstring body);

    // Adds the secondary action button (or relabels the existing one); returns it so the caller
    // can wire onInvoke.
    Button& setAction(std::wstring label);
    // In-place content change (safe to call from the action's own onInvoke).
    void setContent(icons::Icon icon, std::wstring title, std::wstring body);
    void hideAction();

    void layout() override;
    void paint(Canvas& canvas) override;

private:
    [[nodiscard]] float contentTop() const;

    icons::Icon m_icon;
    std::wstring m_title;
    std::wstring m_body;
    Button* m_action = nullptr;
};

} // namespace wl::ui
