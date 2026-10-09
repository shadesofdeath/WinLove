#pragma once
// Toast per infobar-toast-dialog.md: 360×48, bottom-right 16px above the status bar; bg.overlay,
// 1px line.strong, r3, elevation.toast; status icon 16 + bodyStrong title + caption. One at a time
// (a new toast replaces the old). The owner hides it after 4 s (and keeps it while hovered).
// D-091: a 3px stripe in the status color, a short rise-in, and an optional action at the right
// ("Geri al") that runs on click and closes the toast.
#include "ui/anim/Tween.h"
#include "ui/widgets/InfoBar.h"

#include <functional>
#include <string>

namespace wl::ui {

class Toast : public Widget {
public:
    static constexpr float kWidth = 360.0f;
    static constexpr float kHeight = 48.0f;
    static constexpr unsigned kDurationMs = 4000;

    Toast();
    void show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action = {},
              std::function<void()> onAction = {});
    void hide();

    void paint(Canvas& canvas) override;
    void onHoverChanged(bool hovered) override;
    void onPointerMove(PointF p) override;
    void onClick() override;
    bool tick(double now) override;
    [[nodiscard]] float paintOpacity() const override { return m_enter.value(); }
    [[nodiscard]] Cursor cursor() const override { return m_overAction ? Cursor::Hand : Cursor::Arrow; }
    [[nodiscard]] bool hoveredNow() const noexcept { return hovered(); }

private:
    [[nodiscard]] RectF actionRect() const;
    InfoKind m_kind = InfoKind::Info;
    std::wstring m_title;
    std::wstring m_message;
    std::wstring m_action;
    std::function<void()> m_onAction;
    bool m_overAction = false;
    Tween m_enter{1.0f};
};

} // namespace wl::ui
