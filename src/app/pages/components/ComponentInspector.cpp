#include "app/pages/components/ComponentInspector.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "ui/widget/Host.h"

#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// D-107: the package's side panel — 24 padding, the name in Title over the identity (mono),
// key / value rows in Small (27 apart, 118 key column), sections in caps under a line, a note with a
// 2px warning line on its left, the queue button full width (40) at the foot.
constexpr float kPadding = ui::tokens::size::panelPad;
constexpr float kKeyWidth = 104.0f;
constexpr float kRow = 23.0f;
constexpr float kLine = 18.0f;
constexpr float kButton = ui::tokens::size::controlLarge;
} // namespace

ui::icons::Icon ComponentInspector::iconOf(ComponentController::Item::Kind kind) noexcept {
    switch (kind) {
    case ComponentController::Item::Kind::System: return ui::icons::Icon::WindowsLogoGeneric;
    case ComponentController::Item::Kind::Cleanup:
    case ComponentController::Item::Kind::Shrink: return ui::icons::Icon::SizeSaved;
    case ComponentController::Item::Kind::Appx: break;
    }
    return ui::icons::Icon::AppxPackage;
}

ComponentInspector::ComponentInspector(const Localization& strings, Language language)
    : m_strings(strings), m_language(language) {
    m_toggle = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::ComponentsAddToQueue));
    m_toggle->setHeight(kButton);
    m_toggle->onInvoke = [this] {
        if (onToggle) {
            onToggle();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void ComponentInspector::set(std::optional<ComponentController::Item> item, std::wstring group, bool queued, std::wstring held) {
    m_item = std::move(item);
    m_group = std::move(group);
    m_queued = queued;
    m_held = std::move(held);
    m_toggle->setText(m_strings.get(queued ? Str::ComponentsRemoveFromQueue : Str::ComponentsAddToQueue));
    m_toggle->setVisible(m_item.has_value());
    m_toggle->setEnabled(m_held.empty());
    layout();
    invalidate();
}

void ComponentInspector::layout() {
    const RectF b = bounds();
    m_toggle->setBounds({b.x + kPadding, b.bottom() - kPadding - kButton, std::max(b.width - 2 * kPadding, 0.0f), kButton});
}

void ComponentInspector::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    // D-114: a card like the others (rounded, no seam), not a strip glued to the window edge.
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgPanel);
    if (!m_item) {
        return;
    }
    const auto& item = *m_item;
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;
    canvas.drawText(item.name, {x, y, width, 22}, TypeStyle::Title, Color::TextPrimary);
    canvas.drawText(item.identity, {x, y + 24, width, 16}, TypeStyle::Mono, Color::TextTertiary);

    y += 52;
    auto row = [&](Str key, const std::wstring& value, TypeStyle style = TypeStyle::Small,
                   std::optional<Color> dot = std::nullopt) {
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth - 8, kRow}, TypeStyle::Small, Color::TextTertiary);
        float vx = x + kKeyWidth;
        if (dot) {
            canvas.fillEllipse({vx + 3, y + kRow / 2}, 3.0f, *dot);
            vx += 13;
        }
        canvas.drawText(value, {vx, y, b.right() - kPadding - vx, kRow}, style, Color::TextPrimary);
        y += kRow;
    };
    auto section = [&](const std::wstring& title) {
        y += 9;
        canvas.hairlineH(x, y, width, Color::LineSubtle);
        y += 12;
        canvas.drawText(title, {x, y, width, 18}, TypeStyle::Section, Color::TextTertiary);
        y += 26;
    };
    row(Str::ComponentsCategory, m_group);
    row(Str::CommonSize, item.size ? formatBytes(item.size, m_language) : std::wstring(L"—"), TypeStyle::Mono);
    row(Str::RiskColumn, m_strings.get(riskText(item.risk)), TypeStyle::Small, riskInk(item.risk));
    row(Str::CommonStatus, m_strings.get(m_queued ? Str::ComponentsQueued : Str::ComponentsStays));
    row(Str::ComponentsReversible, m_strings.get(Str::CommonNo));

    const std::wstring& notes = item.notes;
    if (!m_held.empty() || !notes.empty()) {
        section(m_strings.get(Str::ComponentsCompat));
    }
    if (!m_held.empty()) {
        const float h = std::ceil(canvas.text().measureWrapped(m_held, TypeStyle::Small, width - 22));
        canvas.drawIcon(ui::icons::Icon::Lock, {x, y + 1}, Color::TextSecondary);
        canvas.drawTextWrapped(m_held, {x + 22, y, width - 22, h}, TypeStyle::Small, Color::TextPrimary);
        y += h + 10;
    }
    if (!notes.empty()) {
        // The package's note: a 2px line on the left, the text in the risk's color.
        const float h = std::ceil(canvas.text().measureWrapped(notes, TypeStyle::Small, width - 14));
        const Color ink = item.risk == core::ops::Risk::Low ? Color::TextSecondary : riskInk(item.risk);
        canvas.fillRoundRect({x, y, 2, h}, 1.0f, item.risk == core::ops::Risk::Low ? Color::LineStrong : riskInk(item.risk));
        canvas.drawTextWrapped(notes, {x + 14, y, width - 14, h}, TypeStyle::Small, ink);
        y += h + 6;
    }
    // What goes: the package full name; a system component lists its packages and paths.
    const std::wstring count = std::to_wstring(item.contents.size());
    section(m_strings.get(Str::ComponentsContents) + L" · " +
            m_strings.format(item.kind == ComponentController::Item::Kind::Appx ? Str::ComponentsPackages
                                                                                : Str::ComponentsItemsN,
                             {{L"n", count}}));
    const float bottom = m_toggle->bounds().y - 12;
    for (const auto& line : item.contents) {
        if (y + 22 > bottom) {
            break;
        }
        canvas.drawText(line, {x, y, width, 22}, TypeStyle::Mono, Color::TextSecondary);
        y += 22;
    }
}

} // namespace wl::app
