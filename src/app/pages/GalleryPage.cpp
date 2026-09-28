#include "app/pages/GalleryPage.h"

#include "ui/widgets/Button.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Kbd.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/Splitter.h"

namespace wl::app {

using namespace ui;

namespace {

constexpr float kLabelColumn = 120.0f;
constexpr float kGap = 8.0f;

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

    auto& empty = addItem<EmptyState>(Sizing::fixed(140), icons::Icon::DiscIso, L"EmptyState title",
                                      L"Caption body text explains what to do next.");
    empty.setAction(L"Secondary action");
}

} // namespace wl::app
