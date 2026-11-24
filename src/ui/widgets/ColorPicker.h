#pragma once
// D-143: a colour picker, custom drawn — a saturation × brightness field for the hue, a hue bar under it,
// the colour as #RRGGBB in a text box beside a swatch of the fill it gives, and a line saying when the
// accent rules changed that fill (Palette.h deriveAccent: darkened for white text, lightened to show on the
// dark card). Pointer: drag in the field or on the bar (each move previews, letting go commits). Keys: the
// arrows move the focused part's marker (Shift: ten steps), each press commits; the box previews what is
// typed once it is a colour and commits it with Enter.
// ColorPickerPopup puts one in a floating panel under a control (Ayarlar › Vurgu rengi): a click outside
// closes it as it is, Esc goes back to the colour it opened with.
#include "ui/theme/Palette.h"
#include "ui/widget/Widget.h"

#include <cstdint>
#include <functional>
#include <string>

namespace wl::ui {

class SearchBox;

class ColorPicker : public Widget {
public:
    struct Texts {
        std::wstring field;     // accessible name of the saturation / brightness field
        std::wstring hue;       // of the hue bar
        std::wstring hex;       // of the #RRGGBB box (also its placeholder)
        std::wstring sample;    // the text drawn on the swatch ("Aa")
        std::wstring darkened;  // the fill was darkened so that white text reads on it
        std::wstring lightened; // the dark themes' fill was lightened to show on the dark card
    };
    static constexpr float kWidth = 232.0f;
    static constexpr float kFieldHeight = 120.0f;
    static constexpr float kHueHeight = 12.0f;
    static constexpr float kHexWidth = 96.0f;

    explicit ColorPicker(Texts texts);
    std::function<void(std::uint32_t)> onPreview; // the colour while it is being picked (0xRRGGBB)
    std::function<void(std::uint32_t)> onCommit;  // the colour picked

    // No callbacks. The hue is kept where a grey (no hue of its own) would lose it.
    void setColor(std::uint32_t rgb);
    [[nodiscard]] std::uint32_t color() const noexcept { return m_rgb; }
    [[nodiscard]] Hsv hsv() const noexcept { return m_hsv; }
    // The note under the colour for `theme` (empty: nothing was changed).
    [[nodiscard]] const std::wstring& note(ThemeKind theme) const noexcept;
    [[nodiscard]] Widget& firstFocus() noexcept;

    [[nodiscard]] SizeF measure(SizeF available) override;
    void layout() override;
    void paint(Canvas& canvas) override;

    // Parts the picker is made of (the .cpp); they call back here.
    class Field;
    class HueBar;
    class HexBox;
    void change(Hsv hsv, bool commit);
    void typed(const std::wstring& text, bool commit);

private:
    void refreshDerived();

    Texts m_texts;
    Hsv m_hsv{};
    std::uint32_t m_rgb = 0;
    AccentDerivation m_derived{};
    Field* m_field = nullptr;
    HueBar* m_hue = nullptr;
    HexBox* m_box = nullptr;
    RectF m_swatch{};
    RectF m_note{};
};

class ColorPickerPopup : public Widget {
public:
    // `anchor`: the control it opens from (window DIPs); the panel goes under it, or above when there is
    // no room below.
    ColorPickerPopup(RectF anchor, ColorPicker::Texts texts, std::uint32_t rgb);
    ~ColorPickerPopup() override;

    std::function<void(std::uint32_t)> onPreview;
    std::function<void(std::uint32_t)> onCommit;
    std::function<void()> onCancel; // Esc: the owner puts the colour it opened with back
    std::function<void()> onClosed; // however it closed (also when the host tears it down)

    [[nodiscard]] ColorPicker& picker() noexcept { return *m_picker; }
    [[nodiscard]] RectF panel() const noexcept { return m_panel; }
    void close();
    // Its owner is going away: no callback may reach it any more (it closes the popup next).
    void detach() noexcept;

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] RectF focusRect() const override { return {}; }
    [[nodiscard]] Widget* hitTest(PointF p) override;
    void onPointerDown(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;
    bool onContextMenu(PointF p) override;

private:
    RectF m_anchor;
    RectF m_panel{};
    ColorPicker* m_picker = nullptr;
};

} // namespace wl::ui
