#include "app/pages/unattended/WelcomeCard.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

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

// Towards white (1) or black (0) by `k`, as the setup screens mix (oobe.ps1 Get-Mix).
Rgba mix(Rgba c, float toward, float k) {
    return {c.r + (toward - c.r) * k, c.g + (toward - c.g) * k, c.b + (toward - c.b) * k, c.a};
}

float luminance(Rgba c) {
    const auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float contrast(Rgba a, Rgba b) {
    const float x = luminance(a);
    const float y = luminance(b);
    return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}

float hueOf(Rgba c) {
    const float hi = std::max({c.r, c.g, c.b});
    const float lo = std::min({c.r, c.g, c.b});
    const float d = hi - lo;
    if (d <= 0) {
        return 0;
    }
    float h = hi == c.r ? std::fmod((c.g - c.b) / d, 6.0f) : hi == c.g ? (c.b - c.r) / d + 2 : (c.r - c.g) / d + 4;
    h *= 60;
    return h < 0 ? h + 360 : h;
}

// The same colour with its hue turned (oobe.ps1 Get-Shift).
Rgba hueTurn(Rgba c, float degrees) {
    const float hi = std::max({c.r, c.g, c.b});
    const float lo = std::min({c.r, c.g, c.b});
    const float l = (hi + lo) / 2;
    const float s = hi == lo ? 0 : (hi - lo) / (1 - std::abs(2 * l - 1));
    float h = std::fmod(hueOf(c) + degrees, 360.0f);
    if (h < 0) {
        h += 360;
    }
    const float chroma = (1 - std::abs(2 * l - 1)) * s;
    const float x = chroma * (1 - std::abs(std::fmod(h / 60, 2.0f) - 1));
    const float m = l - chroma / 2;
    float r = 0, g = 0, b = 0;
    switch (static_cast<int>(h / 60)) {
    case 0: r = chroma, g = x; break;
    case 1: r = x, g = chroma; break;
    case 2: g = chroma, b = x; break;
    case 3: g = x, b = chroma; break;
    case 4: r = x, b = chroma; break;
    default: r = chroma, b = x; break;
    }
    return {std::clamp(r + m, 0.0f, 1.0f), std::clamp(g + m, 0.0f, 1.0f), std::clamp(b + m, 0.0f, 1.0f), c.a};
}

Rgba hexColor(unsigned value, float alpha = 1.0f) {
    return {static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
            static_cast<float>(value & 0xFF) / 255.0f, alpha};
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

} // namespace

// D-126: the setup screens' own colours (oobe.ps1 $palettes and Set-Colors), in the plan's theme — not the
// app's: a dark wizard is drawn dark in the light app too.
struct WelcomeCard::Look {
    bool dark = true;
    Rgba bg, text1, text2, text3, line, glass, glassHi, glassLine, field;
    Rgba g1, g2, on, accT;
};

namespace {

using Look = WelcomeCard::Look;

Look makeLook(bool dark, Rgba accent) {
    Look l;
    l.dark = dark;
    if (dark) {
        l.bg = hexColor(0x090B11), l.text1 = hexColor(0xF4F5F8), l.text2 = hexColor(0xB9BDC7), l.text3 = hexColor(0x8C919E);
        l.line = hexColor(0xFFFFFF, 0.13f), l.glass = hexColor(0xFFFFFF, 0.055f), l.glassHi = hexColor(0xFFFFFF, 0.09f);
        l.glassLine = hexColor(0xFFFFFF, 0.10f), l.field = hexColor(0xFFFFFF, 0.05f);
    } else {
        l.bg = hexColor(0xF1F1EE), l.text1 = hexColor(0x14161C), l.text2 = hexColor(0x4D525E), l.text3 = hexColor(0x646975);
        l.line = hexColor(0x000000, 0.11f), l.glass = hexColor(0xFFFFFF, 0.6f), l.glassHi = hexColor(0xFFFFFF, 0.86f);
        l.glassLine = hexColor(0x000000, 0.07f), l.field = hexColor(0xFFFFFF, 0.65f);
    }
    // Warm accents lean to rose, the others to violet; the gradient is the accent and its turned hue.
    const float hue = hueOf(accent);
    const float turn = hue < 50 || hue > 330 ? -40.0f : 40.0f;
    if (dark) {
        l.g1 = mix(accent, 1, 0.4f);
        l.g2 = mix(hueTurn(accent, turn), 1, 0.45f);
    } else {
        l.g1 = mix(accent, 0, 0.1f);
        l.g2 = hueTurn(l.g1, turn);
    }
    // Accent text: mixed until it reads on the ground (5:1); the pill's text black or white, whichever reads on both ends.
    for (float k = dark ? 0.3f : 0.15f; k <= 1.0f; k += 0.02f) {
        l.accT = mix(accent, dark ? 1.0f : 0.0f, k);
        if (contrast(l.accT, l.bg) >= 5) {
            break;
        }
    }
    const Rgba black{0, 0, 0, 1};
    const Rgba white{1, 1, 1, 1};
    l.on = std::min(contrast(l.g1, black), contrast(l.g2, black)) > std::min(contrast(l.g1, white), contrast(l.g2, white)) ? black : white;
    return l;
}

// A text line in the drawing, as a soft bar (the wizard's words are its own; the shape is what counts).
void skeleton(ui::Canvas& canvas, float x, float y, float width, Rgba color, float height = 5.0f) {
    canvas.fillRoundRect({x, y, std::max(width, 4.0f), height}, height / 2, color);
}

void miniToggle(ui::Canvas& canvas, float x, float cy, bool on, const Look& l) {
    const RectF track{x, cy - 6, 22, 12};
    if (on) {
        canvas.fillGradient(track, 6, l.g1, l.g2);
        canvas.fillEllipse({track.right() - 6, cy}, 4, l.on);
    } else {
        canvas.fillRoundRect(track, 6, l.glass);
        canvas.strokeRoundRect(track, 6, l.line, 1.0f);
        canvas.fillEllipse({track.x + 6, cy}, 3.5f, l.text3);
    }
}

// A field of the wizard: glass, a small label bar on top, the value under it; the focused one ringed in the accent.
void miniField(ui::Canvas& canvas, RectF r, const std::wstring& value, bool focused, const Look& l) {
    canvas.fillRoundRect(r, 8, focused ? l.glassHi : l.field);
    canvas.strokeRoundRect(r, 8, focused ? l.g1 : l.glassLine, focused ? 1.5f * canvas.scale() : 1.0f);
    skeleton(canvas, r.x + 10, r.y + 6, 44, withAlpha(l.text3, 0.55f), 4.0f);
    if (value.empty()) {
        skeleton(canvas, r.x + 10, r.y + 16, 70, withAlpha(l.text1, 0.55f));
    } else {
        canvas.drawText(value, {r.x + 10, r.y + 12, r.width - 20, 16}, TypeStyle::Caption, l.text1);
    }
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
    if (s.width < 160 || s.height < 120) {
        return;
    }
    const Rgba accent = parseAccent(m_plan.accent);
    const Look l = makeLook(m_plan.theme != L"light", accent);
    if (!m_on) {
        canvas.pushOpacity(0.45f);
    }
    // The screen (D-126): the theme's ground and the aurora over it, the accent and its turned hue.
    canvas.fillRoundRect(s, 10.0f, l.bg);
    canvas.pushClip({s.x + 3, s.y + 3, s.width - 6, s.height - 6});
    const Rgba turned = hueTurn(accent, hueOf(accent) < 50 || hueOf(accent) > 330 ? -40.0f : 40.0f);
    if (l.dark) {
        canvas.fillGlow({s.x + s.width * 0.19f, s.y + s.height * 0.22f}, s.width * 0.42f, s.height * 0.62f, withAlpha(mix(accent, 0, 0.05f), 0.55f));
        canvas.fillGlow({s.x + s.width * 0.9f, s.y + s.height * 0.92f}, s.width * 0.42f, s.height * 0.6f, withAlpha(turned, 0.42f));
        canvas.fillGlow({s.x + s.width * 0.62f, s.y + s.height * 0.08f}, s.width * 0.3f, s.height * 0.4f, withAlpha(mix(accent, 0, 0.45f), 0.6f));
    } else {
        canvas.fillGlow({s.x + s.width * 0.19f, s.y + s.height * 0.22f}, s.width * 0.42f, s.height * 0.62f, withAlpha(mix(accent, 1, 0.2f), 0.45f));
        canvas.fillGlow({s.x + s.width * 0.9f, s.y + s.height * 0.92f}, s.width * 0.42f, s.height * 0.6f, withAlpha(mix(turned, 1, 0.3f), 0.42f));
        canvas.fillGlow({s.x + s.width * 0.62f, s.y + s.height * 0.08f}, s.width * 0.3f, s.height * 0.4f, withAlpha(mix(accent, 1, 0.55f), 0.55f));
    }
    canvas.popClip();

    const Stage stage = static_cast<Stage>(std::clamp(m_selected, 0, static_cast<int>(kStageCount) - 1));
    std::vector<Stage> shown;
    for (std::size_t i = 0; i + 1 < kStageCount; ++i) {
        if (asked(static_cast<Stage>(i))) {
            shown.push_back(static_cast<Stage>(i));
        }
    }
    const auto at = std::ranges::find(shown, stage);
    const int index = at != shown.end() ? static_cast<int>(at - shown.begin()) : -1;

    // A lit orb: its glow, the two-colour ball, a shine, the stage's picture on it.
    const auto orb = [&](PointF c, float r, bool ring) {
        if (ring) {
            canvas.strokeRoundRect({c.x - r * 1.55f, c.y - r * 1.55f, r * 3.1f, r * 3.1f}, r * 1.55f, l.line, 1.0f);
        }
        canvas.fillGlow({c.x, c.y + r * 0.35f}, r * 1.9f, r * 1.9f, withAlpha(l.g1, l.dark ? 0.45f : 0.38f));
        canvas.fillGradient({c.x - r, c.y - r, 2 * r, 2 * r}, r, l.g1, l.g2);
        canvas.fillGlow({c.x - r * 0.35f, c.y - r * 0.42f}, r * 0.55f, r * 0.42f, Rgba{1, 1, 1, 0.42f});
    };

    if (stage == Stage::Finish) {
        // The preparing screen: the orb with its share, the heading, the steps on a glass card.
        const float r = std::min(s.height * 0.12f, s.width * 0.08f);
        const PointF c{s.x + s.width / 2, s.y + s.height * 0.08f + r * 1.6f};
        orb(c, r, true);
        canvas.drawText(L"34%", {c.x - r, c.y - 10, 2 * r, 20}, TypeStyle::Display, l.on, ui::TextAlign::Center);
        const float headY = c.y + r * 1.6f + 8;
        canvas.drawText(m_strings.get(stageName(stage)), {s.x + 20, headY, s.width - 40, 22}, TypeStyle::Display, l.text1, ui::TextAlign::Center);
        const float w = std::min(s.width * 0.56f, 320.0f);
        const RectF box{s.x + (s.width - w) / 2, headY + 30, w, std::max(s.bottom() - headY - 30 - s.height * 0.07f, 0.0f)};
        canvas.fillRoundRect(box, 14, l.glass);
        canvas.strokeRoundRect(box, 14, l.glassLine, 1.0f);
        paintStageBody(canvas, box.inset(14, 12), stage, l);
    } else {
        // Left: the numbered rail, a line through the steps; the steps behind ticked, this one lit.
        const float railX = s.x + s.width * 0.05f;
        const float rowH = std::min(s.height * 0.085f, 24.0f);
        const float dot = rowH * 0.36f;
        const float top = s.y + (s.height - rowH * static_cast<float>(std::max<std::size_t>(shown.size(), 1) - 1)) / 2;
        const float cx = railX + dot;
        if (shown.size() > 1) {
            const float last = top + rowH * static_cast<float>(shown.size() - 1);
            canvas.fillRoundRect({cx - 0.75f, top, 1.5f, last - top}, 0.75f, l.line);
            if (index > 0) {
                canvas.fillRoundRect({cx - 0.75f, top, 1.5f, rowH * static_cast<float>(index)}, 0.75f, l.g1);
            }
        }
        const float railW = s.width * 0.2f;
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const float cy = top + rowH * static_cast<float>(i);
            const bool now = static_cast<int>(i) == index;
            const bool done = static_cast<int>(i) < index;
            if (now) {
                canvas.fillGlow({cx, cy}, dot * 2.4f, dot * 2.4f, withAlpha(l.g1, 0.4f));
                canvas.fillGradient({cx - dot, cy - dot, 2 * dot, 2 * dot}, dot, l.g1, l.g2);
            } else if (done) {
                canvas.fillEllipse({cx, cy}, dot, l.bg);
                canvas.fillEllipse({cx, cy}, dot, withAlpha(l.g1, l.dark ? 0.2f : 0.16f));
                canvas.drawIcon(ui::icons::Icon::Check, {cx - dot * 0.7f, cy - dot * 0.7f}, l.accT, ui::IconVariant::Regular16, dot * 1.4f);
            } else {
                canvas.fillEllipse({cx, cy}, dot, l.bg);
                canvas.strokeRoundRect({cx - dot, cy - dot, 2 * dot, 2 * dot}, dot, l.line, 1.0f);
            }
            canvas.drawText(m_strings.get(stageName(shown[i])), {cx + dot + 8, cy - 8, railW - dot * 2 - 8, 16},
                            now ? TypeStyle::SmallSemi : TypeStyle::Small, now ? l.text1 : (done ? l.text2 : l.text3));
        }

        // Middle: the step's number, the big title, what it asks, the controls, then Back and the gradient Next.
        const float colX = s.x + s.width * 0.29f;
        const float colW = s.width * 0.38f;
        const float kickY = s.y + s.height * 0.1f;
        if (index >= 0) {
            canvas.fillRoundRect({colX, kickY + 7, 14, 2}, 1, l.g1);
            canvas.drawText(m_strings.format(Str::UnattendedCardOf, {{L"i", std::to_wstring(index + 1)}, {L"n", std::to_wstring(shown.size())}}),
                            {colX + 20, kickY, 60, 16}, TypeStyle::SmallSemi, l.accT);
        }
        canvas.drawText(m_strings.get(stageName(stage)), {colX, kickY + 20, colW, 24}, TypeStyle::Display, l.text1);
        canvas.drawText(m_strings.get(stageSub(stage)), {colX, kickY + 46, colW, 16}, TypeStyle::Small, l.text2);
        const float footY = s.bottom() - s.height * 0.1f - 26;
        const RectF area{colX, kickY + 74, colW, std::max(footY - kickY - 74 - 12, 0.0f)};
        paintStageBody(canvas, area, stage, l);
        float x = colX;
        if (index > 0) {
            const RectF back{x, footY, 34, 26};
            canvas.strokeRoundRect(back, 13, l.line, 1.0f);
            canvas.drawIcon(ui::icons::Icon::ArrowLeft, {back.x + 10, back.y + 6}, l.text1, ui::IconVariant::Regular16, 14.0f);
            x += 42;
        }
        const std::wstring next = m_strings.get(Str::UnattendedCardNext);
        const float nextW = std::ceil(canvas.text().measure(next, TypeStyle::SmallSemi)) + 44;
        const RectF pill{x, footY, nextW, 26};
        canvas.fillGlow({pill.x + pill.width / 2, pill.bottom()}, pill.width * 0.6f, 16, withAlpha(l.g1, 0.35f));
        canvas.fillGradient(pill, 13, l.g1, l.g2);
        canvas.drawText(next, {pill.x + 14, pill.y, pill.width - 34, pill.height}, TypeStyle::SmallSemi, l.on);
        canvas.drawIcon(ui::icons::Icon::ArrowRight, {pill.right() - 24, pill.y + 6}, l.on, ui::IconVariant::Regular16, 14.0f);

        // Right: the stage's scene, an orb in a ring, the theme the screens open in as a chip under it.
        const float r = std::min(s.width * 0.085f, s.height * 0.16f);
        const PointF c{s.x + s.width * 0.83f, s.y + s.height * 0.44f};
        orb(c, r, true);
        canvas.drawIcon(stageIcon(stage), {c.x - r * 0.5f, c.y - r * 0.5f}, l.on, ui::IconVariant::Regular24, r);
        const std::wstring tag = m_strings.get(l.dark ? Str::UnattendedCardThemeDark : Str::UnattendedCardThemeLight);
        const float tagW = std::ceil(canvas.text().measure(tag, TypeStyle::Small)) + 20;
        const RectF chip{c.x - tagW / 2, c.y + r * 1.55f + 10, tagW, 22};
        canvas.fillRoundRect(chip, 11, l.glassHi);
        canvas.strokeRoundRect(chip, 11, l.glassLine, 1.0f);
        canvas.drawText(tag, chip, TypeStyle::Small, l.text2, ui::TextAlign::Center);
    }

    // A stage the plan leaves out: said over the screen.
    if (m_on && !asked(stage)) {
        canvas.fillRoundRect(s, 10.0f, withAlpha(l.bg, 0.82f));
        canvas.drawText(m_strings.get(Str::UnattendedCardSkipped), s, TypeStyle::BodyStrong, l.text2, ui::TextAlign::Center);
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

void WelcomeCard::paintStageBody(ui::Canvas& canvas, RectF a, Stage stage, const Look& l) const {
    if (a.height < 20) {
        return;
    }
    const float w = std::min(a.width, 280.0f);
    const Rgba bar = withAlpha(l.text1, 0.5f);
    const Rgba barSoft = withAlpha(l.text3, 0.45f);
    switch (stage) {
    case Stage::Account: {
        miniField(canvas, {a.x, a.y, w, 30}, L"", true, l);
        miniField(canvas, {a.x, a.y + 38, w, 30}, L"••••••", false, l);
        break;
    }
    case Stage::Network: {
        for (int i = 0; i < 3 && i * 30.0f + 26 <= a.height; ++i) {
            const RectF row{a.x, a.y + i * 30.0f, w, 26};
            canvas.fillRoundRect(row, 8, i == 0 ? l.glassHi : l.glass);
            canvas.strokeRoundRect(row, 8, i == 0 ? l.g1 : l.glassLine, 1.0f);
            canvas.drawIcon(ui::icons::Icon::Network, {row.x + 8, row.y + 5}, i == 0 ? l.accT : l.text2, ui::IconVariant::Regular16, 16.0f);
            skeleton(canvas, row.x + 32, row.y + 8, 90.0f - i * 18.0f, bar);
            skeleton(canvas, row.x + 32, row.y + 16, 40, barSoft, 4.0f);
        }
        break;
    }
    case Stage::Computer: {
        miniField(canvas, {a.x, a.y, w, 30}, m_plan.computerName.empty() ? std::wstring(core::kWelcomeComputerName) : m_plan.computerName, true, l);
        const RectF zone{a.x, a.y + 38, w, 30};
        miniField(canvas, zone, m_plan.timeZone.empty() ? L"(UTC+03:00) İstanbul" : m_plan.timeZone, false, l);
        canvas.drawIcon(ui::icons::Icon::ChevronDown, {zone.right() - 22, zone.y + 8}, l.text2, ui::IconVariant::Regular16, 14.0f);
        break;
    }
    case Stage::Look: {
        // The two themes (the plan's pre-selected one ringed in the accent), then the accents.
        for (int i = 0; i < 2; ++i) {
            const RectF t{a.x + i * 74.0f, a.y, 66, 40};
            canvas.fillRoundRect(t, 8, i == 0 ? hexColor(0x101219) : hexColor(0xF4F4F1));
            canvas.fillRoundRect({t.x + 14, t.y + 8, 38, 18}, 4, i == 0 ? hexColor(0x1D2029) : hexColor(0xFFFFFF));
            canvas.fillGradient({t.x + 18, t.y + 19, 16, 4}, 2, l.g1, l.g2);
            if ((i == 0) == l.dark) {
                canvas.strokeRoundRect({t.x - 3, t.y - 3, t.width + 6, t.height + 6}, 10, l.g1, 1.5f * canvas.scale());
            } else {
                canvas.strokeRoundRect(t, 8, l.glassLine, 1.0f);
            }
        }
        float x = a.x + 7;
        for (const auto& [key, hex] : core::kWelcomeAccents) {
            const PointF centre{x, a.y + 62};
            canvas.fillEllipse(centre, 7, parseAccent(hex));
            if (m_plan.accent == hex) {
                canvas.strokeRoundRect({centre.x - 10, centre.y - 10, 20, 20}, 10, l.text1, 1.5f * canvas.scale());
            }
            x += 20;
        }
        break;
    }
    case Stage::Wallpaper: {
        for (int i = 0; i < 6; ++i) {
            const RectF t{a.x + (i % 3) * 54.0f, a.y + (i / 3) * 36.0f, 48, 30};
            const Rgba base = hueTurn(parseAccent(m_plan.accent), static_cast<float>(i) * 32.0f);
            canvas.fillGradient(t, 7, mix(base, 1, 0.35f), mix(base, 0, 0.35f));
            if (i == 1) {
                canvas.strokeRoundRect({t.x - 3, t.y - 3, t.width + 6, t.height + 6}, 9, l.g1, 1.5f * canvas.scale());
            }
        }
        break;
    }
    case Stage::Bundles:
    case Stage::Prefs: {
        const int rows = stage == Stage::Bundles ? 3 : 4;
        for (int i = 0; i < rows && i * 24.0f + 20 <= a.height; ++i) {
            const RectF row{a.x, a.y + i * 24.0f, w, 20};
            canvas.fillRoundRect(row, 6, l.glass);
            skeleton(canvas, row.x + 10, row.y + 8, 100.0f - static_cast<float>((i * 23) % 50), bar);
            miniToggle(canvas, row.right() - 30, row.y + 10, stage == Stage::Prefs ? i % 3 != 1 : i == 0, l);
        }
        break;
    }
    case Stage::Privacy: {
        const bool strict = m_plan.privacy != L"windows";
        for (int i = 0; i < 2; ++i) {
            const RectF card{a.x, a.y + i * 42.0f, w, 36};
            const bool chosen = (i == 0) == strict;
            canvas.fillRoundRect(card, 10, chosen ? l.glassHi : l.glass);
            canvas.strokeRoundRect(card, 10, chosen ? l.g1 : l.glassLine, chosen ? 1.5f * canvas.scale() : 1.0f);
            if (chosen) {
                canvas.fillGradient({card.x + 9, card.y + 12, 12, 12}, 6, l.g1, l.g2);
            } else {
                canvas.strokeRoundRect({card.x + 9, card.y + 12, 12, 12}, 6, l.line, 1.0f);
            }
            skeleton(canvas, card.x + 30, card.y + 11, 70, bar);
            skeleton(canvas, card.x + 30, card.y + 21, 130, barSoft, 4.0f);
        }
        break;
    }
    case Stage::Finish:
    case Stage::Count: {
        // What is being done, ticked one by one; the one at work ringed.
        for (int i = 0; i < 4 && i * 22.0f + 16 <= a.height; ++i) {
            const float cy = a.y + 8 + i * 22.0f;
            if (i < 2) {
                canvas.fillGradient({a.x, cy - 7, 14, 14}, 7, l.g1, l.g2);
                canvas.drawIcon(ui::icons::Icon::Check, {a.x + 2, cy - 5}, l.on, ui::IconVariant::Regular16, 10.0f);
            } else {
                canvas.strokeRoundRect({a.x, cy - 7, 14, 14}, 7, i == 2 ? l.g1 : l.line, i == 2 ? 1.5f * canvas.scale() : 1.0f);
            }
            skeleton(canvas, a.x + 24, cy - 3, 140.0f - static_cast<float>(i * 19), i == 2 ? withAlpha(l.text1, 0.75f) : bar);
        }
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
