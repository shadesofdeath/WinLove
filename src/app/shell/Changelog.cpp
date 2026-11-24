#include "app/shell/Changelog.h"

#include "app/Format.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;
namespace spacing = ui::tokens::spacing;

namespace {

constexpr float kHeader = 76.0f; // the title, the facts and the buttons under the title bar

// The window's own title bar: drags the window (HitZone::Caption), the brand and the title.
class ChangelogBar : public ui::Widget {
public:
    std::wstring title;
    bool active = true;
    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.fillRect(b, Color::BgTitleBar);
        const float x = b.x + spacing::s5;
        canvas.drawIcon(ui::icons::Icon::BrandMark, {x, b.y + std::round((b.height - size::icon) / 2)}, Color::AccentBase);
        const float tx = x + size::icon + spacing::s4;
        canvas.drawText(title, {tx, b.y, std::max(b.right() - 3 * size::captionButton - tx - 8, 0.0f), b.height}, TypeStyle::Body,
                        active ? Color::TextPrimary : Color::TextTertiary);
    }
    [[nodiscard]] ui::HitZone windowZone() const override { return ui::HitZone::Caption; }
};

std::wstring publishedText(std::int64_t published, Language language, const Localization& strings, std::int64_t now) {
    if (published <= 0) {
        return L"—";
    }
    using std::chrono::seconds;
    using std::chrono::system_clock;
    return formatRecentTime(system_clock::time_point(seconds(published)), language, strings, system_clock::time_point(seconds(now)));
}

} // namespace

std::vector<ui::MarkdownView::Section> changelogSections(const core::ReleaseCheck& check, const Localization& strings, Language language,
                                                         std::int64_t now, std::size_t older) {
    std::vector<ui::MarkdownView::Section> sections;
    const core::ChangelogParts parts = core::changelogParts(check, older);
    const auto running = core::parseAppVersion(check.current);
    auto add = [&](const core::Release& r, bool fresh, std::wstring divider) {
        ui::MarkdownView::Section s;
        s.divider = std::move(divider);
        s.title = r.title();
        s.meta = strings.format(Str::ChangelogMeta, {{L"tag", r.tag}, {L"date", publishedText(r.published, language, strings, now)}});
        if (fresh) {
            s.tags.push_back({strings.get(Str::ChangelogNewTag), Color::TextOnAccent, Color::AccentBase});
        } else if (running && r.version && *r.version == *running) {
            s.tags.push_back({strings.get(Str::ChangelogCurrentTag), Color::StatusSuccess, Color::StatusSuccessSubtle});
        }
        if (r.prerelease) {
            s.tags.push_back({strings.get(Str::ChangelogPreTag), Color::TextSecondary, Color::BgHover});
        }
        s.body = markdown::parse(r.body);
        s.emptyBody = strings.get(Str::ChangelogEmpty);
        sections.push_back(std::move(s));
    };
    for (const std::size_t i : parts.newer) {
        add(check.releases[i], true, {});
    }
    for (std::size_t k = 0; k < parts.older.size(); ++k) {
        // The older ones under their own line (none when there is nothing newer above them).
        add(check.releases[parts.older[k]], false, k == 0 && !parts.newer.empty() ? strings.get(Str::ChangelogOlder) : std::wstring());
    }
    return sections;
}

std::wstring changelogSubtitle(const core::ReleaseCheck& check, const Localization& strings) {
    const std::size_t newer = core::changelogParts(check, 0).newer.size();
    return newer > 0 ? strings.format(Str::ChangelogSubtitleNew, {{L"current", check.current}, {L"n", std::to_wstring(newer)}})
                     : strings.format(Str::ChangelogSubtitleCurrent, {{L"current", check.current}});
}

ChangelogView::ChangelogView(const Localization& strings, Language language, Services services)
    : m_strings(strings), m_language(language), m_services(std::move(services)) {
    m_title = strings.get(Str::ChangelogTitle);
    m_hint = strings.get(Str::ChangelogHint);
    auto& bar = add<ChangelogBar>();
    bar.title = strings.get(Str::ChangelogWindowTitle);
    m_minimize = &bar.add<CaptionButton>(CaptionButton::Kind::Minimize);
    m_maximize = &bar.add<CaptionButton>(CaptionButton::Kind::Maximize);
    m_close = &bar.add<CaptionButton>(CaptionButton::Kind::Close);
    m_minimize->setAccessible(ui::AccessRole::Button, strings.get(Str::TitleMinimize));
    m_maximize->setAccessible(ui::AccessRole::Button, strings.get(Str::TitleMaximize));
    m_close->setAccessible(ui::AccessRole::Button, strings.get(Str::CommonClose));
    m_minimize->onInvoke = [this] {
        if (m_services.minimize) {
            m_services.minimize();
        }
    };
    m_maximize->onInvoke = [this] {
        if (m_services.toggleMaximize) {
            m_services.toggleMaximize();
        }
    };
    m_close->onInvoke = [this] {
        if (m_services.close) {
            m_services.close();
        }
    };
    m_page = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::AppUpdateOpenRelease), ui::icons::Icon::ExternalLink);
    m_page->onInvoke = [this] {
        if (m_services.openRelease) {
            m_services.openRelease();
        }
    };
    m_install = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::AppUpdateSelfInstall), ui::icons::Icon::UpdateDownload);
    m_install->onInvoke = [this] {
        if (m_services.install) {
            m_services.install();
        }
    };
    m_install->setVisible(false);
    m_notes = &add<ui::MarkdownView>();
    m_notes->setPlaceholder(strings.get(Str::ChangelogNone));
    m_notes->onLink = [this](const std::wstring& url) {
        if (m_services.openLink) {
            m_services.openLink(url);
        }
    };
    setAccessible(ui::AccessRole::Group, m_title);
}

void ChangelogView::setCheck(const core::ReleaseCheck& check, std::int64_t now) {
    m_subtitle = changelogSubtitle(check, m_strings);
    m_notes->setSections(changelogSections(check, m_strings, m_language, now));
    invalidate();
}

void ChangelogView::setInstallable(bool installable) {
    m_install->setVisible(installable && m_services.install);
    layout();
    invalidate();
}

void ChangelogView::setMaximized(bool maximized) {
    m_maximize->setMaximized(maximized);
}

void ChangelogView::setWindowActive(bool active) {
    m_windowActive = active;
    if (!children().empty()) {
        if (auto* bar = dynamic_cast<ChangelogBar*>(children().front().get())) {
            bar->active = active;
        }
    }
    for (CaptionButton* b : {m_minimize, m_maximize, m_close}) {
        b->setWindowActive(active);
    }
    invalidate();
}

void ChangelogView::layout() {
    const RectF b = bounds();
    ui::Widget& bar = *children().front();
    bar.setBounds({b.x, b.y, b.width, size::titleBar});
    const float button = size::captionButton;
    m_close->setBounds({b.right() - button, b.y, button, size::titleBar});
    m_maximize->setBounds({m_close->bounds().x - button, b.y, button, size::titleBar});
    m_minimize->setBounds({m_maximize->bounds().x - button, b.y, button, size::titleBar});
    // The header's buttons at its right, the main one last.
    const float headerY = b.y + size::titleBar;
    float right = b.right() - spacing::s7;
    for (ui::Button* action : {m_install, m_page}) {
        if (!action->visible()) {
            continue;
        }
        const ui::SizeF s = action->measure({});
        action->setBounds({right - s.width, headerY + std::round((kHeader - s.height) / 2), s.width, s.height});
        right -= s.width + spacing::s4;
    }
    m_notes->setBounds({b.x, headerY + kHeader, b.width, std::max(b.bottom() - headerY - kHeader, 0.0f)});
}

void ChangelogView::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgBase);
    const float headerY = b.y + size::titleBar;
    const float x = b.x + spacing::s7;
    const ui::Button* first = m_page->visible() ? m_page : m_install;
    const float room = std::max((first && first->visible() ? first->bounds().x : b.right()) - 16 - x, 0.0f);
    canvas.drawText(m_title, {x, headerY + 14, room, 22}, TypeStyle::Display, Color::TextPrimary);
    canvas.drawText(m_subtitle, {x, headerY + 38, room, 18}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_hint, {x, headerY + 54, room, 16}, TypeStyle::Small, Color::TextTertiary);
    canvas.hairlineH(b.x, headerY + kHeader - 1, b.width, Color::LineSubtle);
}

bool ChangelogView::onKeyDown(const ui::KeyEvent& key) {
    if (key.virtualKey == VK_ESCAPE) {
        if (m_services.close) {
            m_services.close();
        }
        return true;
    }
    // Copy, select all and scrolling work from anywhere in the window.
    return m_notes->onKeyDown(key);
}

ui::HitZone ChangelogView::windowZone() const {
    return ui::HitZone::Client;
}

} // namespace wl::app
