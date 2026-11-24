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
    static constexpr float kWidth = 420.0f; // D-107 / D-108; 2026-11-09: wider, and the text wraps
    static constexpr float kHeight = 54.0f; // the least: one line of title, one of message
    static constexpr float kMaxHeight = 220.0f;

    // The height its title and message need at `width` (they wrap; the toast grows upwards).
    [[nodiscard]] float preferredHeight(float width) const;
    static constexpr unsigned kDurationMs = 4000;

    Toast();
    void show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action = {},
              std::function<void()> onAction = {});
    // D-145: two actions ("Yükle" · "Değişiklikleri gör"): a row of their own under the text, the
    // first at the right and filled (the text keeps the toast's whole width).
    void show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action, std::function<void()> onAction,
              std::wstring second, std::function<void()> onSecond);
    void hide();

    void paint(Canvas& canvas) override;
    void onHoverChanged(bool hovered) override;
    void onPointerMove(PointF p) override;
    void onClick() override;
    bool tick(double now) override;
    [[nodiscard]] float paintOpacity() const override { return m_enter.value(); }
    [[nodiscard]] Cursor cursor() const override { return m_overAction || m_overSecond ? Cursor::Hand : Cursor::Arrow; }
    [[nodiscard]] bool hoveredNow() const noexcept { return hovered(); }

private:
    [[nodiscard]] RectF actionRect() const;
    [[nodiscard]] RectF secondRect() const; // D-145; empty without a second action
    [[nodiscard]] bool stacked() const noexcept { return !m_second.empty(); }
    InfoKind m_kind = InfoKind::Info;
    std::wstring m_title;
    std::wstring m_message;
    std::wstring m_action;
    std::function<void()> m_onAction;
    std::wstring m_second;
    std::function<void()> m_onSecond;
    bool m_overAction = false;
    bool m_overSecond = false;
    Tween m_enter{1.0f};
};

} // namespace wl::ui
