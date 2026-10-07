#include "app/pages/programs/BundleStrip.h"

#include "ui/render/Canvas.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

// A card of its own: the host's hover, press, focus and tooltip come with it.
class BundleStrip::CardView : public ui::Widget {
public:
    explicit CardView(std::function<void()> picked) : m_picked(std::move(picked)) {
        setFocusable(true);
    }

    void set(const Card& card) {
        m_card = card;
        setTooltip(card.description);
        setAccessible(ui::AccessRole::Button, card.title + L" — " + card.caption);
        invalidate();
    }

    void paint(ui::Canvas& canvas) override {
        const RectF r = bounds();
        const bool all = m_card.state == State::All;
        const bool hot = hovered() || pressed();
        canvas.fillRoundRect(r, ui::tokens::radius::r3, all ? Color::AccentSubtle : (hot ? Color::BgRaised : Color::BgPanel));
        canvas.strokeRoundRect(r, ui::tokens::radius::r3, all ? Color::AccentBase : (hot ? Color::LineStrong : Color::LineSubtle));
        canvas.drawIcon(m_card.icon, {r.x + 12, r.y + (r.height - 16) / 2}, all ? Color::AccentBase : Color::TextSecondary);
        const float x = r.x + 38;
        const float right = r.right() - (m_card.state == State::None ? 12.0f : 30.0f);
        canvas.drawText(m_card.title, {x, r.y + 6, std::max(right - x, 0.0f), 16}, TypeStyle::BodyStrong, Color::TextPrimary);
        canvas.drawText(m_card.caption, {x, r.y + 22, std::max(right - x, 0.0f), 16}, TypeStyle::Caption,
                        m_card.state == State::None ? Color::TextTertiary : Color::AccentBase);
        if (all) {
            canvas.drawIcon(ui::icons::Icon::Check, {r.right() - 26, r.y + (r.height - 16) / 2}, Color::AccentBase);
        } else if (m_card.state == State::Some) {
            canvas.fillRoundRect({r.right() - 20, r.y + (r.height - 6) / 2, 6, 6}, 3, Color::AccentBase);
        }
    }

    void onClick() override { m_picked(); }
    bool onKeyDown(const ui::KeyEvent& key) override {
        if (key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN) {
            m_picked();
            return true;
        }
        return false;
    }
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r3; }
    [[nodiscard]] ui::Cursor cursor() const override { return ui::Cursor::Hand; }

private:
    std::function<void()> m_picked;
    Card m_card;
};

BundleStrip::BundleStrip() {
    setAccessible(ui::AccessRole::Group, L"");
}

void BundleStrip::setCards(const std::vector<Card>& cards) {
    while (m_cards.size() < cards.size()) {
        const int index = static_cast<int>(m_cards.size());
        m_cards.push_back(&add<CardView>([this, index] {
            if (onPick) {
                onPick(index);
            }
        }));
    }
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setVisible(i < cards.size());
        if (i < cards.size()) {
            m_cards[i]->set(cards[i]);
        }
    }
    layout();
}

int BundleStrip::shown() const {
    return static_cast<int>(std::ranges::count_if(m_cards, [](const CardView* c) { return c->visible(); }));
}

int BundleStrip::perRow(float width) const {
    const int n = shown();
    if (n == 0) {
        return 1;
    }
    // One row when every card keeps its minimum width; else as many rows as it takes, evenly.
    const int fit = std::max(1, static_cast<int>((width + kGap) / (kMinWidth + kGap)));
    if (fit >= n) {
        return n;
    }
    const int rows = (n + fit - 1) / fit;
    return (n + rows - 1) / rows;
}

float BundleStrip::heightFor(float width) const {
    const int n = shown();
    if (n == 0) {
        return 0;
    }
    const int rows = (n + perRow(width) - 1) / perRow(width);
    return static_cast<float>(rows) * kCard + static_cast<float>(rows - 1) * kGap;
}

void BundleStrip::layout() {
    const RectF b = bounds();
    const int per = perRow(b.width);
    const float width = (b.width - kGap * static_cast<float>(per - 1)) / static_cast<float>(per);
    int index = 0;
    for (CardView* card : m_cards) {
        if (!card->visible()) {
            continue;
        }
        const int row = index / per;
        const int column = index % per;
        const float x = std::floor(b.x + static_cast<float>(column) * (width + kGap));
        const float right = column == per - 1 ? b.right() : std::floor(b.x + static_cast<float>(column + 1) * (width + kGap)) - kGap;
        card->setBounds({x, b.y + static_cast<float>(row) * (kCard + kGap), right - x, kCard});
        ++index;
    }
}

} // namespace wl::app
