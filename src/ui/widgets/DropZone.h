#pragma once
// DropZone per log-console-inspector-diff-palette.md: 1px line.strong, r2, centered 24 icon
// (download, text.tertiary) + bodyStrong title + caption hint. While a drag is over the window:
// valid → accent border, accent.subtle fill, accent icon; invalid → status.error border and the
// "unsupported" text. Clicking (or Space/Enter) invokes the file picker.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>

namespace wl::ui {

class DropZone : public Widget {
public:
    enum class DragState : std::uint8_t { None, Valid, Invalid };

    DropZone(std::wstring title, std::wstring hint, std::wstring unsupported, std::wstring loading);
    std::function<void()> onInvoke;

    void setDragState(DragState state);
    void setLoading(bool loading);
    // 56px horizontal variant (updates, drivers): 16px icon + one line of text, centred.
    void setCompact(bool compact) noexcept { m_compact = compact; }

    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;

private:
    std::wstring m_title;
    std::wstring m_hint;
    std::wstring m_unsupported;
    std::wstring m_loading;
    DragState m_drag = DragState::None;
    bool m_isLoading = false;
    bool m_compact = false;
    Tween m_hover;
};

} // namespace wl::ui
