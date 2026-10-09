#include "app/pages/DownloadPage.h"

#include "app/Format.h"
#include "base/Text.h"
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
        m_esd = &add<ui::CheckField>(strings.get(Str::DownloadEsd), false);
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
    [[nodiscard]] const std::optional<core::uup::Build>& build() const { return m_build; }

    void refreshVisibility() {
        const bool has = m_build.has_value();
        const bool running = m_state.windowsDownload().has_value();
        for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_lang, m_updates, m_edge, m_esd, m_change}) {
            w->setVisible(has);
            w->setEnabled(!running);
        }
        for (auto* b : m_editionBoxes) {
            b->setVisible(has);
            b->setEnabled(!running);
        }
        m_edge->setEnabled(!running && m_updates->checked());
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
        canvas.drawText(m_output.filename().wstring(), {x, m_pathTop, m_change->bounds().x - x - 8, kLine}, TypeStyle::Body,
                        Color::TextPrimary);
        canvas.drawText(m_output.parent_path().wstring(), {x, m_pathTop + kLine, m_change->bounds().x - x - 8, kLine},
                        TypeStyle::Caption, Color::TextTertiary);
        // The size of what goes over the network, and the room the conversion needs on top of it.
        std::wstring size = m_strings.get(Str::DownloadSizeLoading);
        if (editions().empty()) {
            size = m_strings.get(Str::DownloadNoEditions);
        } else if (m_size) {
            const std::uint64_t disk = *m_size + (updates() ? 16ull : 9ull) * 1'000'000'000ull;
            size = m_strings.format(Str::DownloadSize, {{L"size", formatBytes(*m_size, m_language)},
                                                        {L"disk", formatBytes(disk, m_language)}});
        }
        canvas.drawText(size, {x, m_sizeTop, w, kLine}, TypeStyle::Caption, Color::TextSecondary);
        if (const auto& job = m_state.windowsDownload()) {
            paintJob(canvas, *job, {x, m_jobTop, w, 80});
        }
        const float noteTop = m_state.windowsDownload() ? m_jobTop + 92 : m_noteTop;
        const float noteHeight = host() ? std::ceil(host()->text().measureWrapped(m_strings.get(Str::DownloadNote),
                                                                                   TypeStyle::Caption, w))
                                        : 32.0f;
        canvas.drawTextWrapped(m_strings.get(Str::DownloadNote), {x, noteTop, w, noteHeight}, TypeStyle::Caption,
                               Color::TextTertiary);
    }

    void setNow(double now) { m_now = now; }

private:
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
    std::filesystem::path m_output;
    ui::Dropdown* m_lang = nullptr;
    ui::CheckField* m_updates = nullptr;
    ui::CheckField* m_edge = nullptr;
    ui::CheckField* m_esd = nullptr;
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
    m_kind = &add<ui::Dropdown>(L"", std::vector<std::wstring>{strings.get(Str::DownloadFilterRelease),
                                                               strings.get(Str::DownloadFilterInsider),
                                                               strings.get(Str::DownloadFilterServer),
                                                               strings.get(Str::DownloadFilterAll)},
                                0);
    m_kind->onChange = [this](int) { refreshList(); };
    m_arch = &add<ui::Dropdown>(L"", std::vector<std::wstring>{L"x64", L"ARM64", strings.get(Str::DownloadFilterAll)}, 0);
    m_arch->onChange = [this](int) { refreshList(); };
    m_refresh = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::DownloadRefresh), ui::icons::Icon::Refresh);
    m_refresh->onInvoke = [this] {
        m_loading = true;
        m_listError.reset();
        refreshList();
        m_controller.listBuilds();
    };
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
        request.output = m_panel->output();
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
    refreshList();
    m_controller.listBuilds();
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
    if (!b || b->id != id || m_panel->language() != language || m_panel->editions() != editions) {
        return;
    }
    std::uint64_t bytes = 0;
    for (const auto& f : WindowsDownloadController::filesFor(files, m_panel->updates())) {
        bytes += f.size;
    }
    m_panel->setSize(bytes);
    m_panel->refreshVisibility();
}

void DownloadPage::requestFiles() {
    const auto* b = selectedBuild();
    const auto editions = m_panel->editions();
    m_panel->refreshVisibility();
    if (b && !editions.empty() && !m_panel->language().empty()) {
        m_controller.listFiles(b->id, m_panel->language(), editions);
    }
}

const core::uup::Build* DownloadPage::selectedBuild() const {
    const int row = m_table->selected();
    return row >= 0 && row < static_cast<int>(m_shown.size()) ? &m_shown[static_cast<std::size_t>(row)] : nullptr;
}

void DownloadPage::select(int row) {
    (void)row;
    const auto* b = selectedBuild();
    m_panel->setBuild(b);
    if (b) {
        m_controller.listLanguages(b->id);
    }
    layout();
}

void DownloadPage::refreshList() {
    const std::wstring query = text::fold(m_search->text());
    const int kind = m_kind->selected();
    const int arch = m_arch->selected();
    const std::wstring keepId = selectedBuild() ? selectedBuild()->id : std::wstring();
    m_shown.clear();
    for (const auto& b : m_all) {
        if (kind == 0 && b.kind != BuildKind::Release) continue;
        if (kind == 1 && b.kind != BuildKind::Insider) continue;
        if (kind == 2 && b.kind != BuildKind::Server) continue;
        if (kind == 3 && b.kind == BuildKind::Update) continue;
        if (arch == 0 && b.arch != L"amd64") continue;
        if (arch == 1 && b.arch != L"arm64") continue;
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
    m_search->setBounds({x, y, 280, kControl});
    x += 280 + 8;
    m_kind->setBounds({x, y, 120, kControl});
    x += 120 + 8;
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
    setBuilds(std::move(builds));
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
        m_panel->refreshVisibility();
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
