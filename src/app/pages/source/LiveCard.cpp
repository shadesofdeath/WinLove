#include "app/pages/source/LiveCard.h"

#include "app/Format.h"

#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kPadding = 16.0f;
constexpr float kKeyWidth = 80.0f;
constexpr float kLine = 16.0f;
} // namespace

LiveCard::LiveCard(const Localization& strings, Language language, core::LiveSystemInfo info, bool elevated)
    : m_strings(strings), m_language(language), m_info(std::move(info)), m_elevated(elevated) {
    m_edit = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::SourceLiveEdit));
    m_edit->onInvoke = [this] {
        if (onEdit) {
            onEdit();
        }
    };
    if (m_elevated) {
        // Live editing is Faz 4 (docs/ROADMAP.md): say so instead of pretending.
        m_edit->setEnabled(false);
        setTooltip(strings.get(Str::SourceLiveLater));
    }
    setAccessible(ui::AccessRole::Group, strings.get(Str::SourceLive));
}

void LiveCard::layout() {
    const RectF b = bounds();
    const ui::SizeF size = m_edit->measure({});
    // Button sits on the last line (design: y 228 in a 108..268 card).
    m_edit->setBounds({b.x + kPadding, b.bottom() - kPadding - size.height, size.width, size.height});
}

void LiveCard::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.strokeRoundRect(b, ui::tokens::radius::r3, Color::LineStrong);
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    float y = b.y + kPadding;

    canvas.drawIcon(ui::icons::Icon::WindowsLogoGeneric, {x, y + 4}, Color::TextSecondary);
    const float textX = x + 24;
    canvas.drawText(m_strings.get(Str::SourceLive), {textX, y, width - 24, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
    const std::wstring os = std::format(L"{} {} · {}.{} · {}", m_info.productName, m_info.displayVersion, m_info.build,
                                        m_info.ubr, core::architectureName(m_info.architecture));
    canvas.drawText(os, {textX, y + kLine, width - 24, kLine}, TypeStyle::Caption, Color::TextSecondary);

    // Key/value rows (design: 24px apart, starting 55px under the title).
    y = b.y + 71;
    // KeyValue rows (log-console-inspector-diff-palette.md): key 11 text.secondary, value 11
    // text.primary, mono for machine values.
    auto row = [&](Str key, const std::wstring& value, bool mono) {
        canvas.drawText(m_strings.get(key), {x, y, kKeyWidth, kLine}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {x + kKeyWidth, y, width - kKeyWidth, kLine}, mono ? TypeStyle::Mono : TypeStyle::Caption,
                        Color::TextPrimary);
        y += 24;
    };
    row(Str::SourceDrive, std::format(L"{} · {} / {}", m_info.systemDrive, formatBytes(m_info.driveFree, m_language),
                                      formatBytes(m_info.driveTotal, m_language)), true);
    row(Str::SourceAdmin, m_strings.get(m_elevated ? Str::SourceAdminYes : Str::SourceAdminRequired), false);
}

} // namespace wl::app
