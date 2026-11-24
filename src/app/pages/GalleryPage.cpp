#include "app/pages/GalleryPage.h"

#include "ui/widgets/Button.h"
#include "ui/widgets/ColorPicker.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Kbd.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/ProgressRing.h"
#include "ui/widgets/Splitter.h"

#include <format>

namespace wl::app {

using namespace ui;

namespace {

constexpr float kLabelColumn = 120.0f;
constexpr float kGap = 10.0f;

struct State {
    const wchar_t* name;
    bool hover;
    bool press;
    bool enabled;
};
constexpr State kStates[] = {
    {L"rest", false, false, true},
    {L"hover", true, false, true},
    {L"pressed", true, true, true},
    {L"disabled", false, false, false},
};

Stack& row(Stack& page, const wchar_t* title) {
    auto& r = page.addItem<Stack>(Sizing::fixed(tokens::size::control), Axis::Horizontal, kGap);
    r.addItem<Label>(Sizing::fixed(kLabelColumn), title, tokens::TypeStyle::Caption, tokens::Color::TextSecondary);
    return r;
}

void buttonRow(Stack& page, const wchar_t* title, ButtonKind kind, std::optional<icons::Icon> icon = std::nullopt) {
    auto& r = row(page, title);
    for (const auto& state : kStates) {
        auto& b = r.addItem<Button>(Sizing::autoSize(), CrossAlign::Center, kind, state.name, icon);
        b.forceVisualState(state.hover, state.press);
        b.setEnabled(state.enabled);
    }
}

// Every icon of the set with its index (Icons.g.h order): to pick one without opening the handoff.
class IconGrid final : public Widget {
public:
    void paint(Canvas& canvas) override {
        const RectF b = bounds();
        constexpr float kCell = 44.0f;
        const int columns = std::max(1, static_cast<int>(b.width / kCell));
        for (std::size_t i = 0; i < icons::kIconCount; ++i) {
            const float x = b.x + static_cast<float>(static_cast<int>(i) % columns) * kCell;
            const float y = b.y + static_cast<float>(static_cast<int>(i) / columns) * kCell;
            canvas.drawIcon(static_cast<icons::Icon>(i), {x + 12.0f, y + 2.0f}, tokens::Color::TextPrimary);
            canvas.drawText(std::format(L"{}", i), {x, y + 20.0f, kCell - 4.0f, 14.0f}, tokens::TypeStyle::Caption,
                            tokens::Color::TextTertiary, TextAlign::Center);
        }
    }
};

} // namespace

GalleryPage::GalleryPage() : Stack(Axis::Vertical, 12.0f) {
    buttonRow(*this, L"Primary", ButtonKind::Primary);
    buttonRow(*this, L"Secondary", ButtonKind::Secondary);
    buttonRow(*this, L"Subtle", ButtonKind::Subtle);
    buttonRow(*this, L"Danger", ButtonKind::Danger);
    buttonRow(*this, L"With icon", ButtonKind::Secondary, icons::Icon::OpenFolder);

    auto& iconRow = row(*this, L"Icon-only");
    for (const auto& state : kStates) {
        auto button = Button::iconOnly(icons::Icon::Refresh, state.name);
        button->forceVisualState(state.hover, state.press);
        button->setEnabled(state.enabled);
        iconRow.addItem(Sizing::autoSize(), CrossAlign::Center, std::move(button));
    }

    auto& kbdRow = row(*this, L"Kbd");
    kbdRow.addItem<Kbd>(Sizing::autoSize(), CrossAlign::Center, std::vector<std::wstring>{L"Ctrl", L"K"});
    kbdRow.addItem<Kbd>(Sizing::autoSize(), CrossAlign::Center, std::vector<std::wstring>{L"Ctrl", L"Shift", L"T"});

    auto& typeRow = row(*this, L"Type");
    typeRow.addItem<Label>(Sizing::autoSize(), L"Title 16", tokens::TypeStyle::Title);
    typeRow.addItem<Label>(Sizing::autoSize(), L"Body strong 12", tokens::TypeStyle::BodyStrong);
    typeRow.addItem<Label>(Sizing::autoSize(), L"Body 12 — ğüşıöç İĞÜŞ", tokens::TypeStyle::Body);
    typeRow.addItem<Label>(Sizing::autoSize(), L"Caption 11", tokens::TypeStyle::Caption, tokens::Color::TextSecondary);
    typeRow.addItem<Label>(Sizing::autoSize(), L"Section", tokens::TypeStyle::Section, tokens::Color::TextTertiary);
    typeRow.addItem<Label>(Sizing::autoSize(), L"mono 26100.1742", tokens::TypeStyle::Mono);

    auto& splitRow = row(*this, L"Splitter");
    splitRow.addItem<Splitter>(Sizing::fixed(tokens::size::splitterHit));

    // D-142: indeterminate 16 / 20, then determinate 0 / 25 / 60 / 100 %.
    auto& ringRow = row(*this, L"ProgressRing");
    for (const float size : {tokens::size::ring, tokens::size::ringLarge}) {
        ringRow.addItem<ProgressRing>(Sizing::autoSize(), CrossAlign::Center, size).setRunning(true);
    }
    for (const float value : {0.0f, 0.25f, 0.6f, 1.0f}) {
        ringRow.addItem<ProgressRing>(Sizing::autoSize(), CrossAlign::Center, tokens::size::ringLarge).setValue(value);
    }

    // D-143: the accent colour picker (in Ayarlar it opens in a panel under the swatches).
    auto& pickerRow = addItem<Stack>(Sizing::autoSize(), Axis::Horizontal, kGap);
    pickerRow.addItem<Label>(Sizing::fixed(kLabelColumn), CrossAlign::Start, L"ColorPicker", tokens::TypeStyle::Caption,
                             tokens::Color::TextSecondary);
    pickerRow.addItem<ColorPicker>(Sizing::fixed(ColorPicker::kWidth), CrossAlign::Start,
                                   ColorPicker::Texts{L"Saturation and brightness", L"Hue", L"#RRGGBB", L"Aa",
                                                      L"Darkened for white text.", L"Lightened for the dark themes."})
        .setColor(0xF5E663);

    auto& empty = addItem<EmptyState>(Sizing::fixed(140), icons::Icon::DiscIso, L"EmptyState title",
                                      L"Caption body text explains what to do next.");
    empty.setAction(L"Secondary action");
    addItem<IconGrid>(Sizing::fixed(300));
}

} // namespace wl::app
