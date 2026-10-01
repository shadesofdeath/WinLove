#pragma once
// Linear layout: children one after another on the main axis with a gap, inside padding.
// Each child has a sizing rule on the main axis (fixed DIPs, its own measure(), or a share of the
// remaining space) and an alignment on the cross axis.
#include "ui/widget/Widget.h"

#include <vector>

namespace wl::ui {

enum class Axis : std::uint8_t { Horizontal, Vertical };
enum class CrossAlign : std::uint8_t { Stretch, Start, Center, End };

struct Sizing {
    enum class Mode : std::uint8_t { Fixed, Auto, Fill };
    Mode mode = Mode::Auto;
    float value = 0; // Fixed: DIPs, Fill: weight

    static Sizing fixed(float dips) { return {Mode::Fixed, dips}; }
    static Sizing autoSize() { return {Mode::Auto, 0}; }
    static Sizing fill(float weight = 1) { return {Mode::Fill, weight}; }
};

struct Insets {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
    static Insets xy(float x, float y) { return {x, y, x, y}; }
};

class Stack : public Widget {
public:
    explicit Stack(Axis axis, float gap = 0, Insets padding = {}) : m_axis(axis), m_gap(gap), m_padding(padding) {}

    template <class T, class... Args>
    T& addItem(Sizing sizing, CrossAlign align, Args&&... args) {
        T& child = add<T>(std::forward<Args>(args)...);
        m_items.push_back({sizing, align});
        return child;
    }
    template <class T, class... Args>
    T& addItem(Sizing sizing, Args&&... args) {
        return addItem<T>(sizing, CrossAlign::Stretch, std::forward<Args>(args)...);
    }

    // Adopt an already constructed widget (e.g. from a factory like Button::iconOnly).
    Widget& addItem(Sizing sizing, CrossAlign align, std::unique_ptr<Widget> child) {
        Widget& ref = *child;
        addChild(std::move(child));
        m_items.push_back({sizing, align});
        return ref;
    }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void layout() override;

private:
    struct Item {
        Sizing sizing;
        CrossAlign align;
    };
    [[nodiscard]] float mainOf(SizeF s) const { return m_axis == Axis::Horizontal ? s.width : s.height; }
    [[nodiscard]] float crossOf(SizeF s) const { return m_axis == Axis::Horizontal ? s.height : s.width; }

    Axis m_axis;
    float m_gap;
    Insets m_padding;
    std::vector<Item> m_items;
};

} // namespace wl::ui
