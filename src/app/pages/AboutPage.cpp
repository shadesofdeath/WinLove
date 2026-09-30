#include "app/pages/AboutPage.h"

#include "app/SystemInfo.h"

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 11.0f;        // page top → the mark (screen 20)
constexpr float kNameX = 40.0f;      // the 24px mark + gap
constexpr float kRowsTop = 63.0f;    // page top → first row
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 160.0f;
constexpr float kButtonsGap = 12.0f;
} // namespace

AboutPage::AboutPage(AppState& state, const Localization& strings, Intents intents) : m_state(state), m_strings(strings) {
    m_licenses = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AboutLicenses));
    m_licenses->onInvoke = std::move(intents.showLicenses);
    m_logs = &add<ui::Button>(ui::ButtonKind::Subtle, strings.get(Str::AboutOpenLogFolder));
    m_logs->onInvoke = std::move(intents.openLogFolder);
    setAccessible(ui::AccessRole::Group, strings.get(Str::AboutTitle));
    // The work folder row follows the settings.
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Settings) {
            refresh();
        }
    });
    refresh();
}

AboutPage::~AboutPage() {
    m_state.unsubscribe(m_subscription);
}

void AboutPage::refresh() {
    auto s = [&](Str key) { return m_strings.get(key); };
    m_version = m_strings.format(Str::AboutVersion, {{L"v", L"" WL_VERSION_STRING},
                                                     {L"b", buildDate()},
                                                     {L"arch", buildArchitecture()}});
    const std::wstring dismVersion = dismLibraryVersion();
    m_rows = {
        {s(Str::AboutDism), {dismVersion.empty() ? dismLibraryPath() : dismVersion + L" · " + dismLibraryPath(), true}},
        {s(Str::AboutWorkDir), {m_state.settings().workRoot.wstring(), true}},
        {s(Str::AboutFonts), {s(Str::AboutFontsValue), false}},
        {s(Str::AboutThirdParty), {s(Str::AboutThirdPartyValue), false}},
    };
    layout();
    invalidate();
}

void AboutPage::layout() {
    const RectF b = bounds();
    const float y = b.y + kRowsTop + kRow * static_cast<float>(m_rows.size()) + kButtonsGap;
    const ui::SizeF licenses = m_licenses->measure({});
    m_licenses->setBounds({b.x, y, licenses.width, licenses.height});
    const ui::SizeF logs = m_logs->measure({});
    m_logs->setBounds({b.x + licenses.width + 8, y, logs.width, logs.height});
}

void AboutPage::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.drawIcon(ui::icons::Icon::BrandMark, {b.x, b.y + kTop + 2}, Color::AccentBase, ui::IconVariant::Regular24);
    canvas.drawText(m_strings.get(Str::AppName), {b.x + kNameX, b.y + kTop, b.width - kNameX, 22}, TypeStyle::Title,
                    Color::TextPrimary);
    canvas.drawText(m_version, {b.x + kNameX, b.y + kTop + 22, b.width - kNameX, 16}, TypeStyle::Caption, Color::TextSecondary);
    float y = b.y + kRowsTop;
    for (const auto& [label, value] : m_rows) {
        canvas.drawText(label, {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        canvas.drawText(value.first, {b.x + kLabelWidth, y, b.width - kLabelWidth, kRow},
                        value.second ? TypeStyle::Mono : TypeStyle::Body, Color::TextPrimary);
        y += kRow;
    }
}

} // namespace wl::app
