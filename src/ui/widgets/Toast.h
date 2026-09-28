#pragma once
// Toast per infobar-toast-dialog.md: 360×48, bottom-right 16px above the status bar; bg.overlay,
// 1px line.strong, r3, elevation.toast; status icon 16 + bodyStrong title + caption. One at a time
// (a new toast replaces the old). The owner hides it after 4 s (and keeps it while hovered).
#include "ui/widgets/InfoBar.h"

#include <string>

namespace wl::ui {

class Toast : public Widget {
public:
    static constexpr float kWidth = 360.0f;
    static constexpr float kHeight = 48.0f;
    static constexpr unsigned kDurationMs = 4000;

    Toast();
    void show(InfoKind kind, std::wstring title, std::wstring message);
    void hide();

    void paint(Canvas& canvas) override;
    void onHoverChanged(bool hovered) override;
    [[nodiscard]] bool hoveredNow() const noexcept { return m_hovered; }

private:
    InfoKind m_kind = InfoKind::Info;
    std::wstring m_title;
    std::wstring m_message;
    bool m_hovered = false;
};

} // namespace wl::ui
