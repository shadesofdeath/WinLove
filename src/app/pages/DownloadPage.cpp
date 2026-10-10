#include "app/pages/DownloadPage.h"

#include "app/Format.h"
#include "core/system/Files.h"
#include "core/system/Privileges.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/Toggle.h"
#include "ui/anim/Tween.h"
#include "ui/widget/Host.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using core::uup::BuildKind;

namespace {

constexpr float kTop = 12.0f;
constexpr float kGap = 12.0f;
constexpr float kPanel = 320.0f;
constexpr float kPad = 16.0f;
constexpr float kLine = 16.0f;
constexpr float kControl = 24.0f;
enum Column : int { kVersion, kBuildCol, kArchCol, kDateCol };

std::uint64_t unixToFiletime(std::int64_t seconds) {
    return seconds <= 0 ? 0 : (static_cast<std::uint64_t>(seconds) + 11644473600ull) * 10'000'000ull;
}

std::wstring archLabel(std::wstring_view arch) {
    return arch == L"amd64" ? L"x64" : arch == L"arm64" ? L"ARM64" : std::wstring(arch);
}

// This PC's display language as UUP names it ("tr-tr"); English when it is not in the list.
std::wstring preferredLanguage(const std::vector<core::uup::Language>& list) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    std::wstring mine = GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0 ? text::lower(name) : L"en-us";
    for (const auto& l : list) {
        if (l.code == mine) {
            return mine;
        }
    }
    for (const auto& l : list) {
        if (l.code == L"en-us") {
            return l.code;
        }
    }
    return list.empty() ? std::wstring() : list.front().code;
}

// "tr-tr" → "Türkçe (Türkiye)": Windows' own name for it, in its display language; the API's
// English name when Windows has none.
std::wstring localLanguageName(const core::uup::Language& language) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH * 2] = {};
    if (GetLocaleInfoEx(language.code.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, name, static_cast<int>(std::size(name))) > 0) {
        return name;
    }
    return language.name;
}

// A Store app's name in the UI language (strings uupApps.<key>), else from its id
// ("Microsoft.WindowsCalculator" → "WindowsCalculator").
std::wstring appName(const Localization& strings, const std::wstring& id) {
    const std::string key = "uupApps." + utf8::fromWide(core::uup::appKey(id));
    for (std::size_t i = 0; i < kStrCount; ++i) {
        if (key == kStrKeys[i]) {
            return strings.get(static_cast<Str>(i));
        }
    }
    std::wstring name = id.substr(0, id.find(L'_'));
    if (const auto dot = name.find_last_of(L'.'); dot != std::wstring::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}

std::filesystem::path downloadsFolder() {
    PWSTR path = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) {
        out = path;
    }
    CoTaskMemFree(path);
    return out;
}

} // namespace

// ---- the panel: what to make of the selected build ------------------------------------------

class DownloadPage::Panel : public ui::Widget {
public:
    // The Store apps of the selection. Windows 10's sets have no app set of their own: the apps are
    // in the edition's image (Microsoft.ModernApps.*.esd); other builds without one have none.
    enum class AppList : std::uint8_t { Loading, Ready, Failed, InImage, NotOffered };

    Panel(const Localization& strings, Language language, const AppState& state)
        : m_strings(strings), m_language(language), m_state(state) {
        m_lang = &add<ui::Dropdown>(L"", std::vector<std::wstring>{}, 0);
        m_lang->onChange = [this](int i) {
            if (i >= 0 && i < static_cast<int>(m_languages.size()) && onLanguage) {
                onLanguage(m_languages[static_cast<std::size_t>(i)].code);
            }
        };
        m_updates = &add<ui::CheckField>(strings.get(Str::DownloadUpdates), true);
        m_updates->onChange = [this](bool) {
            m_edge->setEnabled(m_updates->checked());
            invalidate();
        };
        m_edge = &add<ui::CheckField>(strings.get(Str::DownloadEdge), true);
        m_apps = &add<ui::CheckField>(strings.get(Str::DownloadApps), true);
        m_apps->onChange = [this](bool) {
            refreshVisibility();
            invalidate();
            changed();
        };
        m_pick = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::DownloadAppsPick));
        m_pick->onInvoke = [this] {
            if (onPickApps) {
                onPickApps();
            }
        };
        m_netFx3 = &add<ui::CheckField>(strings.get(Str::DownloadNetFx3), false);
        m_resetBase = &add<ui::CheckField>(strings.get(Str::DownloadResetBase), false);
        m_esd = &add<ui::CheckField>(strings.get(Str::DownloadEsd), false);
        for (auto* box : {m_updates, m_edge, m_netFx3, m_resetBase, m_esd}) {
            auto previous = box->onChange;
            box->onChange = [this, previous](bool on) {
                if (previous) {
                    previous(on);
                }
                changed();
            };
        }
        m_change = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::DownloadChange));
        m_change->onInvoke = [this] {
            if (onChangeOutput) {
                onChangeOutput();
            }
        };
        m_start = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::DownloadStart), ui::icons::Icon::Download);
        m_start->onInvoke = [this] {
            if (onStart) {
                onStart();
            }
        };
        m_stop = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StatusStop), ui::icons::Icon::Stop);
        m_stop->onInvoke = [this] {
            if (onStop) {
                onStop();
            }
        };
        setBuild(nullptr);
    }

    std::function<void(std::wstring)> onLanguage;
    std::function<void()> onPickApps;
    std::function<void()> onPrefsChanged;

    // The saved choices (settings.json) onto the boxes; no callbacks.
    void setPrefs(const WindowsDownloadPrefs& p) {
        m_updates->setChecked(p.updates);
        m_edge->setChecked(p.edge);
        m_apps->setChecked(p.apps);
        m_netFx3->setChecked(p.netFx3);
        m_resetBase->setChecked(p.resetBase);
        m_esd->setChecked(p.esd);
        m_excluded = p.excludedApps;
        refreshVisibility();
    }
    [[nodiscard]] WindowsDownloadPrefs prefs() const {
        WindowsDownloadPrefs p;
        p.updates = m_updates->checked();
        p.edge = m_edge->checked();
        p.apps = m_apps->checked();
        p.netFx3 = m_netFx3->checked();
        p.resetBase = m_resetBase->checked();
        p.esd = m_esd->checked();
        p.excludedApps = m_excluded;
        return p;
    }
    void setExcluded(std::vector<std::wstring> excluded) {
        m_excluded = std::move(excluded);
        invalidate();
        changed();
    }
    void setAppCount(int total) {
        m_appTotal = total;
        refreshVisibility();
        invalidate();
    }
    [[nodiscard]] int appsIncluded() const {
        int n = 0;
        for (const auto& id : m_appIds) {
            if (std::ranges::none_of(m_excluded, [&](const std::wstring& e) { return text::iequals(e, id); }) ||
                core::uup::appRequired(id)) {
                ++n;
            }
        }
        return n;
    }
    void setAppIds(std::vector<std::wstring> ids) {
        m_appIds = std::move(ids);
        setAppCount(static_cast<int>(m_appIds.size()));
    }
    void setAppList(AppList list) {
        m_appList = list;
        refreshVisibility();
        invalidate();
    }
    [[nodiscard]] AppList appList() const { return m_appList; }
    std::function<void()> onEditionsChanged;
    std::function<void()> onChangeOutput;
    std::function<void()> onStart;
    std::function<void()> onStop;

    void setBuild(const core::uup::Build* build) {
        m_build = build ? std::optional(*build) : std::nullopt;
        m_languages.clear();
        m_lang->setItems({}, 0);
        clearEditions();
        m_size.reset();
        refreshVisibility();
        layout();
        invalidate();
    }
    void setLanguages(std::vector<core::uup::Language> list, const std::wstring& selected) {
        m_languages = std::move(list);
        std::vector<std::wstring> names;
        int index = 0;
        for (std::size_t i = 0; i < m_languages.size(); ++i) {
            names.push_back(m_languages[i].name);
            if (m_languages[i].code == selected) {
                index = static_cast<int>(i);
            }
        }
        m_lang->setItems(std::move(names), index);
        refreshVisibility();
        layout();
        invalidate();
    }
    void setEditions(std::vector<core::uup::Edition> list) {
        clearEditions();
        m_editions = std::move(list);
        for (std::size_t i = 0; i < m_editions.size(); ++i) {
            // Pro (else the first) ticked: what most people install.
            const bool pro = m_editions[i].code == L"PROFESSIONAL";
            auto& box = add<ui::CheckField>(m_editions[i].name, pro);
            box.onChange = [this](bool) {
                m_size.reset();
                invalidate();
                if (onEditionsChanged) {
                    onEditionsChanged();
                }
            };
            m_editionBoxes.push_back(&box);
        }
        if (!m_editionBoxes.empty() && std::ranges::none_of(m_editionBoxes, [](auto* b) { return b->checked(); })) {
            m_editionBoxes.front()->setChecked(true);
        }
        refreshVisibility();
        layout();
        invalidate();
    }
    void setSize(std::uint64_t download) {
        m_size = download;
        invalidate();
    }
    void setAppsSize(std::uint64_t bytes) {
        m_appsSize = bytes;
        invalidate();
    }
    void setOutput(std::filesystem::path path) {
        m_output = std::move(path);
        invalidate();
    }
    [[nodiscard]] const std::filesystem::path& output() const { return m_output; }
    [[nodiscard]] std::wstring language() const {
        const int i = m_lang->selected();
        return i >= 0 && i < static_cast<int>(m_languages.size()) ? m_languages[static_cast<std::size_t>(i)].code : L"";
    }
    [[nodiscard]] std::vector<std::wstring> editions() const {
        std::vector<std::wstring> codes;
        for (std::size_t i = 0; i < m_editionBoxes.size(); ++i) {
            if (m_editionBoxes[i]->checked()) {
                codes.push_back(m_editions[i].code);
            }
        }
        return codes;
    }
    [[nodiscard]] bool updates() const { return m_updates->checked(); }
    [[nodiscard]] bool edge() const { return m_edge->checked(); }
    [[nodiscard]] bool esd() const { return m_esd->checked(); }
    [[nodiscard]] bool apps() const { return m_apps->checked() && appsOffered(); }
    [[nodiscard]] bool netFx3() const { return m_netFx3->checked(); }
    [[nodiscard]] bool resetBase() const { return m_resetBase->checked(); }
    [[nodiscard]] const std::vector<std::wstring>& excluded() const { return m_excluded; }
    [[nodiscard]] const std::optional<core::uup::Build>& build() const { return m_build; }

    void refreshVisibility() {
        const bool has = m_build.has_value();
        const bool running = m_state.windowsDownload().has_value();
        for (ui::Widget* w :
             std::initializer_list<ui::Widget*>{m_lang, m_updates, m_edge, m_apps, m_pick, m_netFx3, m_resetBase, m_esd, m_change}) {
            w->setVisible(has);
            w->setEnabled(!running);
        }
        for (auto* b : m_editionBoxes) {
            b->setVisible(has);
            b->setEnabled(!running);
        }
        m_edge->setEnabled(!running && m_updates->checked());
        // No app set: the box says so (it keeps its saved state for the builds that have one) and
        // "Seç…" goes. A list that failed to load is asked for again by "Seç…".
        m_apps->setEnabled(!running && appsOffered());
        m_pick->setVisible(has && appsOffered());
        m_pick->setEnabled(!running && m_apps->checked() &&
                           ((m_appList == AppList::Ready && m_appTotal > 0) || m_appList == AppList::Failed));
        m_start->setVisible(has && !running);
        m_start->setEnabled(!m_languages.empty() && !editions().empty() && !m_output.empty());
        m_stop->setVisible(running);
    }

    void layout() override {
        const RectF b = bounds();
        const float x = b.x + kPad;
        const float w = b.width - 2 * kPad;
        float y = b.y + kPad + kLine + 4 + kLine + kGap; // title, subtitle
        y += kLine + 4;                                   // "Dil"
        m_lang->setBounds({x, y, w, kControl});
        y += kControl + kGap + kLine + 4;                 // "Sürümler"
        for (auto* box : m_editionBoxes) {
            box->setBounds({x, y, w, kControl});
            y += kControl;
        }
        y += kGap + kLine + 4;                            // "Seçenekler"
        m_updates->setBounds({x, y, w, kControl});
        y += kControl;
        m_hintTop = y;
        y += hintHeight() + 4;
        m_edge->setBounds({x, y, w, kControl});
        y += kControl;
        const ui::SizeF pick = m_pick->measure({});
        m_apps->setBounds({x, y, w - pick.width - 64, kControl});
        m_pick->setBounds({b.right() - kPad - pick.width, y, pick.width, kControl});
        m_appsRow = y;
        y += kControl;
        m_netFx3->setBounds({x, y, w, kControl});
        y += kControl;
        m_resetBase->setBounds({x, y, w, kControl});
        y += kControl;
        m_esd->setBounds({x, y, w, kControl});
        y += kControl + kGap + kLine + 4;                 // "Kayıt yeri"
        const ui::SizeF change = m_change->measure({});
        m_pathTop = y;
        m_change->setBounds({b.right() - kPad - change.width, y, change.width, kControl});
        y += kControl + kGap;
        m_sizeTop = y;
        y += kLine + kGap;
        m_start->setBounds({x, y, w, kControl + 4});
        m_jobTop = y;
        const ui::SizeF stop = m_stop->measure({});
        // Under the progress: below the button's place, or at the top when no build is picked (the
        // page was opened again while a job runs).
        const float stopTop = m_build ? y + 52 : b.y + kPad + kLine + kGap + 52;
        m_stop->setBounds({b.right() - kPad - stop.width, stopTop, stop.width, kControl});
        m_noteTop = y + kControl + 4 + kGap;
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.fillRect(b, Color::BgPanel);
        canvas.line({b.x, b.y}, {b.x, b.bottom()}, Color::LineSubtle);
        const float x = b.x + kPad;
        const float w = b.width - 2 * kPad;
        if (!m_build) {
            // Back on the page while a job runs: its progress, where the button would be.
            if (const auto& job = m_state.windowsDownload()) {
                canvas.drawText(job->title, {x, b.y + kPad, w, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
                paintJob(canvas, *job, {x, b.y + kPad + kLine + kGap, w, 80});
            } else if (const float notice = noticeHeight(w); notice > 0) {
                paintNotice(canvas, *m_state.windowsDownloadNotice(), {x, b.y + kPad, w, notice});
            }
            return; // otherwise the page says what to do
        }
        float y = b.y + kPad;
        canvas.drawText(m_build->title, {x, y, w, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
        y += kLine + 4;
        canvas.drawText(std::format(L"{} · {} · {}", m_build->build, archLabel(m_build->arch),
                                    formatDate(unixToFiletime(m_build->created), m_language)),
                        {x, y, w, kLine}, TypeStyle::Caption, Color::TextSecondary);
        auto heading = [&](const RectF& under, Str label) {
            canvas.drawText(m_strings.get(label), {x, under.y - kLine - 4, w, kLine}, TypeStyle::Section,
                            Color::TextTertiary);
        };
        heading(m_lang->bounds(), Str::DownloadLanguage);
        if (!m_editionBoxes.empty()) {
            heading(m_editionBoxes.front()->bounds(), Str::DownloadEditions);
        }
        heading(m_updates->bounds(), Str::DownloadOptions);
        canvas.drawTextWrapped(m_strings.get(Str::DownloadUpdatesHint), {x + 24, m_hintTop, w - 24, hintHeight()},
                               TypeStyle::Caption, Color::TextTertiary);
        canvas.drawText(m_strings.get(Str::DownloadOutput), {x, m_pathTop - kLine - 4, w, kLine}, TypeStyle::Section,
                        Color::TextTertiary);
        // How many Store apps go in, left of "Seç…" ("…" while the list loads); without an app set,
        // why in the button's place.
        if (!appsOffered()) {
            const float left = m_apps->bounds().right();
            canvas.drawText(m_strings.get(m_appList == AppList::InImage ? Str::DownloadAppsInImage : Str::DownloadAppsNone),
                            {left, m_appsRow, b.right() - kPad - left, kControl}, TypeStyle::Caption, Color::TextTertiary,
                            ui::TextAlign::Trailing);
        } else if (m_apps->checked()) {
            const RectF count{m_pick->bounds().x - 64, m_appsRow, 56, kControl};
            if (m_appList == AppList::Ready && m_appTotal > 0) {
                canvas.drawText(std::format(L"{} / {}", appsIncluded(), m_appTotal), count, TypeStyle::Mono,
                                Color::TextSecondary, ui::TextAlign::Trailing);
            } else if (m_appList == AppList::Failed) {
                canvas.drawText(m_strings.get(Str::DownloadAppsFailed), count, TypeStyle::Caption, Color::TextTertiary,
                                ui::TextAlign::Trailing);
            } else {
                canvas.drawText(L"…", count, TypeStyle::Mono, Color::TextTertiary, ui::TextAlign::Trailing);
            }
        }
        canvas.drawText(m_output.filename().wstring(), {x, m_pathTop, m_change->bounds().x - x - 8, kLine}, TypeStyle::Body,
                        Color::TextPrimary);
        canvas.drawText(m_output.parent_path().wstring(), {x, m_pathTop + kLine, m_change->bounds().x - x - 8, kLine},
                        TypeStyle::Caption, Color::TextTertiary);
        // The size of what goes over the network, and the room the conversion needs on top of it.
        std::wstring size = m_strings.get(Str::DownloadSizeLoading);
        if (editions().empty()) {
            size = m_strings.get(Str::DownloadNoEditions);
        } else if (m_size) {
            size = m_strings.format(Str::DownloadSize, {{L"size", formatBytes(downloadBytes(), m_language)},
                                                        {L"disk", formatBytes(diskBytes(), m_language)}});
        }
        canvas.drawText(size, {x, m_sizeTop, w, kLine}, TypeStyle::Caption, Color::TextSecondary);
        if (const auto& job = m_state.windowsDownload()) {
            paintJob(canvas, *job, {x, m_jobTop, w, 80});
        }
        float noteTop = m_state.windowsDownload() ? m_jobTop + 92 : m_noteTop;
        if (const float notice = noticeHeight(w); notice > 0) {
            paintNotice(canvas, *m_state.windowsDownloadNotice(), {x, m_noteTop, w, notice});
            noteTop += notice + kGap;
        }
        const float noteHeight = host() ? std::ceil(host()->text().measureWrapped(m_strings.get(Str::DownloadNote),
                                                                                   TypeStyle::Caption, w))
                                        : 32.0f;
        canvas.drawTextWrapped(m_strings.get(Str::DownloadNote), {x, noteTop, w, noteHeight}, TypeStyle::Caption,
                               Color::TextTertiary);
    }

    void setNow(double now) { m_now = now; }

    // What goes over the network, and the room the job needs on the work drive on top of it (the
    // "gereken boş alan" line; 0 while the size is not known).
    [[nodiscard]] std::uint64_t downloadBytes() const { return m_size ? *m_size + (apps() ? m_appsSize : 0) : 0; }
    [[nodiscard]] std::uint64_t diskBytes() const {
        return m_size && !editions().empty() ? downloadBytes() + (updates() ? 16ull : 9ull) * 1'000'000'000ull : 0;
    }

private:
    // How the last start ended (AppState) while no job runs: under the button, the note moves down.
    using Notice = AppState::WindowsDownloadNotice;
    [[nodiscard]] static bool hinted(const Notice& notice) { return notice.kind == Notice::Kind::Failed && notice.job; }
    [[nodiscard]] std::wstring noticeHint() const {
        return m_strings.get(Str::DownloadStoppedBody) + L" " + m_strings.get(Str::DownloadFailedLog);
    }
    [[nodiscard]] float noticeHeight(float w) const {
        const auto& notice = m_state.windowsDownloadNotice();
        if (!notice || m_state.windowsDownload() || !host()) {
            return 0;
        }
        float h = kLine + 4 + std::ceil(host()->text().measureWrapped(notice->text, TypeStyle::Caption, w - 24));
        if (hinted(*notice)) {
            h += 4 + std::ceil(host()->text().measureWrapped(noticeHint(), TypeStyle::Caption, w - 24));
        }
        return h;
    }
    void paintNotice(ui::Canvas& canvas, const Notice& notice, RectF r) {
        const bool failed = notice.kind == Notice::Kind::Failed;
        canvas.drawIcon(failed ? ui::icons::Icon::ErrorOctagon : ui::icons::Icon::WarningTriangle, {r.x, r.y},
                        failed ? Color::StatusError : Color::StatusWarning);
        canvas.drawText(notice.title, {r.x + 24, r.y, r.width - 24, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
        const float textTop = r.y + kLine + 4;
        const float textHeight = std::ceil(host()->text().measureWrapped(notice.text, TypeStyle::Caption, r.width - 24));
        canvas.drawTextWrapped(notice.text, {r.x + 24, textTop, r.width - 24, textHeight}, TypeStyle::Caption,
                               Color::TextSecondary);
        if (hinted(notice)) {
            canvas.drawTextWrapped(noticeHint(), {r.x + 24, textTop + textHeight + 4, r.width - 24, r.bottom() - textTop - textHeight - 4},
                                   TypeStyle::Caption, Color::TextTertiary);
        }
    }
    [[nodiscard]] bool appsOffered() const { return m_appList != AppList::InImage && m_appList != AppList::NotOffered; }
    void changed() {
        if (onPrefsChanged) {
            onPrefsChanged();
        }
    }
    void clearEditions() {
        for (auto* box : m_editionBoxes) {
            removeChild(box);
        }
        m_editionBoxes.clear();
        m_editions.clear();
    }
    [[nodiscard]] float hintHeight() const {
        const float w = bounds().width - 2 * kPad - 24;
        return host() && w > 0 ? std::ceil(host()->text().measureWrapped(m_strings.get(Str::DownloadUpdatesHint),
                                                                         TypeStyle::Caption, w))
                                : 32.0f;
    }
    [[nodiscard]] std::wstring stepName(const std::wstring& step) const {
        if (step == L"references") return m_strings.get(Str::DownloadStepReferences);
        if (step == L"media") return m_strings.get(Str::DownloadStepMedia);
        if (step == L"export") return m_strings.get(Str::DownloadStepExport);
        if (step == L"updates") return m_strings.get(Str::DownloadStepUpdates);
        if (step == L"boot") return m_strings.get(Str::DownloadStepBoot);
        if (step == L"optimize") return m_strings.get(Str::DownloadStepOptimize);
        if (step == L"iso") return m_strings.get(Str::DownloadStepIso);
        return m_strings.get(Str::DownloadStepService);
    }
    void paintJob(ui::Canvas& canvas, const AppState::WindowsDownload& job, RectF r) {
        using Stage = AppState::WindowsDownload::Stage;
        std::wstring title;
        if (job.stage == Stage::Preparing) {
            title = m_strings.format(Str::DownloadPreparing, {{L"title", job.title}});
        } else if (job.stage == Stage::Downloading) {
            title = m_strings.format(Str::DownloadDownloading, {{L"done", formatBytes(job.doneBytes, m_language)},
                                                                {L"total", formatBytes(job.totalBytes, m_language)}});
        } else {
            title = m_strings.format(Str::DownloadConverting, {{L"step", stepName(job.step)}});
        }
        const float angle = static_cast<float>(static_cast<int>(m_now / 100.0) % 8) * 45.0f;
        canvas.drawIcon(ui::icons::Icon::Spinner, {r.x, r.y + 4}, Color::AccentBase, ui::IconVariant::Regular16, 0,
                        ui::reducedMotion() ? 0.0f : angle);
        canvas.drawText(title, {r.x + 24, r.y + 4, r.width - 24, kLine}, TypeStyle::BodyStrong, Color::TextPrimary);
        const float fraction = static_cast<float>(std::clamp(job.fraction, 0.0, 1.0));
        canvas.progressBar({r.x, r.y + 30, r.width - 44, 4}, fraction);
        canvas.drawText(std::to_wstring(static_cast<int>(fraction * 100)) + L"%", {r.right() - 40, r.y + 24, 40, kLine},
                        TypeStyle::Mono, Color::TextSecondary, ui::TextAlign::Trailing);
        // Time left once a few percent tell the pace.
        const double elapsed = (m_now - job.startedMs) / 1000.0;
        if (job.stage != Stage::Preparing && fraction > 0.03f && elapsed > 5) {
            const double left = elapsed * (1.0 - fraction) / fraction;
            canvas.drawText(m_strings.format(Str::DownloadEta, {{L"time", formatDuration(left, m_language, true)}}),
                            {r.x, r.y + 56, r.width - m_stop->bounds().width - 8, kLine}, TypeStyle::Caption,
                            Color::TextSecondary);
        }
    }

    const Localization& m_strings;
    Language m_language;
    const AppState& m_state;
    std::optional<core::uup::Build> m_build;
    std::vector<core::uup::Language> m_languages;
    std::vector<core::uup::Edition> m_editions;
    std::vector<ui::CheckField*> m_editionBoxes;
    std::optional<std::uint64_t> m_size;
    std::uint64_t m_appsSize = 0; // UUP dump's app set (what the apps take at most)
    std::filesystem::path m_output;
    ui::Dropdown* m_lang = nullptr;
    ui::CheckField* m_updates = nullptr;
    ui::CheckField* m_edge = nullptr;
    ui::CheckField* m_apps = nullptr;
    ui::Button* m_pick = nullptr;
    ui::CheckField* m_netFx3 = nullptr;
    ui::CheckField* m_resetBase = nullptr;
    ui::CheckField* m_esd = nullptr;
    std::vector<std::wstring> m_excluded; // app ids left out
    std::vector<std::wstring> m_appIds;   // the selection's apps
    int m_appTotal = 0;
    AppList m_appList = AppList::Loading;
    float m_appsRow = 0;
    ui::Button* m_change = nullptr;
    ui::Button* m_start = nullptr;
    ui::Button* m_stop = nullptr;
    float m_hintTop = 0, m_pathTop = 0, m_sizeTop = 0, m_jobTop = 0, m_noteTop = 0;
    double m_now = 0;
};

// ---- the page --------------------------------------------------------------------------------

DownloadPage::DownloadPage(AppState& state, const Localization& strings, Language language,
                           WindowsDownloadController& controller, Intents intents)
    : m_state(state), m_strings(strings), m_language(language), m_controller(controller), m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::DownloadSearch));
    m_search->onChange = [this](const std::wstring&) { refreshList(); };
    m_product = &add<ui::Dropdown>(L"", std::vector<std::wstring>{L"Windows 11", L"Windows 10", strings.get(Str::DownloadFilterAll)}, 0);
    m_product->onChange = [this](int) { loadBuilds(); };
    m_kind = &add<ui::Dropdown>(L"", std::vector<std::wstring>{strings.get(Str::DownloadFilterRelease),
                                                               strings.get(Str::DownloadFilterInsider),
                                                               strings.get(Str::DownloadFilterServer),
                                                               strings.get(Str::DownloadFilterAll)},
                                0);
    m_kind->onChange = [this](int) { refreshList(); };
    m_arch = &add<ui::Dropdown>(L"", std::vector<std::wstring>{L"x64", L"ARM64", L"x86", strings.get(Str::DownloadFilterAll)}, 0);
    m_arch->onChange = [this](int) { refreshList(); };
    m_refresh = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::DownloadRefresh), ui::icons::Icon::Refresh);
    m_refresh->onInvoke = [this] { loadBuilds(); };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::DownloadColVersion), 0, ui::TextAlign::Leading, true},
        {strings.get(Str::DownloadColBuild), 110, ui::TextAlign::Leading, true},
        {strings.get(Str::DownloadColArch), 72, ui::TextAlign::Leading, true},
        {strings.get(Str::DownloadColDate), 110, ui::TextAlign::Leading, true},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->sortKey = [this](int row, int column) -> ui::TableSortKey {
        if (row < 0 || row >= static_cast<int>(m_shown.size())) {
            return {};
        }
        const auto& b = m_shown[static_cast<std::size_t>(row)];
        switch (column) {
        case kVersion: return {b.title};
        case kBuildCol: return {b.build};
        case kArchCol: return {archLabel(b.arch)};
        case kDateCol: return {std::wstring(), static_cast<double>(b.created)};
        default: return {};
        }
    };
    m_table->onSelect = [this](int row) { select(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::WindowsDownload, L"", L"");
    m_panel = &add<Panel>(strings, language, state);
    m_panel->onLanguage = [this](std::wstring code) {
        if (const auto* b = selectedBuild()) {
            m_controller.listEditions(b->id, code);
        }
    };
    m_panel->onEditionsChanged = [this] { requestFiles(); };
    m_panel->onPickApps = [this] { openAppPicker(); };
    m_panel->onPrefsChanged = [this] { savePrefs(); };
    m_panel->setPrefs(state.settings().windowsDownload);
    m_panel->onChangeOutput = [this] {
        if (m_intents.pickOutput) {
            if (auto path = m_intents.pickOutput(m_panel->output())) {
                m_panel->setOutput(*path);
                m_panel->refreshVisibility();
            }
        }
    };
    m_panel->onStart = [this] {
        const auto& build = m_panel->build();
        if (!build) {
            return;
        }
        WindowsDownloadController::Request request;
        request.build = *build;
        request.language = m_panel->language();
        request.editions = m_panel->editions();
        request.updates = m_panel->updates();
        request.edge = m_panel->edge();
        request.esd = m_panel->esd();
        request.apps = m_panel->apps();
        request.excludedApps = m_panel->excluded();
        request.netFx3 = m_panel->netFx3();
        request.resetBase = m_panel->resetBase();
        request.output = m_panel->output();
        // The conversion runs DISM: unelevated, an hour of download would end in "needs an elevated process".
        if (!core::isElevated()) {
            if (m_intents.adminRequired) {
                m_intents.adminRequired();
            }
            return;
        }
        // Room first: a work drive that fills up an hour into the conversion ends with no ISO.
        // What an earlier run downloaded is already there.
        if (const std::uint64_t need = m_panel->diskBytes(); need > 0) {
            const std::filesystem::path root = m_state.settings().workRoot;
            std::error_code ec;
            const auto space = std::filesystem::space(std::filesystem::exists(root, ec) ? root : root.root_path(), ec);
            const std::uint64_t have = std::min(core::treeBytes(m_controller.setFolder(request)), need);
            if (!ec && space.available < need - have) {
                log::warn("uup", std::format(L"not started: {} free on {}, {} needed", space.available, root.wstring(),
                                             need - have));
                m_state.setWindowsDownloadNotice(AppState::WindowsDownloadNotice{
                    AppState::WindowsDownloadNotice::Kind::Failed,
                    m_strings.get(Str::DownloadNoSpaceTitle),
                    m_strings.format(Str::DownloadNoSpaceBody, {{L"drive", root.root_name().wstring()},
                                                                {L"free", formatBytes(space.available, m_language)},
                                                                {L"need", formatBytes(need - have, m_language)}}),
                    false});
                return;
            }
        }
        m_controller.start(std::move(request));
    };
    m_panel->onStop = [this] { m_controller.cancel(); };
    setAccessible(ui::AccessRole::Group, strings.get(Str::DownloadTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::WindowsDownload) {
            m_panel->refreshVisibility();
            m_panel->invalidate();
            if (m_state.windowsDownload()) {
                animate();
            }
        }
    });
    loadBuilds();
    if (m_state.windowsDownload()) {
        animate();
    }
}

DownloadPage::~DownloadPage() {
    m_state.unsubscribe(m_subscription);
}

bool DownloadPage::tick(double now) {
    m_panel->setNow(now);
    m_panel->invalidate();
    return m_state.windowsDownload().has_value();
}

void DownloadPage::setBuilds(std::vector<core::uup::Build> builds) {
    m_all = std::move(builds);
    m_loading = false;
    m_listError.reset();
    refreshList();
}

void DownloadPage::loadBuilds() {
    m_loading = true;
    m_listError.reset();
    m_all.clear();
    refreshList();
    // Windows 10's full sets are "Feature update to Windows 10, version 22H2": its build finds them.
    const int product = m_product->selected();
    m_controller.listBuilds(product == 0 ? L"Windows 11" : product == 1 ? L"19045" : L"");
}

void DownloadPage::savePrefs() {
    AppSettings settings = m_state.settings();
    settings.windowsDownload = m_panel->prefs();
    if (!(settings == m_state.settings())) {
        m_state.setSettings(std::move(settings));
    }
}

void DownloadPage::setApps(const std::wstring& id, const std::wstring& language, const std::vector<std::wstring>& editions,
                           std::vector<core::uup::AppFeature> apps) {
    const auto* b = selectedBuild();
    if (!b || b->id != id || m_panel->language() != language || m_panel->editions() != editions) {
        return;
    }
    m_apps = std::move(apps);
    m_appsArrived = true;
    std::vector<std::wstring> ids;
    for (const auto& a : m_apps) {
        ids.push_back(a.id);
    }
    m_panel->setAppIds(std::move(ids));
    updateAppList();
}

void DownloadPage::requestApps() {
    const auto* b = selectedBuild();
    const auto editions = m_panel->editions();
    m_apps.clear();
    m_appsArrived = false;
    m_panel->setAppIds({});
    updateAppList();
    if (b && !editions.empty() && !m_panel->language().empty()) {
        m_controller.listApps(*b, m_panel->language(), editions); // the picker's list (a 3 MB catalogue)
    }
}

void DownloadPage::updateAppList() {
    using AppList = Panel::AppList;
    const auto* b = selectedBuild();
    AppList list = AppList::Loading;
    if (m_appSet == false) {
        list = b && b->title.find(L"Windows 10") != std::wstring::npos ? AppList::InImage : AppList::NotOffered;
    } else if (m_appsArrived && !m_apps.empty()) {
        list = AppList::Ready;
    } else if (m_appsArrived && m_appSet == true) {
        list = AppList::Failed; // the build has apps but their list did not come
    }
    m_panel->setAppList(list);
}

void DownloadPage::openAppPicker() {
    if (m_apps.empty() && m_panel->appList() == Panel::AppList::Failed) {
        requestApps(); // "Seç…" after a failed list: ask again
        return;
    }
    if (m_apps.empty() || !host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::DownloadAppsTitle), m_strings.get(Str::DownloadAppsBody),
                                               ui::icons::Icon::AppxPackage, Color::TextSecondary, 560.0f);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(440.0f, 300.0f);
    struct Row {
        std::wstring id;
        ui::Toggle* toggle;
    };
    auto rows = std::make_shared<std::vector<Row>>();
    const auto excluded = m_panel->excluded();
    static constexpr Str kGroups[] = {Str::DownloadAppsEssential, Str::DownloadAppsMedia, Str::DownloadAppsCodecs,
                                      Str::DownloadAppsOther};
    for (int g = 0; g < 4; ++g) {
        std::vector<const core::uup::AppFeature*> inGroup;
        for (const auto& a : m_apps) {
            if (static_cast<int>(core::uup::appGroup(a.id)) == g) {
                inGroup.push_back(&a);
            }
        }
        if (inGroup.empty()) {
            continue;
        }
        std::ranges::sort(inGroup, [&](const auto* l, const auto* r) {
            return text::fold(appName(m_strings, l->id)) < text::fold(appName(m_strings, r->id));
        });
        form.addSection(m_strings.get(kGroups[g]));
        for (const auto* a : inGroup) {
            const bool required = core::uup::appRequired(a->id);
            const bool on = required || std::ranges::none_of(excluded, [&](const std::wstring& e) { return text::iequals(e, a->id); });
            auto& toggle = form.addRow<ui::Toggle>(appName(m_strings, a->id),
                                                   required ? m_strings.get(Str::DownloadAppsRequired) : std::wstring(),
                                                   ui::tokens::size::toggleW, std::wstring(), on);
            toggle.setEnabled(!required);
            rows->push_back({a->id, &toggle});
        }
    }
    auto setAll = [rows](auto pick) {
        for (auto& r : *rows) {
            if (r.toggle->enabled()) {
                r.toggle->setOn(pick(r.id));
            }
        }
    };
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::DownloadAppsRecommended),
                   [setAll] { setAll([](const std::wstring&) { return true; }); });
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::DownloadAppsEssentialOnly), [setAll] {
        setAll([](const std::wstring& id) { return core::uup::appGroup(id) == core::uup::AppGroup::Essential; });
    });
    auto close = [this, raw] { host()->popModal(raw); };
    raw->onCancel = close;
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonOk),
                   [this, raw, rows] {
                       std::vector<std::wstring> out;
                       for (const auto& r : *rows) {
                           if (!r.toggle->isOn()) {
                               out.push_back(r.id);
                           }
                       }
                       m_panel->setExcluded(std::move(out));
                       host()->popModal(raw);
                   },
                   /*primary=*/true);
    host()->pushModal(std::move(dialog));
}

void DownloadPage::setListFailed(const Error& error) {
    if (m_loading || m_all.empty()) {
        m_loading = false;
        m_listError = error;
        refreshList();
    }
}

void DownloadPage::setLanguages(const std::wstring& id, std::vector<core::uup::Language> languages) {
    const auto* b = selectedBuild();
    if (!b || b->id != id) {
        return;
    }
    const std::wstring pick = preferredLanguage(languages);
    for (auto& l : languages) {
        l.name = localLanguageName(l);
    }
    std::ranges::sort(languages, [](const core::uup::Language& a, const core::uup::Language& b) {
        return text::fold(a.name) < text::fold(b.name);
    });
    m_panel->setLanguages(std::move(languages), pick);
    if (!pick.empty()) {
        m_controller.listEditions(id, pick);
    }
    m_panel->setOutput(downloadsFolder() / WindowsDownloadController::isoName(*b, pick));
    m_controller.listFiles(id, L"neutral", {L"app"}); // the apps' size
}

void DownloadPage::setEditions(const std::wstring& id, const std::wstring& language, std::vector<core::uup::Edition> editions) {
    const auto* b = selectedBuild();
    if (!b || b->id != id || m_panel->language() != language) {
        return;
    }
    m_panel->setEditions(std::move(editions));
    m_panel->setOutput(m_panel->output().parent_path() / WindowsDownloadController::isoName(*b, language));
    requestFiles();
}

void DownloadPage::setFiles(const std::wstring& id, const std::wstring& language, const std::vector<std::wstring>& editions,
                            const core::uup::FileSet& files) {
    const auto* b = selectedBuild();
    if (b && b->id == id && language == L"neutral") {
        m_panel->setAppsSize(files.totalSize()); // the Store apps' set
        return;
    }
    if (!b || b->id != id || m_panel->language() != language || m_panel->editions() != editions) {
        return;
    }
    std::uint64_t bytes = 0;
    for (const auto& f : WindowsDownloadController::filesFor(files, m_panel->updates())) {
        bytes += f.size;
    }
    m_panel->setSize(bytes);
    m_appSet = files.appxPresent;
    updateAppList();
}

void DownloadPage::requestFiles() {
    const auto* b = selectedBuild();
    const auto editions = m_panel->editions();
    m_appSet.reset();
    if (b && !editions.empty() && !m_panel->language().empty()) {
        m_controller.listFiles(b->id, m_panel->language(), editions);
    }
    requestApps();
}

const core::uup::Build* DownloadPage::selectedBuild() const {
    const int row = m_table->selected();
    return row >= 0 && row < static_cast<int>(m_shown.size()) ? &m_shown[static_cast<std::size_t>(row)] : nullptr;
}

void DownloadPage::select(int row) {
    (void)row;
    const auto* b = selectedBuild();
    m_panel->setBuild(b);
    m_appSet.reset(); // the new build's files tell
    m_apps.clear();
    m_appsArrived = false;
    m_panel->setAppIds({});
    updateAppList();
    if (b) {
        m_controller.listLanguages(b->id);
    }
    layout();
}

void DownloadPage::refreshList() {
    const std::wstring query = text::fold(m_search->text());
    const int kind = m_kind->selected();
    const int arch = m_arch->selected();
    const int product = m_product->selected();
    const std::wstring keepId = selectedBuild() ? selectedBuild()->id : std::wstring();
    m_shown.clear();
    for (const auto& b : m_all) {
        if (kind == 0 && b.kind != BuildKind::Release) continue;
        if (kind == 1 && b.kind != BuildKind::Insider) continue;
        if (kind == 2 && b.kind != BuildKind::Server) continue;
        if (kind == 3 && b.kind == BuildKind::Update) continue;
        if (arch == 0 && b.arch != L"amd64") continue;
        if (arch == 1 && b.arch != L"arm64") continue;
        if (arch == 2 && b.arch != L"x86") continue;
        if (product == 0 && b.title.find(L"Windows 11") == std::wstring::npos) continue;
        if (product == 1 && b.title.find(L"Windows 10") == std::wstring::npos) continue;
        if (!query.empty() && text::fold(b.title + L" " + b.build).find(query) == std::wstring::npos) continue;
        m_shown.push_back(b);
    }
    m_table->setRowCount(static_cast<int>(m_shown.size()));
    int keep = -1;
    for (std::size_t i = 0; i < m_shown.size(); ++i) {
        if (m_shown[i].id == keepId) {
            keep = static_cast<int>(i);
        }
    }
    if (keep >= 0) {
        m_table->setSelected(keep, false);
    } else {
        m_table->clearSelection();
        m_panel->setBuild(nullptr);
    }
    const bool empty = m_shown.empty();
    m_table->setVisible(!empty);
    if (m_loading) {
        m_empty->setContent(ui::icons::Icon::WindowsDownload, m_strings.get(Str::DownloadLoading), L"");
        m_empty->clearAction();
    } else if (m_listError) {
        m_empty->setContent(ui::icons::Icon::WindowsDownload, m_strings.get(Str::DownloadListFailed),
                            m_strings.get(Str::DownloadListFailedBody));
        m_empty->setAction(m_strings.get(Str::DownloadRefresh)).onInvoke = [this] { m_refresh->onInvoke(); };
    } else {
        m_empty->setContent(ui::icons::Icon::WindowsDownload, m_strings.get(Str::DownloadNoBuilds), L"");
        m_empty->clearAction();
    }
    m_empty->setVisible(empty);
    layout();
    invalidate();
}

void DownloadPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_shown.size())) {
        return;
    }
    const auto& b = m_shown[static_cast<std::size_t>(row)];
    const RectF r{rect.x + 8, rect.y, rect.width - 16, rect.height};
    switch (column) {
    case kVersion: canvas.drawText(b.title, r, TypeStyle::Body, Color::TextPrimary); break;
    case kBuildCol: canvas.drawText(b.build, r, TypeStyle::Mono, Color::TextSecondary); break;
    case kArchCol: canvas.drawText(archLabel(b.arch), r, TypeStyle::Body, Color::TextSecondary); break;
    case kDateCol:
        canvas.drawText(formatDate(unixToFiletime(b.created), m_language), r, TypeStyle::Body, Color::TextSecondary);
        break;
    default: break;
    }
}

void DownloadPage::layout() {
    const RectF b = bounds();
    const float listWidth = std::max(b.width - kPanel, 200.0f);
    float x = b.x;
    const float y = b.y + kTop;
    const ui::SizeF refresh = m_refresh->measure({});
    m_refresh->setBounds({b.x + listWidth - kGap - refresh.width, y, refresh.width, kControl});
    m_search->setBounds({x, y, 240, kControl});
    x += 240 + 8;
    m_product->setBounds({x, y, 120, kControl});
    x += 120 + 8;
    m_kind->setBounds({x, y, 110, kControl});
    x += 110 + 8;
    m_arch->setBounds({x, y, 96, kControl});
    const RectF list{b.x, y + kControl + kGap, listWidth - kGap, b.bottom() - (y + kControl + kGap)};
    m_table->setBounds(list);
    m_empty->setBounds(list);
    m_panel->setBounds({b.x + listWidth, b.y, kPanel, b.height});
}

void DownloadPage::paint(ui::Canvas& canvas) {
    if (!m_panel->build() && !m_state.windowsDownload()) {
        // The panel's place says what to do first.
        const RectF p = m_panel->bounds();
        canvas.drawText(m_strings.get(Str::DownloadPickTitle), {p.x + kPad, p.y + kPad, p.width - 2 * kPad, kLine},
                        TypeStyle::BodyStrong, Color::TextPrimary);
        canvas.drawTextWrapped(m_strings.get(Str::DownloadPickBody), {p.x + kPad, p.y + kPad + kLine + 4, p.width - 2 * kPad, 40},
                               TypeStyle::Caption, Color::TextSecondary);
    }
}

void DownloadPage::demo(int what) {
    std::vector<core::uup::Build> builds;
    auto add = [&](std::wstring id, std::wstring title, std::wstring build, std::wstring arch, std::int64_t created) {
        core::uup::Build b;
        b.id = std::move(id);
        b.title = std::move(title);
        b.build = std::move(build);
        b.arch = std::move(arch);
        b.created = created;
        b.kind = core::uup::buildKind(b.title);
        builds.push_back(std::move(b));
    };
    add(L"a1", L"Windows 11, version 26H2 (26300.9550)", L"26300.9550", L"amd64", 1790096444);
    add(L"a2", L"Windows 11, version 26H2 (26300.9539)", L"26300.9539", L"amd64", 1789059942);
    add(L"a3", L"Windows 11, version 26H2 (26300.9457)", L"26300.9457", L"amd64", 1789407664);
    add(L"a4", L"Windows 11, version 25H2 (26200.9470)", L"26200.9470", L"amd64", 1788888000);
    add(L"a5", L"Windows 11, version 24H2 (26100.9470)", L"26100.9470", L"amd64", 1788800000);
    add(L"a6", L"Windows 11 Insider Preview 27965.1000 (rs_prerelease)", L"27965.1000", L"amd64", 1790500000);
    add(L"a7", L"Windows 10, version 22H2 (19045.6456)", L"19045.6456", L"amd64", 1788700000);
    add(L"a8", L"Windows 11, version 26H2 (26300.9550)", L"26300.9550", L"arm64", 1790096445);
    if (what == 5) {
        m_product->setSelected(1); // Windows 10
    }
    setBuilds(std::move(builds));
    if (what == 5) {
        // Windows 10: its Store apps are in the image, there is no set to pick from.
        m_table->setSelected(0, false);
        m_panel->setBuild(selectedBuild());
        m_panel->setLanguages({{L"tr-tr", localLanguageName({L"tr-tr", L"Turkish"})}}, L"tr-tr");
        m_panel->setEditions({{L"PROFESSIONAL", L"Windows Pro"}, {L"CORE", L"Windows Home"}});
        m_panel->setOutput(std::filesystem::path(L"C:\\Users\\shades\\Downloads") /
                           WindowsDownloadController::isoName(*selectedBuild(), L"tr-tr"));
        m_panel->setSize(4'123'456'789ull);
        m_appSet = false;
        m_appsArrived = true;
        updateAppList();
        layout();
        return;
    }
    if (what >= 1) {
        m_table->setSelected(0, false);
        m_panel->setBuild(selectedBuild());
        m_panel->setLanguages({{L"en-us", localLanguageName({L"en-us", L"English (United States)"})},
                               {L"tr-tr", localLanguageName({L"tr-tr", L"Turkish"})}},
                              L"tr-tr");
        m_panel->setEditions({{L"PROFESSIONAL", L"Windows Pro"}, {L"CORE", L"Windows Home"}});
        m_panel->setOutput(std::filesystem::path(L"C:\\Users\\shades\\Downloads") /
                           WindowsDownloadController::isoName(*selectedBuild(), L"tr-tr"));
        m_panel->setSize(8'656'597'514ull);
        m_panel->setAppsSize(1'245'574'462ull);
        for (const wchar_t* id :
             {L"Microsoft.WindowsStore_8wekyb3d8bbwe", L"Microsoft.SecHealthUI_8wekyb3d8bbwe",
              L"Microsoft.DesktopAppInstaller_8wekyb3d8bbwe", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe",
              L"Microsoft.WindowsNotepad_8wekyb3d8bbwe", L"Microsoft.Windows.Photos_8wekyb3d8bbwe",
              L"Microsoft.WindowsTerminal_8wekyb3d8bbwe", L"Microsoft.Paint_8wekyb3d8bbwe",
              L"Microsoft.ZuneMusic_8wekyb3d8bbwe", L"Microsoft.GamingApp_8wekyb3d8bbwe",
              L"Microsoft.YourPhone_8wekyb3d8bbwe", L"Clipchamp.Clipchamp_yxz26nhyzhsrt",
              L"Microsoft.HEVCVideoExtension_8wekyb3d8bbwe", L"Microsoft.WebpImageExtension_8wekyb3d8bbwe",
              L"Microsoft.BingNews_8wekyb3d8bbwe", L"Microsoft.BingWeather_8wekyb3d8bbwe",
              L"Microsoft.OutlookForWindows_8wekyb3d8bbwe", L"MSTeams_8wekyb3d8bbwe"}) {
            core::uup::AppFeature f;
            f.id = id;
            m_apps.push_back(std::move(f));
        }
        std::vector<std::wstring> ids;
        for (const auto& a : m_apps) {
            ids.push_back(a.id);
        }
        m_panel->setAppIds(std::move(ids));
        m_panel->setExcluded({L"Microsoft.BingNews_8wekyb3d8bbwe", L"MSTeams_8wekyb3d8bbwe"});
        m_appSet = true;
        m_appsArrived = true;
        updateAppList();
    }
    if (what == 4) {
        layout();
        openAppPicker();
        return;
    }
    if (what == 6 || what == 7) { // 6: the conversion failed an hour in; 7: the ISO came without the ESU update
        using Notice = AppState::WindowsDownloadNotice;
        m_state.setWindowsDownloadNotice(
            what == 6 ? Notice{Notice::Kind::Failed, m_strings.get(Str::DownloadConvertFailedTitle),
                               L"DISM error (add package C:\\Users\\shades\\AppData\\Local\\WinLove\\uup\\"
                               L"19045.7727_amd64_tr-tr_professional\\Windows10.0-KB5129236-x64.cab)",
                               true}
                      : Notice{Notice::Kind::Warning, m_strings.get(Str::DownloadDoneWarningsTitle),
                               m_strings.format(Str::DownloadEsuSkipped, {{L"kb", L"KB5129236"}}) + L" " +
                                   m_strings.format(Str::DownloadWarnings, {{L"n", L"1"}})});
        layout();
        return;
    }
    if (what >= 2) {
        AppState::WindowsDownload job;
        job.stage = what == 2 ? AppState::WindowsDownload::Stage::Downloading : AppState::WindowsDownload::Stage::Converting;
        job.title = L"Windows 11, version 26H2 (26300.9550)";
        job.totalBytes = 8'656'597'514ull;
        job.fraction = what == 2 ? 0.42 : 0.63;
        job.doneBytes = static_cast<std::uint64_t>(job.fraction * static_cast<double>(job.totalBytes));
        job.step = L"updates";
        job.startedMs = ui::nowMs() - 240'000;
        m_state.setWindowsDownload(std::move(job));
        m_panel->setNow(ui::nowMs());
    }
    layout();
}

} // namespace wl::app
