#pragma once
// Scrolling label / control form (screens 10, 11): section titles in caps with a rule under
// them, then 32px rows — the label column, the control, an optional caption hint right of it.
// Controls are children created through addRow(); titles, labels and hints are painted.
// Taller than the view: wheel, overlay scroll bar, and the focused control is scrolled in.
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wl::ui {

class FormView : public Widget {
public:
    static constexpr float kSection = 32.0f;
    static constexpr float kRow = 32.0f;

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
    void setHint(const Widget& control, std::wstring hint);

    // Sections as scroll targets (step indicators).
    [[nodiscard]] int sectionCount() const noexcept;
    [[nodiscard]] int currentSection() const; // the one at the top of the view (or last scrolled to)
    void scrollToSection(int index);
    std::function<void()> onScroll;

    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    void layout() override;
    void paint(Canvas& canvas) override;
    bool onWheel(PointF p, float lines) override;

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
    };

    void appendRow(std::wstring label, std::wstring hint, float width, Widget* control);
    void reveal(const Widget& control);
    void setOffset(float offset, bool pinned);
    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] float sectionTop(int index) const; // content-space y, -1 when out of range

    float m_labelWidth;
    std::vector<Row> m_rows;
    ScrollBar* m_scroll = nullptr;
    float m_offset = 0;
    int m_pinned = -1; // section scrollToSection() asked for; kept until the user scrolls
};

} // namespace wl::ui
