#include "app/pages/components/ComponentInspector.h"

#include "app/Format.h"
#include "ui/widget/Host.h"

#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kPadding = 16.0f;
constexpr float kKeyWidth = 96.0f;
constexpr float kRow = 24.0f;
constexpr float kLine = 16.0f;

Color riskInk(core::ops::Risk risk) {
    switch (risk) {
    case core::ops::Risk::Low: return Color::StatusSuccess;
    case core::ops::Risk::Medium: return Color::StatusWarning;
    case core::ops::Risk::High: return Color::StatusError;
    }
    return Color::TextSecondary;
}

Str riskText(core::ops::Risk risk) {
    switch (risk) {
    case core::ops::Risk::Low: return Str::RiskLow;
    case core::ops::Risk::Medium: return Str::RiskMedium;
    case core::ops::Risk::High: return Str::RiskHigh;
    }
    return Str::RiskMedium;
}
} // namespace

ui::icons::Icon ComponentInspector::iconOf(ComponentController::Item::Kind kind) noexcept {
    switch (kind) {
    case ComponentController::Item::Kind::System: return ui::icons::Icon::WindowsLogoGeneric;
    case ComponentController::Item::Kind::Cleanup: return ui::icons::Icon::SizeSaved;
    case ComponentController::Item::Kind::Appx: break;
    }
    return ui::icons::Icon::AppxPackage;
}

ComponentInspector::ComponentInspector(const Localization& strings, Language language)
    : m_strings(strings), m_language(language) {
    m_toggle = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::ComponentsAddToQueue));
    m_toggle->onInvoke = [this] {
        if (onToggle) {
            onToggle();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void ComponentInspector::set(std::optional<ComponentController::Item> item, std::wstring group, bool queued) {
    m_item = std::move(item);
    m_group = std::move(group);
    m_queued = queued;
    m_toggle->setText(m_strings.get(queued ? Str::ComponentsRemoveFromQueue : Str::ComponentsAddToQueue));
    m_toggle->setVisible(m_item.has_value());
    m_toggle->setEnabled(m_item && (queued || !m_item->locked));
    layout();
    invalidate();
}

void ComponentInspector::layout() {
    const RectF b = bounds();
    const ui::SizeF size = m_toggle->measure({});
    m_toggle->setBounds({b.x + kPadding, b.bottom() - 12 - size.height, size.width, size.height});
}

void ComponentInspector::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.x, b.y, b.height, Color::LineSubtle);
    if (!m_item) {
        return;
    }
    const auto& item = *m_item;
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;
    canvas.drawIcon(iconOf(item.kind), {x, y + 4}, Color::TextSecondary);
    canvas.drawText(item.name, {x + 24, y, width - 24, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(item.identity, {x + 24, y + kLine, width - 24, kLine}, TypeStyle::Mono, Color::TextTertiary);

    y = b.y + 69;
    auto row = [&](Str key, const std::wstring& value, TypeStyle style = TypeStyle::Caption,
                   std::optional<Color> dot = std::nullopt) {
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth, kRow}, TypeStyle::Caption, Color::TextSecondary);
        float vx = x + kKeyWidth;
        if (dot) {
            canvas.fillRect({vx, y + 9, 6, 6}, *dot);
            vx += 12;
        }
        canvas.drawText(value, {vx, y, b.right() - kPadding - vx, kRow}, style, Color::TextPrimary);
        y += kRow;
    };
    auto section = [&](const std::wstring& title) {
        y += 12;
        canvas.hairlineH(b.x + 1, y, b.width - 1, Color::LineSubtle);
        y += 8;
        canvas.drawText(title, {x, y, width, kRow}, TypeStyle::Section, Color::TextSecondary);
        y += kRow;
    };
    row(Str::ComponentsCategory, m_group);
    row(Str::CommonSize, item.size ? formatBytes(item.size, m_language) : std::wstring(L"—"), TypeStyle::Mono);
    row(Str::RiskColumn, m_strings.get(riskText(item.risk)), TypeStyle::Caption, riskInk(item.risk));
    row(Str::CommonStatus, m_strings.get(m_queued        ? Str::ComponentsQueued
                                         : item.locked ? Str::ComponentsLocked
                                                       : Str::ComponentsStays));
    row(Str::ComponentsReversible, m_strings.get(Str::CommonNo));

    // A locked app: why it cannot be picked comes before what removing it would have meant.
    const std::wstring notes = item.locked ? m_strings.get(Str::ComponentsLockedNote) +
                                                 (item.notes.empty() ? std::wstring() : L" " + item.notes)
                                           : item.notes;
    if (!notes.empty()) {
        section(m_strings.get(Str::ComponentsCompat));
        const float h = std::ceil(canvas.text().measureWrapped(notes, TypeStyle::Caption, width - 20));
        canvas.drawIcon(ui::icons::Icon::WarningTriangle, {x, y}, riskInk(item.risk));
        canvas.drawTextWrapped(notes, {x + 20, y, width - 20, h}, TypeStyle::Caption, Color::TextSecondary);
        y += h + 4;
    }
    // What goes: the package full name; a system component lists its packages and paths.
    const std::wstring count = std::to_wstring(item.contents.size());
    section(m_strings.get(Str::ComponentsContents) + L" · " +
            m_strings.format(item.kind == ComponentController::Item::Kind::Appx ? Str::ComponentsPackages
                                                                                : Str::ComponentsItemsN,
                             {{L"n", count}}));
    const float bottom = m_toggle->bounds().y - 8;
    for (const auto& line : item.contents) {
        if (y + kRow > bottom) {
            break;
        }
        canvas.drawText(line, {x, y, width, kRow}, TypeStyle::Mono, Color::TextSecondary);
        y += kRow;
    }
}

} // namespace wl::app
