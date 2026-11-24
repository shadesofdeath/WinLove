#pragma once
// Scrolling label / control form: section titles, then rows without lines — the label column
// (Body), the control, an optional caption hint right of it; a changed row carries a 2px accent
// bar at its left edge. Controls are children created through addRow(); titles, labels and hints
// are painted. Taller than the view: wheel, overlay scroll bar, and the focused control is scrolled in.
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wl::ui {

class FormView : public Widget {
public:
    // D-114 (user: "biraz fazla sıkışık, daha modern"): each section's rows on a rounded card with
    // hairlines between them, the section title above its card. D-136: the metrics follow the
    // tokens — a control of size::control with a spacing step above and below it, a title block of
    // the Section style's line with room above (between two cards) and under it (to its card).
    static constexpr float kControl = tokens::size::control;
    static constexpr float kRow = kControl + 2 * tokens::spacing::s3;
    static constexpr float kSectionLine = tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Section)].lineHeight;
    static constexpr float kSectionAbove = tokens::spacing::s7;
    static constexpr float kSectionBelow = tokens::spacing::s4;
    static constexpr float kSection = kSectionAbove + kSectionLine + kSectionBelow;

    // The one section title of every form and page (FormView's own, the ISO / USB form, Languages,
    // Kaynak, Simgeler): the Section style in text.primary, its line box at the bottom of `area`
    // less kSectionBelow — `area` is a title block (usually kSection tall), the title sits on its card.
    static void paintSectionTitle(Canvas& canvas, RectF area, std::wstring_view title);

    explicit FormView(float labelWidth);

    // Removes every row and control. Not from inside one of the controls' own callbacks (the
    // control would be destroyed under its feet): rebuild from the outside (tab bar, page).
    void clear();
    void addSection(std::wstring title);
    // Creates the control as a child and appends its row. `width` 0 = the control's measure().
    template <class W, class... Args>
    W& addRow(std::wstring label, std::wstring hint, float width, Args&&... args) {
        auto& control = add<Revealing<W>>(std::forward<Args>(args)...);
        control.onFocused = [this, &control] { reveal(control); };
        appendRow(std::move(label), std::move(hint), width, &control);
        return control;
    }
    // `color`: text.tertiary for explanations, a status color for a problem with the value.
    void setHint(const Widget& control, std::wstring hint, tokens::Color color = tokens::Color::TextTertiary);
    // D-091: a row that will change something: accent bar at its left, label in text.primary.
    void setMarked(const Widget& control, bool marked);
    // A filtered-out row; a section with no row shown is hidden with it.
    void setRowVisible(const Widget& control, bool visible);
    // Where the hint of `control`'s row is drawn: right of the control up to the card's inner
    // padding (clear of the scroll bar), one line with "…" when longer. Width 0: no room.
    [[nodiscard]] RectF hintRect(const Widget& control) const;

    // Sections as scroll targets (step indicators).
    [[nodiscard]] int sectionCount() const noexcept;
    [[nodiscard]] int currentSection() const; // the one at the top of the view (or last scrolled to)
    void scrollToSection(int index);
    std::function<void()> onScroll;

    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    void layout() override;
    void paint(Canvas& canvas) override;
    bool onWheel(PointF p, float lines) override;
    // D-136: a label cut to "…" (a narrow window, a long label) shows whole as its tooltip.
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    [[nodiscard]] RectF tooltipAnchor() const override;

private:
    // Tells the form when the control takes the keyboard focus.
    template <class W>
    class Revealing : public W {
    public:
        using W::W;
        std::function<void()> onFocused;
        void onFocusChanged(bool focused) override {
            W::onFocusChanged(focused);
            if (focused && onFocused) {
                onFocused();
            }
        }
    };
    struct Row {
        std::wstring label; // a section title when `control` is null
        std::wstring hint;
        Widget* control = nullptr;
        float width = 0;
        tokens::Color hintColor = tokens::Color::TextTertiary;
        bool marked = false;
        bool hidden = false; // a section: computed from its rows (shownRows)
    };
    void updateSections();
    [[nodiscard]] float cardWidth() const; // the section cards: the view less the scroll bar's lane
    [[nodiscard]] RectF hintBox(const Widget& control, float rowY) const;

    void appendRow(std::wstring label, std::wstring hint, float width, Widget* control);
    void reveal(const Widget& control);
    void setOffset(float offset, bool pinned);
    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] float sectionTop(int index) const; // content-space y, -1 when out of range

    [[nodiscard]] float labelRoom() const noexcept;
    float m_labelWidth;
    std::vector<Row> m_rows;
    int m_tipRow = -1;   // the row whose cut label is the tooltip
    RectF m_tipRect{};   // where that label is
    ScrollBar* m_scroll = nullptr;
    float m_offset = 0;
    int m_pinned = -1; // section scrollToSection() asked for; kept until the user scrolls
    bool m_scrolling = false; // layout() from setOffset(): rows move, their widths stay
};

} // namespace wl::ui
