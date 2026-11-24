#pragma once
// The Source page's drop target as one row (2026-11-10, the owner's request; the reference tool's
// zone took a third of the page): a card-like box (line.strong, r3) with the download icon, the
// sentence and, after it in the quiet style, the formats it takes. Under the pointer its border
// lifts and bg.hover fades in. While something is dragged over the window it says what a drop
// would do — valid: accent border on accent.subtle, accent icon; not a source: status.error border
// on its subtle fill, the error icon and "Desteklenmeyen dosya" (the formats stay, so the user sees
// what would do). Opening: "Açılıyor…". Click / Space / Enter opens the file picker. The texts are
// measured in layout(), never while painting.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"
#include "ui/widgets/DropZone.h" // DragState: the Shell's drag states, shared by every drop target

#include <functional>
#include <string>

namespace wl::app {

class DropRow : public ui::Widget {
public:
    using DragState = ui::DropZone::DragState;
    // A 24 control's height with a spacing step above and below.
    static constexpr float kHeight = ui::tokens::size::control + 2 * ui::tokens::spacing::s4;

    DropRow(std::wstring title, std::wstring formats, std::wstring unsupported, std::wstring loading);
    std::function<void()> onInvoke;

    void setDragState(DragState state);
    void setLoading(bool loading);
    [[nodiscard]] DragState dragState() const noexcept { return m_drag; }
    [[nodiscard]] bool loading() const noexcept { return m_loading; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] ui::Cursor cursor() const override { return ui::Cursor::Hand; }
    // The keyboard ring lies on the row's own border (the Host draws it focusOffset + 1 outside this):
    // at the top of the card's body an outside ring was cut by the body's clip.
    [[nodiscard]] ui::RectF focusRect() const override {
        const float ring = ui::tokens::radius::focusOffset + 1;
        return bounds().inset(ring, ring);
    }
    [[nodiscard]] float focusRadius() const override {
        return ui::tokens::radius::r3 - ui::tokens::radius::focusOffset - 1;
    }
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool tick(double now) override;

private:
    std::wstring m_title;
    std::wstring m_formats;
    std::wstring m_unsupported;
    std::wstring m_opening;
    float m_titleWidth = 0; // layout(): each sentence's width
    float m_unsupportedWidth = 0;
    float m_openingWidth = 0;
    bool m_measured = false;
    DragState m_drag = DragState::None;
    bool m_loading = false;
    ui::Tween m_hover;
};

} // namespace wl::app
