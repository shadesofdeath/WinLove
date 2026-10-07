#include "app/pages/AboutPage.h"

#include "app/SystemInfo.h"
#include "app/pages/PageBits.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 11.0f;        // page top → the mark (screen 20)
constexpr float kNameX = 40.0f;      // the 24px mark + gap
constexpr float kLoveTop = 63.0f;    // page top → the flag line
constexpr float kFlagH = 16.0f;      // the flag: 2:3, as the law has it
constexpr float kFlagW = kFlagH * 1.5f;
constexpr float kLoveRow = 28.0f;
constexpr float kBodyMaxWidth = 640.0f;
constexpr float kBodyLine = 18.0f;
constexpr float kSection = 40.0f;    // a section title band (paintFormSection)
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 160.0f;
constexpr float kButtonsGap = 12.0f;

// The flag of Türkiye (Türk Bayrağı Kanunu, 1983): national colors, not theme tokens (D-072).
constexpr ui::Rgba kFlagRed{0xE3 / 255.0f, 0x0A / 255.0f, 0x17 / 255.0f, 1.0f};
constexpr ui::Rgba kFlagWhite{1.0f, 1.0f, 1.0f, 1.0f};

// G = the flag's height. Crescent: outer circle at 1/2 G from the hoist, diameter 1/2 G; inner
// circle 1/16 G further, diameter 2/5 G. Star: diameter 1/4 G, centered at 0.8208 G (the law's
// construction sheet), one point toward the hoist.
void paintFlag(ui::Canvas& canvas, ui::PointF topLeft, float g) {
    canvas.fillRoundRect({topLeft.x, topLeft.y, g * 1.5f, g}, 2.0f, kFlagRed);
    const float cy = topLeft.y + g / 2;
    canvas.fillEllipse({topLeft.x + g * 0.5f, cy}, g * 0.25f, kFlagWhite);
    canvas.fillEllipse({topLeft.x + g * 0.5625f, cy}, g * 0.2f, kFlagRed);
    const float sx = topLeft.x + g * 0.8208f;
    const float outer = g * 0.125f;
    const float inner = outer * 0.381966f; // a regular five-pointed star
    std::array<ui::PointF, 10> star{};
    for (std::size_t i = 0; i < star.size(); ++i) {
        const float angle = std::numbers::pi_v<float> + static_cast<float>(i) * std::numbers::pi_v<float> / 5.0f;
        const float r = i % 2 == 0 ? outer : inner;
        star[i] = {sx + r * std::cos(angle), cy + r * std::sin(angle)};
    }
    canvas.fillPolygon(star, kFlagWhite);
}
} // namespace

AboutPage::AboutPage(AppState& state, const Localization& strings, Intents intents) : m_state(state), m_strings(strings) {
    auto link = [&](Str label, const wchar_t* url) {
        auto& button = add<ui::Button>(ui::ButtonKind::Subtle, std::wstring(url).substr(8), ui::icons::Icon::ExternalLink);
        button.onInvoke = [open = intents.openLink, target = std::wstring(url)] {
            if (open) {
                open(target);
            }
        };
        m_links.push_back({label, &button});
    };
    link(Str::AboutDeveloper, kDeveloperUrl);
    link(Str::AboutProject, kProjectUrl);
    link(Str::AboutIssues, kIssuesUrl);
    auto& license = add<ui::Button>(ui::ButtonKind::Subtle, strings.get(Str::AboutLicenseValue), ui::icons::Icon::ExternalLink);
    license.onInvoke = [open = intents.openLink] {
        if (open) {
            open(kLicenseUrl);
        }
    };
    m_links.push_back({Str::AboutLicense, &license});

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
    m_version = m_strings.format(Str::AboutVersion, {{L"v", L"" WL_VERSION_LABEL},
                                                     {L"b", buildDate()},
                                                     {L"arch", buildArchitecture()}});
    const std::wstring dismVersion = dismLibraryVersion();
    m_rows = {
        {s(Str::AboutDism), {(dismVersion.empty() ? dismLibraryPath() : dismVersion + L" · " + dismLibraryPath()) +
                                 (dismFromAdk() ? L" · " + s(Str::SettingsDismAdk) : std::wstring()),
                             true}},
        {s(Str::AboutWorkDir), {m_state.settings().workRoot.wstring(), true}},
        {s(Str::AboutFonts), {s(Str::AboutFontsValue), false}},
        {s(Str::AboutThirdParty), {s(Str::AboutThirdPartyValue), false}},
    };
    layout();
    invalidate();
}

float AboutPage::freeBodyHeight(float width) const {
    // Measured when attached; before that a rough estimate (12px text, about 6.4px per character).
    if (host()) {
        return std::ceil(host()->text().measureWrapped(m_strings.get(Str::AboutFreeBody), TypeStyle::Body, width));
    }
    const float perLine = std::max(1.0f, std::floor(width / 6.4f));
    const auto chars = static_cast<float>(m_strings.get(Str::AboutFreeBody).size());
    return std::ceil(chars / perLine) * kBodyLine;
}

void AboutPage::layout() {
    const RectF b = bounds();
    const float bodyWidth = std::min(b.width, kBodyMaxWidth);
    float y = b.y + kLoveTop + kLoveRow + freeBodyHeight(bodyWidth) + 8 + kSection;
    for (const auto& item : m_links) {
        const ui::SizeF size = item.button->measure({});
        item.button->setBounds({b.x + kLabelWidth - 8, y + (kRow - size.height) / 2, size.width, size.height});
        y += kRow;
    }
    y += kSection + kRow * static_cast<float>(m_rows.size()) + kButtonsGap;
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

    // Made with love in Türkiye, and what that means here.
    float y = b.y + kLoveTop;
    paintFlag(canvas, {b.x + (24 - kFlagW) / 2 + 4, y + (kLoveRow - kFlagH) / 2}, kFlagH);
    canvas.drawText(m_strings.get(Str::AboutMadeWithLove), {b.x + kNameX, y, b.width - kNameX, kLoveRow}, TypeStyle::BodyStrong,
                    Color::TextPrimary);
    y += kLoveRow;
    const float bodyWidth = std::min(b.width, kBodyMaxWidth);
    const float bodyHeight = freeBodyHeight(bodyWidth);
    canvas.drawTextWrapped(m_strings.get(Str::AboutFreeBody), {b.x, y, bodyWidth, bodyHeight + 4}, TypeStyle::Body,
                           Color::TextSecondary);
    y += bodyHeight + 8;

    paintFormSection(canvas, {b.x, y, b.width, kSection}, m_strings.get(Str::AboutSectionLinks));
    y += kSection;
    for (const auto& item : m_links) {
        canvas.drawText(m_strings.get(item.label), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        y += kRow;
    }

    paintFormSection(canvas, {b.x, y, b.width, kSection}, m_strings.get(Str::AboutSectionSystem));
    y += kSection;
    for (const auto& [label, value] : m_rows) {
        canvas.drawText(label, {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        canvas.drawText(value.first, {b.x + kLabelWidth, y, b.width - kLabelWidth, kRow},
                        value.second ? TypeStyle::Mono : TypeStyle::Body, Color::TextPrimary);
        y += kRow;
    }
}

} // namespace wl::app
