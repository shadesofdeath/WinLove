#include "app/pages/unattended/WelcomeCard.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>

namespace wl::app {

using ui::PointF;
using ui::RectF;
using ui::Rgba;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Stage = WelcomeCard::Stage;

namespace {

constexpr float kPad = 20.0f;
constexpr float kLeftMax = 430.0f;
constexpr float kBadgeH = 20.0f;
constexpr float kChipH = 26.0f;
constexpr float kChipGap = 6.0f;
constexpr float kTile = 34.0f;
constexpr auto kStageCount = static_cast<std::size_t>(Stage::Count);

// "#RRGGBB" → colour; Windows' blue when it is not one.
Rgba parseAccent(const std::wstring& hex, float alpha = 1.0f) {
    unsigned value = 0x0078D4;
    if (hex.size() == 7 && hex[0] == L'#') {
        value = static_cast<unsigned>(std::wcstoul(hex.c_str() + 1, nullptr, 16));
    }
    return {static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
            static_cast<float>(value & 0xFF) / 255.0f, alpha};
}

Rgba withAlpha(Rgba c, float a) {
    c.a = a;
    return c;
}

// Mixed towards black (dark) or white (light): the wizard's backdrop in its accent.
Rgba shade(Rgba c, bool dark) {
    const float k = dark ? 0.22f : 0.12f;
    return dark ? Rgba{c.r * k, c.g * k, c.b * k + 0.04f, 1.0f}
                : Rgba{1.0f - (1.0f - c.r) * k, 1.0f - (1.0f - c.g) * k, 1.0f - (1.0f - c.b) * k, 1.0f};
}

ui::icons::Icon stageIcon(Stage stage) {
    using ui::icons::Icon;
    switch (stage) {
    case Stage::Account: return Icon::UserAccount;
    case Stage::Network: return Icon::Network;
    case Stage::Computer: return Icon::WindowsLogoGeneric;
    case Stage::Look: return Icon::Personalize;
    case Stage::Wallpaper: return Icon::StartMenu;
    case Stage::Bundles: return Icon::Programs;
    case Stage::Prefs: return Icon::TweaksSliders;
    case Stage::Privacy: return Icon::ShieldCheck;
    case Stage::Finish:
    case Stage::Count: break;
    }
    return Icon::SuccessCircle;
}

// A text line in the drawing, as a soft bar (the wizard's words are its own; the shape is what counts).
void skeleton(ui::Canvas& canvas, float x, float y, float width, float height = 6.0f) {
    canvas.fillRoundRect({x, y, std::max(width, 4.0f), height}, height / 2, ui::Ink(Color::TextTertiary, Color::TextTertiary, 0, 0.35f));
}

void miniToggle(ui::Canvas& canvas, float x, float cy, bool on, Rgba accent) {
    const RectF track{x, cy - 6, 22, 12};
    if (on) {
        canvas.fillRoundRect(track, 6, accent);
        canvas.fillEllipse({track.right() - 6, cy}, 4, Rgba{1, 1, 1, 1});
    } else {
        canvas.strokeRoundRect(track, 6, Color::LineStrong);
        canvas.fillEllipse({track.x + 6, cy}, 3.5f, Color::TextTertiary);
    }
}

void miniInput(ui::Canvas& canvas, RectF r, const std::wstring& text, Rgba accent) {
    canvas.fillRoundRect(r, 4, Color::BgInput);
    canvas.strokeRoundRect(r, 4, Color::LineSubtle);
    canvas.fillRoundRect({r.x + 1, r.bottom() - 2, r.width - 2, 2}, 1, accent);
    canvas.drawText(text, {r.x + 8, r.y, r.width - 12, r.height}, TypeStyle::Small, Color::TextPrimary);
}

} // namespace

WelcomeCard::WelcomeCard(const Localization& strings) : m_strings(strings), m_chips(kStageCount) {
    m_toggle = &add<ui::Toggle>(strings.get(Str::UnattendedCardUse), false);
    m_toggle->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::UnattendedCardUse));
    m_toggle->onChange = [this](bool on) {
        if (onToggle) {
            onToggle(on);
        }
    };
    m_window = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::UnattendedCardWindow), ui::icons::Icon::Eye);
    m_window->onInvoke = [this] {
        if (onWindowPreview) {
            onWindowPreview();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::UnattendedCardTitle));
}

void WelcomeCard::setState(bool on, const core::WelcomePlan& plan) {
    m_on = on;
    m_plan = plan;
    if (m_toggle->isOn() != on) {
        m_toggle->setOn(on);
    }
    invalidate();
}

bool WelcomeCard::asked(Stage stage) const {
    switch (stage) {
    case Stage::Network: return m_plan.networkPage;
    case Stage::Computer: return m_plan.computerPage;
    case Stage::Look: return m_plan.lookPage;
    case Stage::Wallpaper: return m_plan.wallpaperPage;
    case Stage::Bundles: return m_plan.bundlesPage;
    case Stage::Prefs: return m_plan.prefsPage;
    case Stage::Privacy: return m_plan.privacyPage;
    case Stage::Account:
    case Stage::Finish:
    case Stage::Count: break;
    }
    return true; // the account and the finish are always there
}

Str WelcomeCard::stageName(Stage stage) const {
    switch (stage) {
    case Stage::Account: return Str::UnattendedCardStageAccount;
    case Stage::Network: return Str::UnattendedCardStageNetwork;
    case Stage::Computer: return Str::UnattendedCardStageComputer;
    case Stage::Look: return Str::UnattendedCardStageLook;
    case Stage::Wallpaper: return Str::UnattendedCardStageWallpaper;
    case Stage::Bundles: return Str::UnattendedCardStageBundles;
    case Stage::Prefs: return Str::UnattendedCardStagePrefs;
    case Stage::Privacy: return Str::UnattendedCardStagePrivacy;
    case Stage::Finish:
    case Stage::Count: break;
    }
    return Str::UnattendedCardStageFinish;
}

Str WelcomeCard::stageSub(Stage stage) const {
    switch (stage) {
    case Stage::Account: return Str::UnattendedCardSubAccount;
    case Stage::Network: return Str::UnattendedCardSubNetwork;
    case Stage::Computer: return Str::UnattendedCardSubComputer;
    case Stage::Look: return Str::UnattendedCardSubLook;
    case Stage::Wallpaper: return Str::UnattendedCardSubWallpaper;
    case Stage::Bundles: return Str::UnattendedCardSubBundles;
    case Stage::Prefs: return Str::UnattendedCardSubPrefs;
    case Stage::Privacy: return Str::UnattendedCardSubPrivacy;
    case Stage::Finish:
    case Stage::Count: break;
    }
    return Str::UnattendedCardSubFinish;
}

float WelcomeCard::leftWidth(float cardWidth) {
    return std::min(kLeftMax, std::max(cardWidth * 0.36f, 280.0f));
}

RectF WelcomeCard::leftColumn() const {
    const RectF b = bounds();
    return {b.x + kPad, b.y + kPad, leftWidth(b.width), b.height - 2 * kPad};
}

float WelcomeCard::bodyHeight(float width) const {
    if (!host()) {
        return 40.0f;
    }
    float h = 0;
    for (const Str key : {Str::UnattendedCardBody, Str::UnattendedCardOffNote}) {
        h = std::max(h, std::ceil(host()->text().measureWrapped(m_strings.get(key), TypeStyle::Caption, width)));
    }
    return h;
}

std::vector<RectF> WelcomeCard::chipRects(float x0, float top, float width) const {
    std::vector<RectF> chips(kStageCount);
    float x = x0;
    float y = top;
    for (std::size_t i = 0; i < kStageCount; ++i) {
        const std::wstring name = std::to_wstring(i + 1) + L"  " + m_strings.get(stageName(static_cast<Stage>(i)));
        const float w = textWidth(name, TypeStyle::Small, 70.0f) + 22.0f;
        if (x + w > x0 + width && x > x0) {
            x = x0;
            y += kChipH + kChipGap;
        }
        chips[i] = {x, y, w, kChipH};
        x += w + kChipGap;
    }
    return chips;
}

// The left column, top to bottom: the title row (tile 34), the description, the switch, the
// stages' caption and chips, the real-window button. The card is as tall as that.
float WelcomeCard::preferredHeight(float width) const {
    const float left = leftWidth(width);
    const float toggleTop = kPad + kTile + 10 + bodyHeight(left) + 10;
    const float chipsTop = toggleTop + ui::tokens::size::control + 30;
    const auto chips = chipRects(0, chipsTop, left);
    const float button = m_window->measure({}).height;
    return std::max(chips.back().bottom() + 16 + button + kPad, 230.0f);
}

void WelcomeCard::layout() {
    const RectF b = bounds();
    const RectF left = leftColumn();
    m_bodyTop = left.y + kTile + 10;
    m_bodyH = bodyHeight(left.width);
    const float toggleTop = m_bodyTop + m_bodyH + 10;
    const ui::SizeF toggle = m_toggle->measure({});
    m_toggle->setBounds({left.x, toggleTop, std::min(toggle.width, left.width), ui::tokens::size::control});
    m_chipsTop = toggleTop + ui::tokens::size::control + 30;
    m_chips = chipRects(left.x, m_chipsTop, left.width);
    const ui::SizeF window = m_window->measure({});
    m_window->setBounds({left.x, std::max(m_chips.back().bottom() + 16, left.bottom() - window.height), window.width, window.height});
    const float screenX = left.right() + kPad + 8;
    m_screen = {screenX, b.y + kPad, std::max(b.right() - kPad - screenX, 0.0f), b.height - 2 * kPad};
}

void WelcomeCard::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const RectF left = leftColumn();
    // The card: the accent's hairline when the setup screens are on.
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    canvas.strokeRoundRect(b, ui::tokens::radius::r3, m_on ? Color::AccentBase : Color::LineSubtle, m_on ? 1.5f * canvas.scale() : 1.0f);

    // Title row: WinLove's mark, the name, the ON / OFF badge.
    const RectF tile{left.x, left.y, kTile, kTile};
    canvas.fillRoundRect(tile, 9.0f, m_on ? Color::AccentBase : Color::BgPressed);
    canvas.drawIcon(ui::icons::Icon::BrandMark, {tile.x + (kTile - 20) / 2, tile.y + (kTile - 20) / 2},
                    m_on ? Color::TextOnAccent : Color::TextSecondary, ui::IconVariant::Regular24, 20.0f);
    const std::wstring title = m_strings.get(Str::UnattendedCardTitle);
    const float titleX = tile.right() + 12;
    const float titleW = std::ceil(canvas.text().measure(title, TypeStyle::Panel));
    canvas.drawText(title, {titleX, tile.y, std::min(titleW + 1, left.right() - titleX), kTile}, TypeStyle::Panel, Color::TextPrimary);
    const std::wstring badge = m_strings.get(m_on ? Str::UnattendedCardOn : Str::UnattendedCardOff);
    const float badgeW = std::ceil(canvas.text().measure(badge, TypeStyle::Badge)) + 16;
    const RectF badgeRect{std::min(titleX + titleW + 10, left.right() - badgeW), tile.y + (kTile - kBadgeH) / 2, badgeW, kBadgeH};
    canvas.fillRoundRect(badgeRect, kBadgeH / 2, m_on ? Color::AccentBase : Color::BgPressed);
    canvas.drawText(badge, badgeRect, TypeStyle::Badge, m_on ? Color::TextOnAccent : Color::TextSecondary, ui::TextAlign::Center);

    // What it is (on) or what happens instead (off).
    canvas.drawTextWrapped(m_strings.get(m_on ? Str::UnattendedCardBody : Str::UnattendedCardOffNote),
                           {left.x, m_bodyTop, left.width, m_bodyH + 4}, TypeStyle::Caption,
                           m_on ? Color::TextSecondary : Color::StatusWarning);

    // The stages, in setup's order: the ones asked in the accent, the others greyed; the drawn one outlined.
    canvas.drawText(m_strings.get(Str::UnattendedCardStages), {left.x, m_chipsTop - 22, left.width, 18}, TypeStyle::GroupCap,
                    Color::TextTertiary);
    for (std::size_t i = 0; i < kStageCount; ++i) {
        const Stage stage = static_cast<Stage>(i);
        const RectF r = m_chips[i];
        const bool on = m_on && asked(stage);
        const bool chosen = static_cast<int>(i) == m_selected;
        canvas.fillRoundRect(r, kChipH / 2, on ? Color::AccentSubtle : Color::BgHover);
        if (chosen) {
            canvas.strokeRoundRect(r, kChipH / 2, Color::AccentBase, 1.5f * canvas.scale());
        } else if (static_cast<int>(i) == m_hover) {
            canvas.strokeRoundRect(r, kChipH / 2, Color::LineStrong);
        }
        const std::wstring name = std::to_wstring(i + 1) + L"  " + m_strings.get(stageName(stage));
        canvas.drawText(name, r, chosen ? TypeStyle::SmallSemi : TypeStyle::Small,
                        on ? Color::AccentText : Color::TextTertiary, ui::TextAlign::Center);
    }

    paintPreview(canvas, m_screen);
}

void WelcomeCard::paintPreview(ui::Canvas& canvas, RectF s) const {
    if (s.width < 120 || s.height < 120) {
        return;
    }
    const bool darkPlan = m_plan.theme != L"light";
    const Rgba accent = parseAccent(m_plan.accent);
    if (!m_on) {
        canvas.pushOpacity(0.45f);
    }
    // The wizard's screen: its accent darkened (or lightened) behind a centred card; the glow
    // circles stay inside the screen.
    canvas.fillRoundRect(s, 10.0f, shade(accent, darkPlan));
    canvas.pushClip({s.x + 4, s.y + 4, s.width - 8, s.height - 8});
    canvas.fillEllipse({s.x + s.width * 0.85f, s.y + s.height * 0.15f}, s.height * 0.45f, withAlpha(accent, darkPlan ? 0.18f : 0.10f));
    canvas.fillEllipse({s.x + s.width * 0.1f, s.bottom() - s.height * 0.05f}, s.height * 0.35f, withAlpha(accent, darkPlan ? 0.12f : 0.08f));
    canvas.popClip();
    const RectF card{s.x + s.width * 0.06f, s.y + s.height * 0.08f, s.width * 0.88f, s.height * 0.84f};
    canvas.fillRoundRect(card, 8.0f, Color::BgPanel);
    canvas.strokeRoundRect(card, 8.0f, Color::LineSubtle);

    const Stage stage = static_cast<Stage>(std::clamp(m_selected, 0, static_cast<int>(kStageCount) - 1));
    // Left: the stage's picture in a ring of the accent.
    const float picW = card.width * 0.36f;
    const PointF centre{card.x + picW / 2, card.y + card.height * 0.48f};
    const float ring = std::min(picW, card.height) * 0.32f;
    canvas.fillEllipse(centre, ring, withAlpha(accent, 0.12f));
    canvas.fillEllipse(centre, ring * 0.66f, withAlpha(accent, 0.22f));
    canvas.drawIcon(stageIcon(stage), {centre.x - 14, centre.y - 14}, Color::AccentBase, ui::IconVariant::Regular24, 28.0f);

    // Right: the step bar ("3 / 8"), the title and what it asks.
    const RectF right{card.x + picW, card.y + 16, card.width - picW - 20, card.height - 32};
    std::vector<Stage> shown;
    for (std::size_t i = 0; i + 1 < kStageCount; ++i) {
        if (asked(static_cast<Stage>(i))) {
            shown.push_back(static_cast<Stage>(i));
        }
    }
    const auto at = std::ranges::find(shown, stage);
    if (stage != Stage::Finish && at != shown.end()) {
        const int index = static_cast<int>(at - shown.begin());
        const float segW = 16.0f;
        float x = right.right() - static_cast<float>(shown.size()) * (segW + 3) - 34;
        for (std::size_t i = 0; i < shown.size(); ++i) {
            canvas.fillRoundRect({x, right.y + 5, segW, 3}, 1.5f,
                                 static_cast<int>(i) <= index ? accent : withAlpha(Rgba{0.5f, 0.5f, 0.5f, 1}, 0.35f));
            x += segW + 3;
        }
        canvas.drawText(m_strings.format(Str::UnattendedCardOf, {{L"i", std::to_wstring(index + 1)}, {L"n", std::to_wstring(shown.size())}}),
                        {x + 4, right.y - 2, 40, 16}, TypeStyle::Small, Color::TextTertiary);
    }
    const float titleY = right.y + 18;
    canvas.drawText(m_strings.get(stageName(stage)), {right.x, titleY, right.width, 26}, TypeStyle::Panel, Color::TextPrimary);
    canvas.drawText(m_strings.get(stageSub(stage)), {right.x, titleY + 26, right.width, 18}, TypeStyle::Small, Color::TextSecondary);
    const RectF area{right.x, titleY + 54, right.width, right.bottom() - titleY - 54 - 34};
    paintStageBody(canvas, area, stage, accent);

    // "Next" (the theme's accent button: its text color is made for it in every theme).
    if (stage != Stage::Finish) {
        const std::wstring next = m_strings.get(Str::UnattendedCardNext);
        const float w = std::ceil(canvas.text().measure(next, TypeStyle::SmallSemi)) + 28;
        const RectF button{right.right() - w, right.bottom() - 24, w, 24};
        canvas.fillRoundRect(button, 4.0f, Color::AccentBase);
        canvas.drawText(next, button, TypeStyle::SmallSemi, Color::TextOnAccent, ui::TextAlign::Center);
    }
    // A theme tag on the screen's corner: what the wizard opens in.
    const std::wstring tag = m_strings.get(darkPlan ? Str::UnattendedCardThemeDark : Str::UnattendedCardThemeLight);
    const float tagW = std::ceil(canvas.text().measure(tag, TypeStyle::Small)) + 16;
    const RectF tagRect{s.right() - tagW - 10, s.bottom() - 26, tagW, 18};
    canvas.fillRoundRect(tagRect, 9.0f, ui::Ink(Color::BgPanel, Color::BgPanel, 0, 0.9f));
    canvas.drawText(tag, tagRect, TypeStyle::Small, Color::TextSecondary, ui::TextAlign::Center);

    // A stage the plan leaves out: said over the card.
    if (m_on && !asked(stage)) {
        canvas.fillRoundRect(card, 8.0f, ui::Ink(Color::BgPanel, Color::BgPanel, 0, 0.85f));
        canvas.drawText(m_strings.get(Str::UnattendedCardSkipped), card, TypeStyle::BodyStrong, Color::TextSecondary,
                        ui::TextAlign::Center);
    }
    if (!m_on) {
        canvas.popOpacity();
        const std::wstring off = m_strings.get(Str::UnattendedCardOffNote);
        const float w = std::min(std::ceil(canvas.text().measure(off, TypeStyle::BodyStrong)) + 32, s.width - 20);
        const RectF pill{s.x + (s.width - w) / 2, s.y + (s.height - 34) / 2, w, 34};
        canvas.panel(pill, 17.0f, ui::tokens::elevation::toast);
        canvas.drawText(off, pill, TypeStyle::BodyStrong, Color::TextPrimary, ui::TextAlign::Center);
    }
}

void WelcomeCard::paintStageBody(ui::Canvas& canvas, RectF a, Stage stage, Rgba accent) const {
    if (a.height < 20) {
        return;
    }
    const float w = std::min(a.width, 300.0f);
    switch (stage) {
    case Stage::Account: {
        skeleton(canvas, a.x, a.y + 2, 70);
        miniInput(canvas, {a.x, a.y + 12, w, 24}, L"", accent);
        skeleton(canvas, a.x, a.y + 46, 50);
        miniInput(canvas, {a.x, a.y + 56, w, 24}, L"••••••", accent);
        break;
    }
    case Stage::Network: {
        for (int i = 0; i < 3; ++i) {
            const RectF row{a.x, a.y + i * 30.0f, w, 26};
            if (i == 0) {
                canvas.fillRoundRect(row, 4, withAlpha(accent, 0.15f));
            }
            canvas.drawIcon(ui::icons::Icon::Network, {row.x + 6, row.y + 5}, Color::TextSecondary);
            skeleton(canvas, row.x + 30, row.y + 10, 90.0f - i * 18.0f);
        }
        break;
    }
    case Stage::Computer: {
        skeleton(canvas, a.x, a.y + 2, 80);
        miniInput(canvas, {a.x, a.y + 12, w, 24}, m_plan.computerName.empty() ? std::wstring(core::kWelcomeComputerName) : m_plan.computerName,
                  accent);
        skeleton(canvas, a.x, a.y + 46, 60);
        miniInput(canvas, {a.x, a.y + 56, w, 24}, m_plan.timeZone.empty() ? L"(UTC+03:00) İstanbul" : m_plan.timeZone, accent);
        break;
    }
    case Stage::Look: {
        // The two themes (the plan's pre-selected one ringed), then the accents.
        const bool dark = m_plan.theme != L"light";
        for (int i = 0; i < 2; ++i) {
            const RectF t{a.x + i * 70.0f, a.y, 60, 38};
            canvas.fillRoundRect(t, 5, i == 0 ? Rgba{0.12f, 0.12f, 0.14f, 1} : Rgba{0.96f, 0.96f, 0.97f, 1});
            canvas.fillRoundRect({t.x + 8, t.y + 8, 30, 5}, 2, i == 0 ? Rgba{0.35f, 0.35f, 0.4f, 1} : Rgba{0.8f, 0.8f, 0.84f, 1});
            canvas.fillRoundRect({t.x + 8, t.bottom() - 12, 18, 5}, 2, accent);
            if ((i == 0) == dark) {
                canvas.strokeRoundRect({t.x - 2, t.y - 2, t.width + 4, t.height + 4}, 6, Color::AccentBase, 2.0f * canvas.scale());
            }
        }
        float x = a.x;
        for (const auto& [key, hex] : core::kWelcomeAccents) {
            const Rgba c = parseAccent(hex);
            const PointF centre{x + 8, a.y + 58};
            canvas.fillEllipse(centre, 7, c);
            if (m_plan.accent == hex) {
                canvas.fillEllipse(centre, 2.5f, Rgba{1, 1, 1, 1});
            }
            x += 20;
        }
        break;
    }
    case Stage::Wallpaper: {
        for (int i = 0; i < 6; ++i) {
            const RectF t{a.x + (i % 3) * 52.0f, a.y + (i / 3) * 34.0f, 46, 28};
            const float k = 0.25f + 0.12f * static_cast<float>(i);
            canvas.fillRoundRect(t, 4, Rgba{accent.r * k + 0.1f, accent.g * k + 0.1f, accent.b * (k + 0.2f) + 0.15f, 1});
            if (i == 0) {
                canvas.strokeRoundRect({t.x - 2, t.y - 2, t.width + 4, t.height + 4}, 5, Color::AccentBase, 2.0f * canvas.scale());
            }
        }
        break;
    }
    case Stage::Bundles:
    case Stage::Prefs: {
        const int rows = stage == Stage::Bundles ? 4 : 5;
        for (int i = 0; i < rows && i * 20.0f + 14 < a.height; ++i) {
            const float cy = a.y + 7 + i * 20.0f;
            skeleton(canvas, a.x, cy - 3, 110.0f - static_cast<float>((i * 23) % 50));
            miniToggle(canvas, a.x + w - 24, cy, stage == Stage::Prefs ? i % 3 != 1 : i == 0, accent);
        }
        break;
    }
    case Stage::Privacy: {
        const bool strict = m_plan.privacy != L"windows";
        for (int i = 0; i < 2; ++i) {
            const RectF card{a.x, a.y + i * 40.0f, w, 34};
            const bool chosen = (i == 0) == strict;
            canvas.fillRoundRect(card, 5, chosen ? withAlpha(accent, 0.12f) : Rgba{0.5f, 0.5f, 0.5f, 0.06f});
            if (chosen) {
                canvas.strokeRoundRect(card, 5, Color::AccentBase);
            }
            canvas.fillEllipse({card.x + 14, card.y + 17}, 5, chosen ? accent : Rgba{0.5f, 0.5f, 0.5f, 0.5f});
            skeleton(canvas, card.x + 28, card.y + 10, 70);
            skeleton(canvas, card.x + 28, card.y + 21, 130, 5);
        }
        break;
    }
    case Stage::Finish:
    case Stage::Count: {
        // The last screen: what is being done, ticked one by one.
        for (int i = 0; i < 4 && i * 20.0f + 14 < a.height; ++i) {
            const float cy = a.y + 7 + i * 20.0f;
            if (i < 2) {
                canvas.fillEllipse({a.x + 6, cy}, 6, Color::AccentBase);
                canvas.line({a.x + 3, cy}, {a.x + 5.5f, cy + 2.5f}, Color::TextOnAccent, 1.5f * canvas.scale());
                canvas.line({a.x + 5.5f, cy + 2.5f}, {a.x + 9.5f, cy - 2.5f}, Color::TextOnAccent, 1.5f * canvas.scale());
            } else {
                canvas.strokeRoundRect({a.x, cy - 6, 12, 12}, 6, Color::LineStrong);
            }
            skeleton(canvas, a.x + 20, cy - 3, 130.0f - static_cast<float>(i * 17));
        }
        canvas.fillRoundRect({a.x, a.bottom() - 4, w, 3}, 1.5f, Rgba{0.5f, 0.5f, 0.5f, 0.2f});
        canvas.fillRoundRect({a.x, a.bottom() - 4, w * 0.55f, 3}, 1.5f, accent);
        break;
    }
    }
}

void WelcomeCard::onPointerMove(PointF p) {
    int hover = -1;
    for (std::size_t i = 0; i < m_chips.size(); ++i) {
        if (m_chips[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover < 0 && m_screen.contains(p)) {
        hover = -2;
    }
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
}

void WelcomeCard::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hover = -1;
    }
    invalidate();
}

void WelcomeCard::onClick() {
    if (m_hover >= 0) {
        m_selected = m_hover;
    } else if (m_hover == -2) {
        // The drawing: on to the next stage the plan asks (like "Next"), round to the first.
        for (int step = 1; step <= static_cast<int>(kStageCount); ++step) {
            const int next = (m_selected + step) % static_cast<int>(kStageCount);
            if (asked(static_cast<Stage>(next))) {
                m_selected = next;
                break;
            }
        }
    }
    invalidate();
}

ui::Cursor WelcomeCard::cursor() const {
    return m_hover != -1 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

} // namespace wl::app
