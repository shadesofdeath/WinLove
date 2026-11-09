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
// D-114 (user: "daha modern, profesyonel"): a hero card (the mark on an accent tile, name,
// version, what WinLove promises as pills, the two links that matter as buttons), the made-with-
// love card, then Bağlantılar and Sistem side by side as cards.
constexpr float kHero = 120.0f;
constexpr float kTile = 64.0f;
constexpr float kGap = 12.0f;        // between cards
constexpr float kCardPad = 16.0f;
constexpr float kCardTitle = 40.0f;  // the card's title band
constexpr float kFlagH = 16.0f;      // the flag: 2:3, as the law has it
constexpr float kFlagW = kFlagH * 1.5f;
constexpr float kLoveRow = 28.0f;
constexpr float kBodyLine = 18.0f;
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 128.0f;
constexpr float kPill = 24.0f;
constexpr float kTwoColumns = 760.0f; // narrower: the cards stack

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
    m_heroProject = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::AboutActionProject), ui::icons::Icon::ExternalLink);
    m_heroProject->onInvoke = [open = intents.openLink] {
        if (open) {
            open(kProjectUrl);
        }
    };
    m_heroIssue = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AboutActionIssue), ui::icons::Icon::QuestionCircle);
    m_heroIssue->onInvoke = [open = intents.openLink] {
        if (open) {
            open(kIssuesUrl);
        }
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

    m_licenses = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AboutLicenses), ui::icons::Icon::File);
    m_licenses->onInvoke = std::move(intents.showLicenses);
    m_logs = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AboutOpenLogFolder), ui::icons::Icon::OpenFolder);
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

float AboutPage::systemRowHeight(std::size_t row, float cardWidth) const {
    if (row >= m_rows.size() || !host()) {
        return kRow;
    }
    const auto& [value, mono] = m_rows[row].second;
    const float width = std::max(cardWidth - 2 * kCardPad - kLabelWidth, 40.0f);
    const float text = std::ceil(host()->text().measureWrapped(value, mono ? TypeStyle::Mono : TypeStyle::Body, width));
    return std::max(kRow, text + 14.0f);
}

float AboutPage::systemRowsHeight(float cardWidth) const {
    float h = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        h += systemRowHeight(i, cardWidth);
    }
    return h;
}

float AboutPage::contentHeight() const {
    const Cards c = cards();
    return std::max(c.links.bottom(), c.system.bottom()) - bounds().y;
}

AboutPage::Cards AboutPage::cards() const {
    const RectF b = bounds();
    Cards c;
    c.hero = {b.x, b.y, b.width, kHero};
    const float loveBody = freeBodyHeight(b.width - 2 * kCardPad);
    c.love = {b.x, c.hero.bottom() + kGap, b.width, kCardPad + kLoveRow + loveBody + kCardPad};
    const float linksH = kCardTitle + kRow * static_cast<float>(m_links.size()) + kCardPad;
    const float top = c.love.bottom() + kGap;
    const bool twoColumns = b.width >= kTwoColumns;
    const float systemW = twoColumns ? (b.width - kGap) / 2 : b.width;
    const float systemH = kCardTitle + systemRowsHeight(systemW) + 8 + ui::tokens::size::control + kCardPad;
    if (twoColumns) {
        c.links = {b.x, top, systemW, std::max(linksH, systemH)};
        c.system = {b.x + systemW + kGap, top, systemW, std::max(linksH, systemH)};
    } else {
        c.links = {b.x, top, b.width, linksH};
        c.system = {b.x, c.links.bottom() + kGap, b.width, systemH};
    }
    return c;
}

void AboutPage::layout() {
    const Cards c = cards();
    // Hero: the two buttons at its right, centred on it.
    const ui::SizeF issue = m_heroIssue->measure({});
    const ui::SizeF project = m_heroProject->measure({});
    const float by = c.hero.y + (c.hero.height - project.height) / 2;
    m_heroIssue->setBounds({c.hero.right() - kCardPad - issue.width, by, issue.width, issue.height});
    m_heroProject->setBounds({m_heroIssue->bounds().x - 8 - project.width, by, project.width, project.height});
    float y = c.links.y + kCardTitle;
    for (const auto& item : m_links) {
        const ui::SizeF size = item.button->measure({});
        const float maxWidth = c.links.width - kCardPad - kLabelWidth - kCardPad + 8;
        item.button->setBounds({c.links.x + kCardPad + kLabelWidth - 8, y + (kRow - size.height) / 2, std::min(size.width, maxWidth),
                                size.height});
        y += kRow;
    }
    const float buttonsY = c.system.y + kCardTitle + systemRowsHeight(c.system.width) + 8;
    const ui::SizeF licenses = m_licenses->measure({});
    m_licenses->setBounds({c.system.x + kCardPad, buttonsY, licenses.width, licenses.height});
    const ui::SizeF logs = m_logs->measure({});
    m_logs->setBounds({c.system.x + kCardPad + licenses.width + 8, buttonsY, logs.width, logs.height});
}

void AboutPage::paint(ui::Canvas& canvas) {
    const Cards c = cards();
    const float radius = ui::tokens::radius::r3;
    // Hero.
    canvas.fillRoundRect(c.hero, radius, Color::BgRaised);
    const RectF tile{c.hero.x + kCardPad + 4, c.hero.y + (c.hero.height - kTile) / 2, kTile, kTile};
    canvas.fillRoundRect(tile, 14.0f, Color::AccentBase);
    canvas.drawIcon(ui::icons::Icon::BrandMark, {tile.x + (kTile - 36) / 2, tile.y + (kTile - 36) / 2}, Color::TextOnAccent,
                    ui::IconVariant::Regular24, 36.0f);
    const float tx = tile.right() + 18;
    const float textRoom = std::max(m_heroProject->bounds().x - tx - 12, 0.0f);
    // Title 30, version 18, 10 apart from the pills (24): the block centred on the card. The pills
    // sat over the version line before (2026-11-09).
    constexpr float kTitleH = 30.0f;
    constexpr float kVersionH = 18.0f;
    constexpr float kPillGap = 10.0f;
    const float textTop = c.hero.y + std::round((c.hero.height - (kTitleH + kVersionH + kPillGap + kPill)) / 2);
    canvas.drawText(m_strings.get(Str::AppName), {tx, textTop, textRoom, kTitleH}, TypeStyle::Title, Color::TextPrimary);
    canvas.drawText(m_version, {tx, textTop + kTitleH, textRoom, kVersionH}, TypeStyle::Caption, Color::TextSecondary);
    const float pillY = textTop + kTitleH + kVersionH + kPillGap;
    float px = tx;
    for (const Str pill : {Str::AboutPillFree, Str::AboutPillNoAds, Str::AboutPillNoData, Str::AboutPillOpenSource}) {
        const std::wstring text = m_strings.get(pill);
        const float w = std::ceil(canvas.text().measure(text, TypeStyle::Caption)) + 20;
        if (px + w > tx + textRoom) {
            break;
        }
        const RectF r{px, pillY, w, kPill};
        canvas.fillRoundRect(r, kPill / 2, Color::AccentSubtle);
        canvas.drawText(text, r, TypeStyle::Caption, Color::AccentText, ui::TextAlign::Center);
        px += w + 6;
    }

    // Made with love in Türkiye, and what that means here.
    canvas.fillRoundRect(c.love, radius, Color::BgRaised);
    float y = c.love.y + kCardPad;
    paintFlag(canvas, {c.love.x + kCardPad, y + (kLoveRow - kFlagH) / 2}, kFlagH);
    canvas.drawText(m_strings.get(Str::AboutMadeWithLove), {c.love.x + kCardPad + kFlagW + 12, y, c.love.width, kLoveRow},
                    TypeStyle::BodyStrong, Color::TextPrimary);
    y += kLoveRow;
    const float bodyWidth = c.love.width - 2 * kCardPad;
    canvas.drawTextWrapped(m_strings.get(Str::AboutFreeBody), {c.love.x + kCardPad, y, bodyWidth, freeBodyHeight(bodyWidth) + 4},
                           TypeStyle::Body, Color::TextSecondary);

    // Links and system.
    auto card = [&](RectF r, Str title, ui::icons::Icon icon) {
        canvas.fillRoundRect(r, radius, Color::BgRaised);
        canvas.drawIcon(icon, {r.x + kCardPad, r.y + (kCardTitle - ui::tokens::size::icon) / 2}, Color::AccentBase);
        canvas.drawText(m_strings.get(title), {r.x + kCardPad + ui::tokens::size::icon + 10, r.y, r.width, kCardTitle},
                        TypeStyle::BodySemi, Color::TextPrimary);
    };
    card(c.links, Str::AboutSectionLinks, ui::icons::Icon::Link);
    y = c.links.y + kCardTitle;
    for (const auto& item : m_links) {
        canvas.hairlineH(c.links.x + kCardPad, y, c.links.width - 2 * kCardPad, Color::LineSubtle);
        canvas.drawText(m_strings.get(item.label), {c.links.x + kCardPad, y, kLabelWidth - 8, kRow}, TypeStyle::Body,
                        Color::TextSecondary);
        y += kRow;
    }
    card(c.system, Str::AboutSectionSystem, ui::icons::Icon::Settings);
    y = c.system.y + kCardTitle;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const auto& [label, value] = m_rows[i];
        const float rowH = systemRowHeight(i, c.system.width);
        canvas.hairlineH(c.system.x + kCardPad, y, c.system.width - 2 * kCardPad, Color::LineSubtle);
        canvas.drawText(label, {c.system.x + kCardPad, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        const RectF valueRect{c.system.x + kCardPad + kLabelWidth, y, c.system.width - 2 * kCardPad - kLabelWidth, rowH};
        const TypeStyle style = value.second ? TypeStyle::Mono : TypeStyle::Body;
        if (rowH <= kRow) {
            canvas.drawText(value.first, valueRect, style, Color::TextPrimary); // one line: centred like the label
        } else {
            canvas.drawTextWrapped(value.first, {valueRect.x, y + 7, valueRect.width, rowH - 7}, style, Color::TextPrimary);
        }
        y += rowH;
    }
}

} // namespace wl::app
