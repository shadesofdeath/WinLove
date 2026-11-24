#include "app/pages/AboutPage.h"

#include "app/Format.h"
#include "app/SystemInfo.h"
#include "app/pages/PageBits.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// D-114 (user: "daha modern, profesyonel"): a hero card (the mark on an accent tile, name,
// version, what WinLove promises as pills), the made-with-love card, then Bağlantılar and Sistem
// side by side as cards. The two links that matter, the licenses and the logs are the ribbon.
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
// D-140: the Güncellemeler card — a band (status icon, title, one detail line, the buttons at its
// right); a new release's first lines under it.
constexpr float kUpdateBand = 60.0f;
constexpr float kUpdateIcon = ui::tokens::size::iconLarge;
constexpr float kUpdateTitleH = 18.0f;
constexpr float kUpdateLineH = 16.0f;
constexpr int kNotesLines = 5;
constexpr float kButtonGap = 8.0f;

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

std::chrono::system_clock::time_point fromSeconds(std::int64_t seconds) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

Color toneInk(UpdateCardView::Tone tone) {
    switch (tone) {
    case UpdateCardView::Tone::Success: return Color::StatusSuccess;
    case UpdateCardView::Tone::Accent: return Color::AccentText;
    case UpdateCardView::Tone::Warning: return Color::StatusWarning;
    case UpdateCardView::Tone::Quiet: break;
    }
    return Color::TextTertiary;
}
} // namespace

namespace {
UpdateCardView baseCardView(const AppState& state, const Localization& strings, Language language, std::int64_t now);
} // namespace

// D-145: the check's card, then what "Yükle" is doing over it.
UpdateCardView updateCardView(const AppState& state, const Localization& strings, Language language, std::int64_t now) {
    UpdateCardView v = baseCardView(state, strings, language, now);
    const auto& u = state.appUpdate();
    v.changelog = !v.checking && u.result && !u.result->releases.empty();
    v.install = v.available;
    if (const auto& install = u.install) {
        using Stage = AppState::AppUpdate::Install::Stage;
        using Why = AppState::AppUpdate::Install::Why;
        v.install = false;
        switch (install->stage) {
        case Stage::Downloading:
        case Stage::Verifying: {
            const bool verifying = install->stage == Stage::Verifying;
            v.icon = ui::icons::Icon::UpdateDownload;
            v.tone = UpdateCardView::Tone::Accent;
            v.title = verifying ? strings.get(Str::AppUpdateSelfVerifyingTitle)
                                : strings.format(Str::AppUpdateSelfDownloadingTitle, {{L"name", install->name}});
            v.detail = verifying ? strings.get(Str::AppUpdateSelfVerifyingBody)
                                 : strings.format(Str::AppUpdateSelfDownloadingBody, {{L"done", formatBytes(install->done, language)},
                                                                                     {L"total", formatBytes(install->total, language)}});
            v.installing = true;
            v.progress = verifying ? 1.0f
                         : install->total ? std::clamp(static_cast<float>(static_cast<double>(install->done) / static_cast<double>(install->total)), 0.0f, 1.0f)
                                          : 0.0f;
            v.progressText = strings.format(Str::ApplyPct, {{L"pct", std::to_wstring(static_cast<int>(v.progress * 100))}});
            v.notes.clear();
            break;
        }
        case Stage::Ready:
            v.icon = ui::icons::Icon::SuccessCircle;
            v.tone = UpdateCardView::Tone::Success;
            v.title = strings.format(Str::AppUpdateSelfReadyTitle, {{L"name", install->name}});
            v.detail = strings.format(Str::AppUpdateSelfReadyBody, {{L"file", L"WinLove.exe.old"}});
            v.restart = true;
            v.notes.clear();
            break;
        case Stage::Failed: {
            v.icon = ui::icons::Icon::WarningTriangle;
            v.tone = UpdateCardView::Tone::Warning;
            v.title = strings.get(Str::AppUpdateSelfFailedTitle);
            const std::wstring detail = install->detail;
            switch (install->why) {
            case Why::NoAsset: v.detail = strings.get(Str::AppUpdateSelfNoAsset); v.openDownload = true; break;
            case Why::Mounted: v.detail = strings.get(Str::AppUpdateSelfMounted); v.retry = true; break;
            case Why::Busy: v.detail = strings.get(Str::AppUpdateSelfBusy); v.retry = true; break;
            case Why::NotWritable:
                v.detail = strings.format(Str::AppUpdateSelfNotWritable, {{L"detail", detail}});
                v.openDownload = true;
                break;
            case Why::Download: v.detail = strings.format(Str::AppUpdateSelfDownload, {{L"detail", detail}}); v.retry = true; break;
            case Why::Verify:
                v.detail = strings.format(Str::AppUpdateSelfVerify, {{L"detail", detail}});
                v.openDownload = true;
                break;
            case Why::Swap: v.detail = strings.format(Str::AppUpdateSelfSwap, {{L"detail", detail}}); v.openDownload = true; break;
            case Why::Restart:
                v.detail = strings.format(Str::AppUpdateSelfRestartFailed, {{L"detail", detail}});
                v.openDownload = true;
                break;
            case Why::Cancelled: v.detail = strings.get(Str::AppUpdateSelfCancelled); v.retry = true; break;
            case Why::None: v.retry = true; break;
            }
            v.install = v.retry && v.available;
            v.retry = v.install;
            if (v.url.empty() && u.result && u.result->latest) {
                v.url = core::releasePageUrl(u.result->latest->url);
            }
            break;
        }
        }
    }
    return v;
}

namespace {
UpdateCardView baseCardView(const AppState& state, const Localization& strings, Language language, std::int64_t now) {
    using Phase = AppState::AppUpdate::Phase;
    const auto& u = state.appUpdate();
    const AppSettings& settings = state.settings();
    const auto nowPoint = fromSeconds(now);
    auto when = [&](std::int64_t seconds) { return formatRecentTime(fromSeconds(seconds), language, strings, nowPoint); };
    UpdateCardView v;
    switch (u.phase) {
    case Phase::Checking:
        v.icon = ui::icons::Icon::Refresh;
        v.title = strings.get(Str::AppUpdateChecking);
        v.detail = strings.get(Str::AppUpdateCheckingBody);
        v.checking = true;
        return v;
    case Phase::Failed: {
        v.icon = ui::icons::Icon::WarningTriangle;
        v.tone = UpdateCardView::Tone::Warning;
        v.title = strings.get(Str::AppUpdateFailedTitle);
        switch (core::releaseFailure(u.error)) {
        case core::ReleaseFailure::RateLimit: v.detail = strings.get(Str::AppUpdateFailedRateLimit); break;
        case core::ReleaseFailure::Http: v.detail = strings.format(Str::AppUpdateFailedHttp, {{L"detail", u.error.message}}); break;
        case core::ReleaseFailure::BadAnswer: v.detail = strings.get(Str::AppUpdateFailedAnswer); break;
        case core::ReleaseFailure::Cancelled:
        case core::ReleaseFailure::Network: v.detail = strings.get(Str::AppUpdateFailedNetwork); break;
        }
        return v;
    }
    case Phase::Done:
        if (u.result) {
            const auto& c = *u.result;
            const auto* latest = c.latest ? &*c.latest : nullptr;
            const std::wstring published = latest && latest->published > 0 ? when(latest->published) : std::wstring(L"—");
            switch (c.verdict) {
            case core::ReleaseVerdict::Available:
                if (latest) {
                    v.icon = ui::icons::Icon::UpdateDownload;
                    v.tone = UpdateCardView::Tone::Accent;
                    v.title = strings.format(Str::AppUpdateAvailableTitle, {{L"name", latest->title()}});
                    v.detail = strings.format(Str::AppUpdateAvailableBody,
                                              {{L"tag", latest->tag}, {L"date", published}, {L"current", c.current}});
                    v.notes = latest->notes;
                    v.available = true;
                    v.url = core::releasePageUrl(latest->url);
                    return v;
                }
                break;
            case core::ReleaseVerdict::Current:
                if (latest) {
                    v.icon = ui::icons::Icon::SuccessCircle;
                    v.tone = UpdateCardView::Tone::Success;
                    v.title = strings.get(Str::AppUpdateCurrentTitle);
                    v.detail = strings.format(Str::AppUpdateCurrentBody, {{L"tag", latest->tag}, {L"date", published}, {L"when", when(u.at)}});
                    return v;
                }
                break;
            case core::ReleaseVerdict::Unknown:
                v.icon = ui::icons::Icon::InfoCircle;
                v.title = strings.get(Str::AppUpdateUnknownTitle);
                v.detail = strings.format(Str::AppUpdateUnknownBody,
                                          {{L"tag", latest ? latest->tag : std::wstring(L"—")}, {L"current", c.current}});
                return v;
            case core::ReleaseVerdict::None:
                v.icon = ui::icons::Icon::InfoCircle;
                v.title = strings.get(Str::AppUpdateNoneTitle);
                v.detail = strings.format(Str::AppUpdateLastCheckTitle, {{L"when", when(u.at)}});
                return v;
            }
        }
        break;
    case Phase::Idle: break;
    }
    // Not asked this session: what the settings remember.
    if (!u.updatedFrom.empty()) {
        // D-145: this instance replaced the version it names.
        v.icon = ui::icons::Icon::SuccessCircle;
        v.tone = UpdateCardView::Tone::Success;
        v.title = strings.format(Str::AppUpdateSelfUpdatedTitle, {{L"from", u.updatedFrom}, {L"to", L"" WL_VERSION_LABEL}});
        v.detail = strings.get(Str::AppUpdateSelfUpdatedBody);
        return v;
    }
    v.icon = ui::icons::Icon::Refresh;
    v.title = settings.updateCheckedAt > 0 ? strings.format(Str::AppUpdateLastCheckTitle, {{L"when", when(settings.updateCheckedAt)}})
                                           : strings.get(Str::AppUpdateIdleTitle);
    v.detail = strings.get(settings.checkUpdates ? Str::AppUpdateAutoOn : Str::AppUpdateAutoOff);
    if (settings.updateCheckedAt > 0 && !settings.updateLatestTag.empty()) {
        v.detail = strings.format(Str::AppUpdateLastKnown, {{L"tag", settings.updateLatestTag}}) + L" · " + v.detail;
    }
    return v;
}
} // namespace

AboutPage::AboutPage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_language(language) {
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
    for (const Str pill : {Str::AboutPillFree, Str::AboutPillNoAds, Str::AboutPillNoData, Str::AboutPillOpenSource}) {
        m_pills.push_back({pill, 0.0f}); // measured in layout(), once a host has the fonts
    }
    // D-140: the update card's buttons — the check, and the new release's page (the card's one main action).
    m_releaseButton = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::AppUpdateOpenRelease), ui::icons::Icon::ExternalLink);
    m_releaseButton->onInvoke = [this, open = intents.openLink] {
        if (open && !m_update.url.empty()) {
            open(m_update.url);
        }
    };
    m_checkButton = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AppUpdateCheckNow), ui::icons::Icon::Refresh);
    m_checkButton->onInvoke = [check = intents.checkUpdates] {
        if (check) {
            check();
        }
    };
    // D-145: install it, see what changed, the download's stop, the restart.
    auto call = [](std::function<void()> f) {
        return [f = std::move(f)] {
            if (f) {
                f();
            }
        };
    };
    m_installButton = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::AppUpdateSelfInstall), ui::icons::Icon::UpdateDownload);
    m_installButton->onInvoke = call(intents.install);
    m_changelogButton = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AppUpdateSelfChangelog), ui::icons::Icon::File);
    m_changelogButton->onInvoke = call(intents.changelog);
    m_restartButton = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::AppUpdateSelfRestart), ui::icons::Icon::Refresh);
    m_restartButton->onInvoke = call(intents.restart);
    m_cancelButton = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), ui::icons::Icon::Stop);
    m_cancelButton->onInvoke = call(intents.cancelInstall);
    setAccessible(ui::AccessRole::Group, strings.get(Str::AboutTitle));
    // The work folder row follows the settings; the update card the check (and its setting).
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Settings) {
            refresh();
            refreshUpdate();
        } else if (change == AppState::Change::AppUpdate) {
            refreshUpdate();
        }
    });
    refresh();
    refreshUpdate();
}

void AboutPage::refreshUpdate() {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    UpdateCardView view = updateCardView(m_state, m_strings, m_language, now);
    if (view == m_update && m_checkButton->enabled() == (!view.checking && !view.installing)) {
        return;
    }
    const float before = contentHeight();
    m_update = std::move(view);
    m_checkButton->setEnabled(!m_update.checking && !m_update.installing);
    // D-145: "Yükle" replaces the release page's button; the page stays for what cannot be installed.
    m_releaseButton->setVisible(m_update.openDownload);
    m_releaseButton->setText(m_strings.get(Str::AppUpdateSelfOpenDownload));
    m_installButton->setVisible(m_update.install);
    m_installButton->setText(m_strings.get(m_update.retry ? Str::AppUpdateSelfRetry : Str::AppUpdateSelfInstall));
    m_changelogButton->setVisible(m_update.changelog);
    m_restartButton->setVisible(m_update.restart);
    m_cancelButton->setVisible(m_update.installing);
    m_notesWidth = -1.0f;
    layout();
    invalidate();
    contentChanged(*this, before);
}

float AboutPage::updateTextX(const RectF& card) const {
    return card.x + kCardPad + kUpdateIcon + 12;
}

float AboutPage::updateNotesHeight(float width) const {
    if (m_update.notes.empty()) {
        return 0.0f;
    }
    if (!host()) {
        return kUpdateLineH * 2;
    }
    if (width != m_notesWidth) {
        m_notesWidth = width;
        const float measured = std::ceil(host()->text().measureWrapped(m_update.notes, TypeStyle::Small, width));
        m_notesHeight = std::min(measured, kUpdateLineH * kNotesLines);
    }
    return m_notesHeight;
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
    m_rowHeights.clear();
    layout();
    invalidate();
}

float AboutPage::freeBodyHeight(float width) const {
    // Measured when attached; before that a rough estimate (12px text, about 6.4px per character).
    if (host()) {
        if (width != m_freeBodyWidth) {
            m_freeBodyWidth = width;
            m_freeBodyHeight = std::ceil(host()->text().measureWrapped(m_strings.get(Str::AboutFreeBody), TypeStyle::Body, width));
        }
        return m_freeBodyHeight;
    }
    const float perLine = std::max(1.0f, std::floor(width / 6.4f));
    const auto chars = static_cast<float>(m_strings.get(Str::AboutFreeBody).size());
    return std::ceil(chars / perLine) * kBodyLine;
}

float AboutPage::systemRowHeight(std::size_t row, float cardWidth) const {
    if (row >= m_rows.size() || !host()) {
        return kRow;
    }
    if (cardWidth != m_rowHeightsWidth || m_rowHeights.size() != m_rows.size()) {
        m_rowHeightsWidth = cardWidth;
        m_rowHeights.assign(m_rows.size(), -1.0f);
    }
    float& height = m_rowHeights[row];
    if (height < 0) {
        const auto& [value, mono] = m_rows[row].second;
        const float width = std::max(cardWidth - 2 * kCardPad - kLabelWidth, 40.0f);
        const float text = std::ceil(host()->text().measureWrapped(value, mono ? TypeStyle::Mono : TypeStyle::Body, width));
        height = std::max(kRow, text + 14.0f);
    }
    return height;
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
    // D-140: the update card; a new release's lines under its band.
    const float notesH = m_update.installing ? kUpdateLineH : updateNotesHeight(std::max(b.right() - kCardPad - updateTextX(b), 40.0f));
    c.update = {b.x, c.hero.bottom() + kGap, b.width, kUpdateBand + (notesH > 0 ? notesH + kCardPad : 0.0f)};
    const float loveBody = freeBodyHeight(b.width - 2 * kCardPad);
    c.love = {b.x, c.update.bottom() + kGap, b.width, kCardPad + kLoveRow + loveBody + kCardPad};
    const float linksH = kCardTitle + kRow * static_cast<float>(m_links.size()) + kCardPad;
    const float top = c.love.bottom() + kGap;
    const bool twoColumns = b.width >= kTwoColumns;
    const float systemW = twoColumns ? (b.width - kGap) / 2 : b.width;
    const float systemH = kCardTitle + systemRowsHeight(systemW) + kCardPad;
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
    for (Pill& pill : m_pills) {
        if (pill.width <= 0 && host()) {
            pill.width = std::ceil(textWidth(m_strings.get(pill.text), TypeStyle::Caption)) + 20;
        }
    }
    float y = c.links.y + kCardTitle;
    for (const auto& item : m_links) {
        const ui::SizeF size = item.button->measure({});
        const float maxWidth = c.links.width - kCardPad - kLabelWidth - kCardPad + 8;
        item.button->setBounds({c.links.x + kCardPad + kLabelWidth - 8, y + (kRow - size.height) / 2, std::min(size.width, maxWidth),
                                size.height});
        y += kRow;
    }
    // D-140: the update card's buttons at the band's right, the main one (the release page) first.
    float right = c.update.right() - kCardPad;
    for (ui::Button* button : {m_checkButton, m_changelogButton, m_cancelButton, m_releaseButton, m_installButton, m_restartButton}) {
        if (!button->visible()) {
            continue;
        }
        const ui::SizeF size = button->measure({});
        button->setBounds({right - size.width, c.update.y + (kUpdateBand - size.height) / 2, size.width, size.height});
        right -= size.width + kButtonGap;
    }
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
    const float textRoom = std::max(c.hero.right() - kCardPad - tx, 0.0f);
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
    for (const Pill& pill : m_pills) {
        if (px + pill.width > tx + textRoom) {
            break;
        }
        const RectF r{px, pillY, pill.width, kPill};
        canvas.fillRoundRect(r, kPill / 2, Color::AccentSubtle);
        canvas.drawText(m_strings.get(pill.text), r, TypeStyle::Caption, Color::AccentText, ui::TextAlign::Center);
        px += pill.width + 6;
    }

    // D-140: Güncellemeler — the state's icon, what it found, one line of facts; the buttons at the right.
    canvas.fillRoundRect(c.update, radius, Color::BgRaised);
    {
        const Color ink = toneInk(m_update.tone);
        canvas.drawIcon(m_update.icon, {c.update.x + kCardPad, c.update.y + (kUpdateBand - kUpdateIcon) / 2}, ink,
                        ui::IconVariant::Regular16, kUpdateIcon);
        const float ux = updateTextX(c.update);
        float firstX = c.update.right();
        for (const ui::Button* button : {m_checkButton, m_changelogButton, m_cancelButton, m_releaseButton, m_installButton, m_restartButton}) {
            if (button->visible()) {
                firstX = std::min(firstX, button->bounds().x);
            }
        }
        const float room = std::max(firstX - 16 - ux, 0.0f);
        const float uy = c.update.y + std::round((kUpdateBand - kUpdateTitleH - kUpdateLineH) / 2);
        canvas.drawText(m_update.title, {ux, uy, room, kUpdateTitleH}, TypeStyle::BodySemi,
                        m_update.tone == UpdateCardView::Tone::Accent ? Color::AccentText : Color::TextPrimary);
        canvas.drawText(m_update.detail, {ux, uy + kUpdateTitleH, room, kUpdateLineH}, TypeStyle::Caption, Color::TextTertiary);
        if (m_update.installing) {
            // D-145: the download's meter under the band, its percentage at the right.
            const float top = c.update.y + kUpdateBand;
            const float w = std::max(c.update.right() - kCardPad - ux, 40.0f);
            const float pctW = std::ceil(textWidth(m_update.progressText, TypeStyle::Caption)) + 12;
            canvas.hairlineH(ux, top, w, Color::LineSubtle);
            paintMeter(canvas, {ux, top + kCardPad / 2 + 5, std::max(w - pctW, 0.0f), 6}, m_update.progress);
            canvas.drawText(m_update.progressText, {ux + w - pctW + 12, top + kCardPad / 2, pctW, 16}, TypeStyle::Caption,
                            Color::TextSecondary);
        } else if (!m_update.notes.empty()) {
            const float notesW = std::max(c.update.right() - kCardPad - ux, 40.0f);
            const float notesH = updateNotesHeight(notesW);
            const float top = c.update.y + kUpdateBand;
            canvas.hairlineH(ux, top, notesW, Color::LineSubtle);
            canvas.pushClip({ux, top + kCardPad / 2, notesW, notesH});
            canvas.drawTextWrapped(m_update.notes, {ux, top + kCardPad / 2, notesW, notesH + kUpdateLineH}, TypeStyle::Small,
                                   Color::TextSecondary);
            canvas.popClip();
        }
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
