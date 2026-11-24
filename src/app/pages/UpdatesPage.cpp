#include "app/pages/UpdatesPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "ui/anim/Tween.h"
#include "ui/widgets/ProgressRing.h"

#include <algorithm>
#include <format>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 0.0f;
constexpr float kDrop = 56.0f;
constexpr Str kKinds[] = {Str::UpdatesKindSsu,   Str::UpdatesKindLcu,      Str::UpdatesKindDotnet,
                          Str::UpdatesKindOther, Str::UpdatesKindLanguage, Str::UpdatesKindSafeOs};
enum Column : int { kHandle, kOrder, kPackage, kKind, kKb, kSize, kState };
} // namespace

// D-046: the catalog search / download, where the drop zone is: progress ring (D-142), what is happening,
// bytes, a 2px bar and "Durdur" (the .part files stay; the next download resumes).
class UpdatesPage::FetchStrip : public ui::Widget {
public:
    FetchStrip(const Localization& strings, const AppState& state, Language language)
        : m_strings(strings), m_state(state), m_language(language) {
        m_stop = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StatusStop), ui::icons::Icon::Stop);
        m_stop->onInvoke = [this] {
            if (onStop) {
                onStop();
            }
        };
    }
    std::function<void()> onStop;

    void start() {
        m_now = ui::nowMs();
        m_ring.start(m_now);
        animate();
    }
    [[nodiscard]] float tickIntervalMs() const override { return m_ring.tickMs(100.0f); } // the ring's pace, else the ETA's
    bool tick(double now) override {
        m_now = now;
        invalidate();
        const bool on = visible() && m_state.updateFetch().has_value();
        if (!on) {
            m_ring.stop();
        }
        return on;
    }
    void layout() override {
        const RectF b = bounds();
        const ui::SizeF size = m_stop->measure({});
        m_stop->setBounds({b.right() - 8 - size.width, b.y + (b.height - size.height) / 2, size.width, size.height});
    }
    void paint(ui::Canvas& canvas) override {
        const auto& fetch = m_state.updateFetch();
        if (!fetch) {
            return;
        }
        const RectF b = bounds();
        canvas.fillRect(b, Color::BgPanel);
        // D-142: the progress ring, centred where the 16 DIP spinner icon was.
        ui::paintRing(canvas, ui::ringBox({b.x + 25, b.y + 20}, ui::tokens::size::ringLarge), m_ring.pose(m_now));
        const float x = b.x + 40;
        const float width = std::max(m_stop->bounds().x - x - 16, 0.0f);
        using Stage = AppState::UpdateFetch::Stage;
        std::wstring title;
        if (fetch->stage == Stage::Searching) {
            title = m_strings.get(Str::UpdatesSearching);
        } else if (fetch->stage == Stage::Verifying) {
            title = m_strings.format(Str::UpdatesVerifyingKb, {{L"kb", fetch->kb}});
        } else {
            title = m_strings.format(Str::UpdatesDownloading, {{L"kb", fetch->kb},
                                                               {L"done", formatBytes(fetch->doneBytes, m_language)},
                                                               {L"total", formatBytes(fetch->totalBytes, m_language)}});
        }
        canvas.drawText(title, {x, b.y + 12, width, 16}, TypeStyle::BodyStrong, Color::TextPrimary);
        if (fetch->stage != Stage::Searching && fetch->totalBytes > 0) {
            const float fraction = std::clamp(static_cast<float>(static_cast<double>(fetch->doneBytes) /
                                                                 static_cast<double>(fetch->totalBytes)),
                                              0.0f, 1.0f);
            const float barWidth = std::max(width - 48, 0.0f);
            canvas.progressBar({x, b.y + 38, barWidth, 2}, fraction);
            canvas.drawText(std::to_wstring(static_cast<int>(fraction * 100)) + L"%", {x + barWidth + 8, b.y + 31, 40, 16},
                            TypeStyle::Mono, Color::TextSecondary, ui::TextAlign::Trailing);
        }
    }

private:
    const Localization& m_strings;
    const AppState& m_state;
    Language m_language;
    ui::Button* m_stop = nullptr;
    double m_now = 0;
    ui::RingClock m_ring;
};

UpdatesPage::UpdatesPage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_drop = &add<ui::DropZone>(strings.get(Str::UpdatesDrop), strings.get(Str::UpdatesDrop),
                                strings.get(Str::SourceDropUnsupported), strings.get(Str::UpdatesDrop));
    m_drop->setCompact(true);
    m_drop->onInvoke = [this] {
        if (m_intents.addPackages) {
            m_intents.addPackages();
        }
    };
    m_fetch = &add<FetchStrip>(strings, state, language);
    m_fetch->onStop = [this] {
        if (m_intents.stopFetch) {
            m_intents.stopFetch();
        }
    };
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 24},
        {strings.get(Str::UpdatesOrder), 48},
        {strings.get(Str::UpdatesPackage), 0},
        {.title = strings.get(Str::UpdatesType), .width = 170, .minWidth = 120},
        {.title = strings.get(Str::UpdatesKb), .width = 104, .minWidth = 90},
        {strings.get(Str::CommonSize), 84, ui::TextAlign::Trailing},
        {.title = strings.get(Str::CommonStatus), .width = 220, .minWidth = 150},
    });
    m_table->setFlexMin(kNameColumnMin); // D-136: the package's file name keeps its room at a narrow window
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kPackage || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return {};
        }
        return {std::filesystem::path(m_rows[static_cast<std::size_t>(row)].target).filename().wstring(), TypeStyle::Body,
                ui::tokens::size::icon + 6};
    };
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_rows.size())) {
            m_state.unqueue(OpKind::AddPackage, m_rows[static_cast<std::size_t>(row)].target);
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::UpdateDownload, strings.get(Str::UpdatesNoMountTitle),
                                   strings.get(Str::UpdatesNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = [this] {
        if (m_intents.goImages) {
            m_intents.goImages();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::UpdatesTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Queue || change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::UpdateFetch) {
            const bool was = m_fetch->visible();
            if (was == (m_state.mounted().has_value() && m_state.updateFetch().has_value())) {
                // Download progress (audit PG-10): the strip reads the state itself; the rows come
                // from the queue, which did not change — no need to plan the whole queue again.
                invalidate();
                return;
            }
            refresh();
            if (!was && m_fetch->visible()) {
                m_fetch->start();
            }
        }
    });
    refresh();
}

UpdatesPage::~UpdatesPage() {
    m_state.unsubscribe(m_subscription);
}

std::size_t UpdatesPage::queuePackages(AppState& state, const std::vector<std::filesystem::path>& files,
                                       std::optional<core::UpdateKind> kind) {
    std::size_t added = 0;
    const auto batch = state.batchQueue(); // a folder of packages: one notification, one undo step (audit PG-04)
    for (const auto& file : files) {
        if (!core::isUpdateFile(file) || state.changes().find(OpKind::AddPackage, file.wstring())) {
            continue;
        }
        const auto info = core::analyzeUpdate(file);
        // A Safe OS update's file name does not say what it is: the catalog does (D-080).
        core::ops::Operation op{OpKind::AddPackage, file.wstring(), core::updateKindKey(kind.value_or(info.kind))};
        op.risk = core::ops::Risk::Low;
        op.sizeDelta = static_cast<std::int64_t>(info.size); // the image grows
        state.queue(std::move(op));
        ++added;
    }
    return added;
}

void UpdatesPage::resetQueued(AppState& state) {
    state.unqueueIf([](const core::ops::Operation& op) { return op.kind == OpKind::AddPackage && op.value != L"language"; });
}

void UpdatesPage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

const core::UpdateInfo& UpdatesPage::infoFor(const std::filesystem::path& path) {
    auto it = m_info.find(path.wstring());
    if (it == m_info.end()) {
        it = m_info.emplace(path.wstring(), core::analyzeUpdate(path)).first;
    }
    return it->second;
}

UpdatesPage::Compat UpdatesPage::compatibility(const core::UpdateInfo& info) const {
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (!mounted || !source) {
        return Compat::Unknown;
    }
    for (const auto& image : source->install.images) {
        if (image.index != mounted->index) {
            continue;
        }
        if (info.targetWindows != 0 && info.targetWindows != core::windowsGeneration(image.build)) {
            return Compat::WrongWindows;
        }
        const std::wstring arch = core::architectureName(image.architecture);
        if (!info.architecture.empty() && info.architecture != arch) {
            return Compat::WrongArch;
        }
        return info.targetWindows == 0 ? Compat::Unknown : Compat::Ok;
    }
    return Compat::Unknown;
}

void UpdatesPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    const bool fetching = m_state.updateFetch().has_value();
    m_empty->setVisible(!mounted);
    m_drop->setVisible(mounted && !fetching);
    m_fetch->setVisible(mounted && fetching);
    m_table->setVisible(mounted);
    m_summaryLine->setVisible(mounted);
    m_rows.clear();
    for (const auto& step : core::ops::plan(m_state.changes()).steps) {
        // Language packs are listed on the Diller page (D-053).
        if (step.operation.kind == OpKind::AddPackage && step.operation.value != L"language") {
            m_rows.push_back(step.operation);
        }
    }
    // D-139: what each row shows, worked out here (the package's file is read once: infoFor keeps it).
    m_texts.clear();
    std::uint64_t bytes = 0;
    std::size_t bad = 0;
    for (const auto& op : m_rows) {
        const auto& info = infoFor(op.target);
        RowText t;
        t.fileIcon = info.path.extension() == L".cab" ? ui::icons::Icon::CabFile : ui::icons::Icon::MsuFile;
        t.file = info.path.filename().wstring();
        // The queue says what it is (the catalog's word for a Safe OS update), the file name the rest.
        const auto kind = op.value == L"safeos" ? core::UpdateKind::SafeOs : info.kind;
        t.kind = m_strings.get(kKinds[static_cast<int>(kind)]);
        t.kb = info.kb.empty() ? std::wstring(L"\u2014") : info.kb;
        t.size = info.size ? formatBytes(info.size, m_language) : std::wstring(L"\u2014");
        t.compat = compatibility(info);
        t.compatText = compatText(t.compat, info);
        bytes += info.size;
        bad += t.compat == Compat::WrongWindows || t.compat == Compat::WrongArch ? 1 : 0;
        m_texts.push_back(std::move(t));
    }
    std::vector<SummaryPart> parts;
    if (!m_rows.empty()) {
        parts.push_back({bytes > 0 ? m_strings.format(Str::UpdatesSummary, {{L"n", formatCount(m_rows.size(), m_language)},
                                                                            {L"size", formatBytes(bytes, m_language)}})
                                   : m_strings.format(Str::UpdatesSummaryN, {{L"n", formatCount(m_rows.size(), m_language)}})});
    }
    if (bad > 0) {
        parts.push_back({m_strings.format(Str::UpdatesSummaryBad, {{L"n", formatCount(bad, m_language)}}), SummaryPart::Tone::Warning});
    }
    m_summaryLine->set(std::move(parts));
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    layout();
    invalidate();
    detailChanged();
}

std::wstring UpdatesPage::compatText(Compat compat, const core::UpdateInfo& info) const {
    switch (compat) {
    case Compat::WrongWindows:
        return m_strings.format(Str::UpdatesIncompatible,
                                {{L"ver", m_strings.format(Str::UpdatesWindowsVersion, {{L"ver", std::to_wstring(info.targetWindows)}})}});
    case Compat::WrongArch: return m_strings.format(Str::UpdatesArchMismatch, {{L"arch", info.architecture}});
    case Compat::Unknown: return m_strings.get(Str::UpdatesUnknown);
    case Compat::Ok: break;
    }
    return m_strings.get(Str::UpdatesCompatible);
}

void UpdatesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || static_cast<std::size_t>(row) >= m_texts.size()) {
        return;
    }
    const RowText& t = m_texts[static_cast<std::size_t>(row)];
    switch (column) {
    case kHandle: canvas.drawIcon(ui::icons::Icon::DragHandle, {rect.x - 4, rect.y + (rect.height - 16) / 2}, Color::TextTertiary); break;
    case kOrder: canvas.drawText(std::to_wstring(row + 1), rect, TypeStyle::Body, Color::TextPrimary); break;
    case kPackage: {
        canvas.drawIcon(t.fileIcon, {rect.x, rect.y + (rect.height - 16) / 2}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(t.file, {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kKind: canvas.drawText(t.kind, rect, TypeStyle::Body, Color::TextPrimary); break;
    case kKb: canvas.drawText(t.kb, rect, TypeStyle::Mono, Color::TextPrimary); break;
    case kSize:
        canvas.drawText(t.size, rect, TypeStyle::Mono, t.size.size() > 1 ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
        break;
    case kState: {
        ui::icons::Icon icon = ui::icons::Icon::SuccessCircle;
        Color ink = Color::StatusSuccess;
        if (t.compat == Compat::WrongWindows || t.compat == Compat::WrongArch) {
            icon = ui::icons::Icon::ErrorOctagon;
            ink = Color::StatusError;
        } else if (t.compat == Compat::Unknown) {
            icon = ui::icons::Icon::InfoCircle;
            ink = Color::TextTertiary;
        }
        canvas.drawIcon(icon, {rect.x, rect.y + (rect.height - 16) / 2}, ink);
        canvas.drawText(t.compatText, {rect.x + 22, rect.y, rect.width - 22, rect.height}, TypeStyle::Caption,
                        t.compat == Compat::Ok ? Color::TextSecondary : ink);
        break;
    }
    default: break;
    }
}

void UpdatesPage::layout() {
    // The drop zone (or the catalog's download), then D-139's header row — the list's title, the summary
    // at its right — and the table.
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    m_fetch->setBounds({b.x, y, b.width, kDrop});
    y += kDrop + kHeaderGap;
    m_titleY = y;
    m_summaryLine->setBounds({b.x + 220.0f, y, std::max(b.width - 220.0f, 0.0f), kHeaderRow});
    y += kHeaderRow + kHeaderGap;
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kKb, kKind, kSize});
}

void UpdatesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF t = m_table->bounds();
    canvas.drawText(m_strings.get(Str::UpdatesListTitle), {t.x, m_titleY, 220.0f, kHeaderRow}, TypeStyle::Section, Color::TextPrimary);
    if (m_rows.empty()) {
        paintTableEmpty(canvas, t, m_strings.get(Str::UpdatesEmptyList));
    }
}

} // namespace wl::app

namespace wl::app {

DetailModel UpdatesPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size()) || static_cast<std::size_t>(row) >= m_texts.size()) {
        return model;
    }
    const auto& op = m_rows[static_cast<std::size_t>(row)];
    const RowText& t = m_texts[static_cast<std::size_t>(row)];
    const auto found = m_info.find(op.target);
    model.icon = t.fileIcon;
    model.title = t.file;
    model.subtitle = t.kind;
    model.identity = std::filesystem::path(op.target).parent_path().wstring();
    model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::DetailWillAdd), DetailTone::Change};
    model.facts.push_back({m_strings.get(Str::UpdatesOrder),
                           m_strings.format(Str::UpdatesPosition, {{L"i", std::to_wstring(row + 1)}, {L"n", std::to_wstring(m_rows.size())}})});
    model.facts.push_back({m_strings.get(Str::UpdatesType), t.kind});
    model.facts.push_back({m_strings.get(Str::UpdatesKb), t.kb, DetailTone::Normal, true});
    model.facts.push_back({m_strings.get(Str::CommonSize), t.size, DetailTone::Normal, true});
    if (found != m_info.end()) {
        const auto& info = found->second;
        std::wstring target;
        if (info.targetWindows != 0) {
            target = m_strings.format(Str::UpdatesWindowsVersion, {{L"ver", std::to_wstring(info.targetWindows)}});
        }
        if (!info.architecture.empty()) {
            target += (target.empty() ? L"" : L" · ") + info.architecture;
        }
        if (!target.empty()) {
            model.facts.push_back({m_strings.get(Str::UpdatesTarget), target});
        }
    }
    const bool bad = t.compat == Compat::WrongWindows || t.compat == Compat::WrongArch;
    if (bad) {
        model.notes.push_back({ui::icons::Icon::ErrorOctagon, t.compatText, DetailTone::Error}); // whole, wrapped
    } else {
        model.facts.push_back({m_strings.get(Str::CommonStatus), t.compatText, t.compat == Compat::Ok ? DetailTone::Success : DetailTone::Quiet});
    }
    model.actions.push_back({kActionRemove, m_strings.get(Str::UpdatesDequeue)});
    return model;
}

void UpdatesPage::detailAction(int id) {
    const int row = m_table->selected();
    if (id == kActionRemove && row >= 0 && row < static_cast<int>(m_rows.size())) {
        const std::wstring target = m_rows[static_cast<std::size_t>(row)].target; // a copy: the queue notifies
        m_state.unqueue(OpKind::AddPackage, target); // the row's Delete
    }
}

} // namespace wl::app
