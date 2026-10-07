#pragma once
// D-078: the bundles above the Programlar list — one card each (icon, name; "9 program" or "4 / 9
// seçili"), in one row, more when the row is narrow. A click (or Space / Enter) picks the bundle's
// programs, or takes them out when all are picked: the card then has the accent frame and a
// check. The card's tooltip is the bundle's description.
#include "ui/generated/Icons.g.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class BundleStrip : public ui::Widget {
public:
    enum class State : std::uint8_t { None, Some, All };
    struct Card {
        ui::icons::Icon icon = ui::icons::Icon::Download;
        std::wstring title;
        std::wstring caption;
        std::wstring description;
        State state = State::None;
    };
    static constexpr float kCard = 44.0f;
    static constexpr float kGap = 8.0f;
    static constexpr float kMinWidth = 148.0f;

    BundleStrip();

    std::function<void(int card)> onPick;

    void setCards(const std::vector<Card>& cards);
    [[nodiscard]] float heightFor(float width) const;

    void layout() override;

private:
    class CardView;
    [[nodiscard]] int shown() const;
    [[nodiscard]] int perRow(float width) const;

    std::vector<CardView*> m_cards;
};

} // namespace wl::app
