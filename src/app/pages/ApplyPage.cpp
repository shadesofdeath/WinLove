#include "app/pages/ApplyPage.h"

#include "app/ApplyReport.h"

#include "core/image/Source.h"
#include "core/image/dism/Edition.h"

#include "app/Format.h"
#include "app/pages/LogsPage.h"
#include "app/pages/PageBits.h"
#include "app/pages/apply/RiskConfirm.h"
#include "app/pages/iso/IsoOptions.h"
#include "ui/anim/Tween.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Phase;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Run = AppState::ApplyRun;
using Outcome = RunSummary::Outcome;

namespace {

constexpr float kTop = 0.0f;
constexpr float kGap = 10.0f;
constexpr float kCardGap = ui::tokens::spacing::s5; // under a head card
constexpr float kInfoBar = ui::InfoBar::kHeight;
constexpr float kStepsWidth = 400.0f;
// A step row holds two lines (the group, then "done / total · the step"): 24 (D-109) let the second
// line run into the next row (user report 2026-11-09).
constexpr float kStepRow = 42.0f;
constexpr float kStepGap = 2.0f;
constexpr float kListCaption = 22.0f; // "Adımlar" / "Canlı log" over the two columns
constexpr float kEditionsLabel = 220.0f; // "Diğer sürümlere de uygula" column (D-055)
constexpr float kEditionsRow = 28.0f;
constexpr float kIsoSummaryLine = 16.0f; // D-144 addendum: the ISO's options, a caption line under its row

// "~2 dk–4 dk": the estimate as a range (the run's pace varies with the disk and the image).
std::wstring timeRange(double seconds, Language language) {
    return formatDuration(seconds * 0.7, language, true) + L"–" + formatDuration(seconds * 1.4, language, true).substr(1);
}

} // namespace

ApplyPage::Mode ApplyPage::modeFor(const AppState& state) {
    const auto& run = state.applyRun();
    if (run && run->stage != Run::Stage::Done) {
        return run->stage == Run::Stage::Paused ? Mode::Paused : Mode::Running;
    }
    // 2026-11-11: what the run left mounted is being saved / discarded, or was: the run's card says it.
    if (run && run->closing) {
        return Mode::Done;
    }
    // Held (a step failed, nothing saved), stopped, or left unsaved at a pause: the image is still
    // mounted. The report with the reasons comes first, until the queue is touched (A7).
    if (run && run->result && state.mounted() && state.changes().version() == run->queueVersionAtEnd &&
        (run->result->held || run->result->kept || !run->result->report.completed)) {
        return Mode::Done;
    }
    if (state.mounted() && !state.changes().empty()) {
        return Mode::Summary;
    }
    if (run) {
        return Mode::Done;
    }
    return state.mounted() ? Mode::Empty : Mode::NoMount;
}

int ApplyPage::modeKey(const AppState& state) {
    int key = static_cast<int>(modeFor(state)) * 1000;
    if (const auto& run = state.applyRun(); run && run->closing) {
        key += (1 + static_cast<int>(run->closing->state) * 2 + (run->closing->commit ? 1 : 0)) * 10;
    }
    // D-144: the ISO after the run — its buttons change with its state (the page is built again).
    if (const auto& run = state.applyRun(); run && run->autoIso) {
        key += 1 + static_cast<int>(run->autoIso->state);
    }
    return key;
}

bool ApplyPage::awaitingSave(const AppState& state) {
    const auto& run = state.applyRun();
    return run && run->stage == Run::Stage::Done && run->result && state.mounted() && run->result->report.completed &&
           (run->result->held || run->result->kept) && state.changes().version() == run->queueVersionAtEnd;
}

std::wstring ApplyPage::displayName(const AppState& state, const core::ops::Operation& op) {
    if (op.kind == OpKind::RemoveComponent || op.kind == OpKind::CleanupImage || op.kind == OpKind::ShrinkStore) {
        // The operation carries the name it was queued under.
        const std::wstring title = core::componentTitle(op.value);
        return title.empty() ? op.target : title;
    }
    if (op.kind == OpKind::SetEdition) {
        // "Windows 11 Pro", for the build of the image it is queued on.
        int build = 26100;
        if (const auto& source = state.source(); source && !source->install.images.empty()) {
            build = source->install.images.front().build;
        }
        return core::editionDisplayName(op.value, build);
    }
    if (op.kind == OpKind::RemoveAppx) {
        // The catalog name it was queued under; from a bare changeset file, the package identity
        // ("Microsoft.BingNews_4.55…_8wekyb3d8bbwe" → "Microsoft.BingNews").
        return !op.value.empty() ? op.value : op.target.substr(0, op.target.find(L'_'));
    }
    if (const auto& features = state.optionalFeatures()) {
        for (const auto& item : features->items) {
            if (item.name == op.target) {
                return item.displayName;
            }
        }
    }
    return op.target;
}

ApplyPage::ApplyPage(AppState& state, ApplyController& controller, const ImageSettingsCatalog& settings,
                     const Localization& strings, Language language, Intents intents)
    : m_state(state), m_controller(controller), m_settings(settings), m_strings(strings), m_language(language),
      m_intents(std::move(intents)),
      m_mode(modeFor(state)) {
    setAccessible(ui::AccessRole::Group, strings.get(Str::ApplyTitle));
    switch (m_mode) {
    case Mode::Summary: buildSummary(); break;
    case Mode::Running:
    case Mode::Paused: buildRunning(); break;
    case Mode::Done: buildDone(); break;
    default: buildEmpty(); break;
    }
    // Live texts while running; mode switches are handled by the Shell (it rebuilds the page).
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Apply && live()) {
            refreshRun();
            invalidate();
        }
        if (change == AppState::Change::Operation && m_result) {
            refreshClosing();
        }
        if (change == AppState::Change::Iso && m_result) {
            refreshIso(); // D-144
        }
        // Iso: the options changed (here, in their dialog, or on the ISO page) or the media update.
        if (m_iso && (change == AppState::Change::Settings || change == AppState::Change::Source ||
                      change == AppState::Change::Unattend || change == AppState::Change::Iso)) {
            syncIsoOption();
        }
        if (m_waitBar && (change == AppState::Change::UpdateFetch || change == AppState::Change::LanguageFetch ||
                          change == AppState::Change::StoreFetch)) {
            syncWaitBar();
        }
    });
}

ApplyPage::~ApplyPage() {
    m_state.unsubscribe(m_subscription);
}

// ---- rows / names -------------------------------------------------------------------------

std::vector<ApplyPage::Row> ApplyPage::rows() const {
    std::vector<Row> result;
    for (const auto& g : m_groups) {
        result.push_back({g});
    }
    result.push_back({std::nullopt}); // commit + unmount
    return result;
}

std::vector<ApplyPage::Row> ApplyPage::doneRows() const {
    std::vector<Row> result = rows();
    const auto& run = m_state.applyRun();
    if (run && run->result) {
        const auto& results = run->result->report.results;
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (!results[i].outcome) {
                result.push_back({std::nullopt, static_cast<int>(i)});
            }
        }
    }
    return result;
}

std::wstring ApplyPage::skipReason(const Error& error) const {
    return applySkipReason(m_strings, error); // the report words it the same way
}

std::wstring ApplyPage::groupName(const Row& row) const {
    if (!row.group) {
        return m_strings.get(virtualDiskSource() ? Str::ApplyOpsCommitVhd : Str::ApplyOpsCommitUnmount);
    }
    return applyPhaseName(m_strings, row.group->phase);
}

ui::icons::Icon ApplyPage::groupIcon(const Row& row) {
    if (!row.group) {
        return ui::icons::Icon::ImageWim;
    }
    switch (row.group->phase) {
    case Phase::Edition: return ui::icons::Icon::LayersEditions;
    case Phase::Remove: return ui::icons::Icon::ComponentsRemove;
    case Phase::Features: return ui::icons::Icon::PuzzleFeatures;
    case Phase::Drivers: return ui::icons::Icon::DriverChip;
    case Phase::Updates: return ui::icons::Icon::UpdateDownload;
    case Phase::Apps: return ui::icons::Icon::AppxPackage;
    case Phase::DeepRemove: return ui::icons::Icon::DriverChip;
    case Phase::Cleanup: return ui::icons::Icon::SizeSaved;
    case Phase::Settings: return ui::icons::Icon::Registry;
    case Phase::Shrink: return ui::icons::Icon::SizeSaved;
    }
    return ui::icons::Icon::File;
}

// ---- header -------------------------------------------------------------------------------

std::pair<std::wstring, std::wstring> ApplyPage::header() const {
    const auto& run = m_state.applyRun();
    switch (m_mode) {
    case Mode::Summary:
        return {m_strings.get(Str::ApplyTitle),
                m_strings.format(Str::ApplyDesc, {{L"n", std::to_wstring(m_state.changes().size())}})};
    case Mode::Running: {
        std::size_t done = 0;
        for (int s : run->stepState) {
            done += s >= 2 ? 1 : 0;
        }
        const std::size_t total = run->stepState.size() + 1;
        const double elapsed = (ui::nowMs() - run->startedMs) / 1000.0;
        const double f = std::clamp(run->fraction, 0.0, 1.0);
        const std::wstring eta = f > 0.03 ? formatDuration(elapsed * (1 - f) / f, m_language, true).substr(1) : L"…";
        return {m_strings.get(Str::ApplyRunning),
                m_strings.format(Str::ApplyRunningDesc, {{L"done", std::to_wstring(done)},
                                                         {L"total", std::to_wstring(total)},
                                                         {L"pct", std::to_wstring(static_cast<int>(f * 100))},
                                                         {L"eta", eta}})};
    }
    case Mode::Paused: return {m_strings.get(Str::ApplyPausedTitle), m_strings.get(Str::ApplyPausedDesc)};
    case Mode::Done: {
        if (run->closing) {
            // 2026-11-11: the save / discard after the run is the news now.
            const RunSummary s = summarizeRun(*run, ui::nowMs());
            const auto texts = resultTexts(s);
            return {texts.title, texts.body};
        }
        if (run->error) {
            return {m_strings.get(Str::ApplyFailedTitle), run->error->message};
        }
        const auto& r = *run->result;
        const std::wstring took = formatDuration(static_cast<double>(r.elapsed.count()) / 1000.0, m_language);
        if (!r.report.completed) {
            return {m_strings.get(Str::ApplyStoppedTitle),
                    m_strings.format(Str::ApplyStoppedDesc, {{L"done", std::to_wstring(r.report.results.size())},
                                                             {L"total", std::to_wstring(run->plan.steps.size())}})};
        }
        if (r.commitError) {
            return {m_strings.get(Str::ApplyFailedTitle),
                    m_strings.format(Str::ApplyCommitFailedBody, {{L"e", r.commitError->message}})};
        }
        if (r.held) {
            return {m_strings.format(Str::ApplyHeldTitle, {{L"n", std::to_wstring(r.report.failures())}}),
                    m_strings.get(Str::ApplyHeldDesc)};
        }
        if (r.kept) {
            return {m_strings.get(Str::ApplyKeptTitle), m_strings.get(Str::ApplyKeptDesc)};
        }
        return {m_strings.get(Str::ApplyDoneTitle),
                m_strings.format(virtualDiskSource() ? Str::ApplyDoneDescVhd : Str::ApplyDoneDesc,
                                 {{L"n", std::to_wstring(run->plan.steps.size())}, {L"t", took}})};
    }
    case Mode::NoMount:
    case Mode::Empty: break;
    }
    return {m_strings.get(Str::ApplyTitle), m_strings.get(Str::ApplyEmptyDesc)};
}

// ---- modes --------------------------------------------------------------------------------

void ApplyPage::buildEmpty() {
    if (m_mode == Mode::NoMount) {
        m_empty = &add<ui::EmptyState>(ui::icons::Icon::ApplyPlay, m_strings.get(Str::ApplyNoMountTitle),
                                       m_strings.get(Str::ApplyNoMountBody));
        m_empty->setAction(m_strings.get(Str::CommonGoImages)).onInvoke = m_intents.goImages;
    } else {
        m_empty = &add<ui::EmptyState>(ui::icons::Icon::ApplyPlay, m_strings.get(Str::ApplyEmptyTitle),
                                       m_strings.get(Str::ApplyEmptyBody));
        m_empty->setAction(m_strings.get(Str::ApplyGoFeatures)).onInvoke = m_intents.goFeatures;
    }
}

void ApplyPage::buildSummary() {
    m_plan = m_controller.currentPlan();
    m_groups = core::ops::groups(m_plan);
    const auto planned = planRows(m_plan);
    const double seconds = planSeconds(m_groups);
    const auto& changes = m_state.changes();

    // D-138: the head card — the plan in one line, the size before → after, the time, the button.
    {
        PlanHeader::Texts head;
        head.title = m_strings.format(Str::ApplyPlanTitle, {{L"n", std::to_wstring(changes.size())},
                                                            {L"g", std::to_wstring(m_groups.size() + 1)}}); // + the save
        std::wstring edition;
        std::uint64_t before = 0;
        if (const auto& mounted = m_state.mounted()) {
            edition = mounted->edition;
            if (const auto& source = m_state.source()) {
                for (const auto& image : source->install.images) {
                    if (image.index == mounted->index) {
                        before = image.totalBytes;
                        edition = image.name;
                    }
                }
            }
        }
        head.subtitle = m_strings.format(Str::ApplyPlanSubtitle, {{L"edition", edition}});
        head.note = m_strings.get(Str::ApplyPlanNote);
        const std::int64_t delta = changes.estimatedSizeDelta();
        head.sizeLabel = m_strings.get(Str::OverviewSize);
        if (before > 0) {
            const auto after = static_cast<std::uint64_t>(std::max<std::int64_t>(static_cast<std::int64_t>(before) + delta, 0));
            head.before = before;
            head.after = after;
            head.sizeValue = m_strings.format(Str::OverviewSizeValue,
                                              {{L"before", formatBytes(before, m_language)}, {L"after", formatBytes(after, m_language)}});
        } else {
            head.sizeValue = m_strings.get(Str::CommonNone);
        }
        if (delta < 0) {
            const auto saved = static_cast<std::uint64_t>(-delta);
            const int pct = before > 0 ? static_cast<int>(std::lround(100.0 * static_cast<double>(saved) / static_cast<double>(before))) : 0;
            head.sizeDelta = before > 0 ? m_strings.format(Str::OverviewSizeSmaller,
                                                           {{L"size", formatBytes(saved, m_language)}, {L"pct", std::to_wstring(pct)}})
                                        : m_strings.format(Str::OverviewSizeSaved, {{L"size", formatBytes(saved, m_language)}});
            head.deltaInk = Color::StatusSuccess;
        } else if (delta > 0) {
            head.sizeDelta = m_strings.format(Str::OverviewSizeLarger, {{L"size", formatBytes(static_cast<std::uint64_t>(delta), m_language)}});
        } else {
            head.sizeDelta = m_strings.get(Str::OverviewSizeSame);
        }
        head.time = m_strings.format(Str::ApplyPlanTime, {{L"t", timeRange(seconds, m_language)}});
        head.action = m_strings.format(Str::ApplyApplyN, {{L"n", std::to_wstring(changes.size())}});
        m_planHeader = &add<PlanHeader>();
        m_planHeader->onApply = m_intents.start;
        m_planHeader->set(std::move(head));
    }

    const auto risky = m_controller.highRisk();
    if (!risky.empty()) {
        const auto named = RiskConfirm::itemsFor(m_state, m_settings, m_strings, m_language, risky);
        std::wstring items;
        for (std::size_t i = 0; i < named.size() && i < 3; ++i) {
            items += (i ? L", " : L"") + named[i].name;
        }
        if (named.size() > 3) {
            items += L" …";
        }
        const bool removals = std::ranges::any_of(named, &RiskConfirm::Item::removal);
        const bool settings = std::ranges::any_of(named, [](const RiskConfirm::Item& i) { return !i.removal; });
        m_riskBar = &add<ui::InfoBar>(ui::InfoKind::Warning,
                                      m_strings.format(Str::ApplyHighRiskN, {{L"n", std::to_wstring(named.size())}}),
                                      m_strings.format(!settings  ? Str::ApplyHighRiskBody
                                                       : removals ? Str::ApplyHighRiskBodyMixed
                                                                  : Str::ApplyHighRiskBodySettings,
                                                       {{L"items", items}}),
                                      m_strings.get(Str::CommonClose));
        m_riskBar->setAction(m_strings.get(Str::ApplyReview), m_intents.review);
        m_riskBar->onClose = [this] {
            m_riskBar->setVisible(false);
            layout();
        };
    }

    // D-124: a download that will queue packages holds Uygula (it went without them before).
    m_waitBar = &add<ui::InfoBar>(ui::InfoKind::Warning, L"", L"", m_strings.get(Str::CommonClose));
    m_waitBar->setVisible(false);
    m_waitBar->onClose = [this] {
        m_waitDismissed = true; // until this download is over
        m_waitBar->setVisible(false);
        layout();
    };
    syncWaitBar();

    // D-124: what the queue breaks, feature by feature, and the guard that would keep each.
    if (m_intents.impact) {
        std::vector<ImpactCard::Row> rows;
        for (const auto& impact : m_intents.impact()) {
            std::wstring items;
            for (std::size_t i = 0; i < impact.items.size(); ++i) {
                items += (i ? L", " : L"") + impact.items[i];
            }
            rows.push_back({impact.guard, impact.name, std::move(items), impact.app});
        }
        m_impact = &add<ImpactCard>(m_strings, std::move(rows));
        m_impact->onKeep = m_intents.keep;
        m_impact->onMore = m_intents.openCompat;
    }

    // D-055: the same queue on other editions of the WIM.
    for (const auto& e : m_controller.otherEditions()) {
        const bool on = std::ranges::find(m_controller.extraEditions(), e.index) != m_controller.extraEditions().end();
        auto& check = add<ui::CheckField>(e.name, on);
        const int index = e.index;
        check.onChange = [this, index](bool checked) {
            m_controller.setExtraEdition(index, checked);
            invalidate();
        };
        m_editions.push_back(&check);
    }
    // D-106: stop before each save, the image mounted, for changes by hand (kept in the settings).
    m_pause = &add<ui::CheckField>(m_strings.get(Str::ApplyPauseOption), m_state.settings().pauseBeforeSave);
    m_pause->onChange = [this](bool checked) {
        AppSettings settings = m_state.settings();
        settings.pauseBeforeSave = checked;
        m_state.setSettings(std::move(settings));
    };
    // D-125: after setup, a task keeps what ran when an update puts it back (kept in the settings).
    m_guard = &add<ui::CheckField>(m_strings.get(Str::ApplyGuardOption), m_state.settings().updateGuard);
    m_guard->setTooltip(m_strings.get(Str::ApplyGuardHint));
    m_guard->onChange = [this](bool checked) {
        AppSettings settings = m_state.settings();
        settings.updateGuard = checked;
        m_state.setSettings(std::move(settings));
    };
    // D-144: the setup ISO right after the save (kept in the settings, as the two above).
    if (m_intents.isoUnavailable) {
        m_iso = &add<ui::CheckField>(m_strings.get(Str::ApplyAutoIsoOption), m_state.settings().isoAfterApply);
        m_iso->setTooltip(m_strings.get(Str::ApplyAutoIsoHint));
        m_iso->onChange = [this](bool checked) {
            AppSettings settings = m_state.settings();
            settings.isoAfterApply = checked;
            m_state.setSettings(std::move(settings));
        };
        m_isoChange = &add<ui::Button>(ui::ButtonKind::Subtle, m_strings.get(Str::ApplyAutoIsoChange), ui::icons::Icon::OpenFolder);
        m_isoChange->onInvoke = [this] {
            if (m_intents.pickIsoOutput) {
                m_intents.pickIsoOutput();
                syncIsoOption();
            }
        };
        // D-144 addendum: the options the ISO is built with — ISO Oluştur's own, in a dialog.
        m_isoOptions = &add<ui::Button>(ui::ButtonKind::Subtle, m_strings.get(Str::ApplyAutoIsoOptions), ui::icons::Icon::TweaksSliders);
        m_isoOptions->setTooltip(m_strings.get(Str::ApplyAutoIsoOptionsHint));
        m_isoOptions->onInvoke = [this] {
            if (m_intents.isoOptions) {
                m_intents.isoOptions();
            }
        };
        syncIsoOption();
    }

    // The plan, group by group: its steps, what they do to the size, the high-risk ones, the time.
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {m_strings.get(Str::ApplyOrder), 48},
        {m_strings.get(Str::ApplyOperation), 0},
        {m_strings.get(Str::ApplySteps), 104, ui::TextAlign::Trailing, false, 72},
        {m_strings.get(Str::ApplySizeColumn), 120, ui::TextAlign::Trailing, false, 92},
        {m_strings.get(Str::ApplyEta), 112, ui::TextAlign::Trailing, false, 80},
    });
    m_table->setFlexMin(kNameColumnMin);
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintSummaryCell(c, row, column, rect);
    };
    m_rowCache = rows();
    m_planCells.clear();
    for (std::size_t i = 0; i < m_rowCache.size(); ++i) {
        const Row& r = m_rowCache[i];
        PlanCells cells;
        cells.order = std::to_wstring(i + 1);
        cells.name = groupName(r);
        if (r.group && i < planned.size()) {
            const auto& p = planned[i];
            cells.steps = std::to_wstring(r.group->count);
            cells.time = formatDuration(r.group->estimateSeconds, m_language, true);
            if (p.sizeDelta < 0) {
                cells.size = L"−" + formatBytes(static_cast<std::uint64_t>(-p.sizeDelta), m_language);
                cells.saves = true;
            } else if (p.sizeDelta > 0) {
                cells.size = L"+" + formatBytes(static_cast<std::uint64_t>(p.sizeDelta), m_language);
            } else {
                cells.size = L"—";
            }
            if (p.risky > 0) {
                cells.risky = m_strings.format(Str::ApplyRiskyN, {{L"n", std::to_wstring(p.risky)}});
            }
        } else {
            cells.steps = L"1";
            cells.size = L"—";
            cells.time = formatDuration(core::ops::kCommitSeconds + core::ops::kOptimizeSeconds, m_language, true);
        }
        m_planCells.push_back(std::move(cells));
    }
    m_table->setRowCount(static_cast<int>(m_rowCache.size()));
}

void ApplyPage::buildRunning() {
    const auto& run = m_state.applyRun();
    m_groups = run->groups;
    m_rowCache = rows();
    m_logVersion = run->logVersion;
    // D-138: the head card (phase, step, percentage, times, counts; paused: why and the choices).
    m_runHeader = &add<RunHeader>(m_strings);
    m_runHeader->onOpenFolder = m_intents.openFolder;
    m_runHeader->onStop = m_intents.stop;
    m_runHeader->onResume = m_intents.resume;
    m_log = &add<ui::LogConsole>();
    m_log->newLinesText = [this](std::size_t n) { return m_strings.format(Str::LogsNewLines, {{L"n", std::to_wstring(n)}}); };
    // The same cap as the log ring buffer and the Logs page: a long run no longer grows the console
    // without end; the oldest lines go, the selection and the view stay put (audit PG-09 / D11).
    constexpr std::size_t kMaxLogLines = 20000;
    m_log->setLineLimit(kMaxLogLines);
    refreshRun();
    poll();
}

std::wstring ApplyPage::runLine(const RunSummary& s) const {
    std::wstring line = m_strings.format(Str::ApplyStepOf, {{L"done", std::to_wstring(s.ok + s.skipped)}, {L"total", std::to_wstring(s.total + 1)}});
    line += L" · " + m_strings.format(Str::ApplyRunElapsed, {{L"t", formatDuration(s.elapsedSeconds, m_language)}});
    if (s.remainingSeconds >= 0) {
        line += L" · " + m_strings.format(Str::ApplyRunRemaining, {{L"t", formatDuration(s.remainingSeconds, m_language, true).substr(1)}});
    }
    return line;
}

std::array<DotCount, 3> ApplyPage::dotCounts(const RunSummary& s) const {
    return {DotCount{m_strings.format(Str::ApplyRunOk, {{L"n", std::to_wstring(s.ok)}}), Color::StatusSuccess, 0},
            DotCount{m_strings.format(Str::ApplySkipped, {{L"n", std::to_wstring(s.skipped)}}),
                     s.skipped ? Color::StatusWarning : Color::TextTertiary, 0},
            DotCount{s.active() || s.pending > 0
                         ? m_strings.format(s.active() ? Str::ApplyRunPending : Str::ApplyRunNotRun, {{L"n", std::to_wstring(s.pending)}})
                         : std::wstring(),
                     Color::TextTertiary, 0}};
}

void ApplyPage::refreshRun() {
    if (!m_runHeader) {
        return;
    }
    const auto& opt = m_state.applyRun();
    if (!opt) {
        return;
    }
    const auto& run = *opt;
    const RunSummary s = summarizeRun(run, ui::nowMs());
    m_runSecond = static_cast<std::int64_t>(s.elapsedSeconds);
    RunHeader::Texts texts;
    texts.fraction = static_cast<float>(s.fraction);
    texts.pct = m_strings.format(Str::ApplyPct, {{L"pct", std::to_wstring(static_cast<int>(s.fraction * 100))}});
    texts.line = runLine(s);
    texts.counts = dotCounts(s);
    if (s.outcome == Outcome::Paused) {
        texts.paused = true;
        texts.icon = ui::icons::Icon::Pause;
        texts.ink = Color::StatusInfo;
        texts.title = m_strings.get(Str::ApplyPausedTitle);
        const std::wstring folder = m_state.mounted() ? m_state.mounted()->mountDir.wstring() : std::wstring();
        texts.body = run.extraCurrent >= 0
                         ? m_strings.format(Str::ApplyPausedEditionBody,
                                            {{L"name", run.extras[static_cast<std::size_t>(run.extraCurrent)].name}, {L"folder", folder}})
                         : m_strings.format(Str::ApplyPausedBody, {{L"folder", folder}});
    } else if (s.outcome == Outcome::Committing) {
        texts.icon = ui::icons::Icon::Save;
        texts.title = m_strings.get(virtualDiskSource() ? Str::ApplyOpsCommitVhd : Str::ApplyOpsCommitUnmount);
    } else {
        // No icon: the head card shows the progress ring (D-142).
        if (s.current >= 0 && static_cast<std::size_t>(s.current) < run.plan.steps.size()) {
            const auto step = static_cast<std::size_t>(s.current);
            texts.title = applyPhaseName(m_strings, run.plan.steps[step].phase);
            texts.detail = activeName(step, run.plan.steps[step].operation);
        } else {
            texts.title = m_strings.get(Str::ApplyRunning);
        }
    }
    if (!run.extras.empty()) {
        // "Sürüm 2 / 3 · Windows 11 Home" while the queue goes to the other editions.
        const int k = run.extraCurrent + 2; // the mounted one is 1
        const std::wstring name = run.extraCurrent >= 0 ? run.extras[static_cast<std::size_t>(run.extraCurrent)].name : run.edition;
        texts.edition = m_strings.format(Str::ApplyEditionOf, {{L"k", std::to_wstring(run.extraCurrent >= 0 ? k : 1)},
                                                              {L"n", std::to_wstring(run.extras.size() + 1)},
                                                              {L"name", name}});
    }
    m_runHeader->set(std::move(texts));

    // The step list: one line per group, then the save; its texts here, not in paint().
    m_stepLines.clear();
    for (const auto& r : m_rowCache) {
        StepLine line;
        line.name = groupName(r);
        std::size_t done = 0;
        std::size_t total = 1;
        bool active = false;
        std::size_t failed = 0;
        std::wstring current;
        if (r.group) {
            total = r.group->count;
            for (std::size_t i = r.group->first; i < r.group->first + r.group->count && i < run.stepState.size(); ++i) {
                done += run.stepState[i] >= 2 ? 1 : 0;
                failed += run.stepState[i] == 3 ? 1 : 0;
                if (run.stepState[i] == 1) {
                    active = true;
                    current = activeName(i, run.plan.steps[i].operation);
                }
            }
        } else {
            active = run.stage == Run::Stage::Committing;
            done = run.result && run.result->committed ? 1 : 0;
        }
        const bool complete = done == total && !active;
        line.state = active ? 1 : complete ? (failed > 0 ? 3 : 2) : 0;
        line.caption = active || complete ? std::format(L"{} / {}", done, total)
                                          : m_strings.format(Str::ApplyStepsN, {{L"n", std::to_wstring(total)}});
        if (failed > 0) {
            line.caption += L" · " + m_strings.format(Str::ApplySkipped, {{L"n", std::to_wstring(failed)}});
        }
        if (active && !current.empty()) {
            line.caption += L" · " + current;
        }
        m_stepLines.push_back(std::move(line));
    }
    // D-144: the ISO after the save, as the last line (it waits for the save).
    if (run.autoIso) {
        StepLine iso;
        iso.name = m_strings.get(Str::ApplyAutoIsoStep);
        iso.caption = m_strings.format(Str::ApplyAutoIsoStepCaption, {{L"file", run.autoIso->output.filename().wstring()}});
        iso.state = 0;
        m_stepLines.push_back(std::move(iso));
    }
    // D-142: a line running → its ring turns (the page ticks); none → the clock stops.
    if (std::ranges::any_of(m_stepLines, [](const StepLine& l) { return l.state == 1; })) {
        m_ringNow = ui::nowMs();
        m_stepRing.start(m_ringNow);
        if (m_stepRing.turning()) {
            animate();
        }
    } else {
        m_stepRing.stop();
    }
}

bool ApplyPage::tick(double now) {
    m_ringNow = now;
    if (!live() || !m_stepRing.turning()) {
        return false;
    }
    invalidate();
    return true;
}

bool ApplyPage::virtualDiskSource() const {
    const auto& source = m_state.source();
    return source && core::isVirtualDiskFormat(source->format);
}

void ApplyPage::syncIsoOption() {
    if (!m_iso) {
        return;
    }
    const auto blocker = m_intents.isoUnavailable ? m_intents.isoUnavailable() : std::nullopt;
    m_isoAvailable = !blocker;
    m_iso->setEnabled(m_isoAvailable);
    m_iso->setChecked(m_isoAvailable && m_state.settings().isoAfterApply);
    const bool on = m_isoAvailable && m_state.settings().isoAfterApply;
    m_isoChange->setVisible(on);
    m_isoOptions->setVisible(on);
    m_isoLine = blocker ? isoBlockerText(static_cast<int>(*blocker))
                        : (m_intents.isoOutput ? m_intents.isoOutput().wstring() : std::wstring());
    m_isoSummary = on ? isoChoicesSummary(m_state, m_strings) : std::wstring();
    layout();
    invalidate();
}

std::wstring ApplyPage::isoBlockerText(int blocker) const {
    using Blocker = IsoController::Blocker;
    switch (static_cast<Blocker>(blocker)) {
    case Blocker::NoSource: return m_strings.get(Str::ApplyAutoIsoNoSource);
    case Blocker::WimOnly: return m_strings.get(Str::ApplyAutoIsoWimOnly);
    case Blocker::VirtualDisk: return m_strings.get(Str::ApplyAutoIsoVirtualDisk);
    case Blocker::UnattendInvalid: return m_strings.get(Str::ApplyAutoIsoUnattend);
    case Blocker::Mounted: return m_strings.get(Str::IsoBlockMounted);
    case Blocker::Busy: return m_strings.get(Str::IsoBlockBusy);
    }
    return {};
}

std::wstring ApplyPage::isoStageText(int stage) const {
    switch (stage) {
    case 0: return m_strings.get(Str::IsoStageExtract);
    case 1: return m_strings.get(Str::IsoStageRepack);
    case 3: return m_strings.get(Str::IsoStageSha);
    case 4: return m_strings.get(Str::IsoStageBoot);
    default: return m_strings.get(Str::IsoStageWrite);
    }
}

std::optional<ResultCard::Texts::Iso> ApplyPage::isoTexts() const {
    const auto& run = m_state.applyRun();
    if (!run || !run->autoIso) {
        return std::nullopt;
    }
    using State = Run::AutoIso::State;
    using Skip = Run::AutoIso::Skip;
    const auto& a = *run->autoIso;
    ResultCard::Texts::Iso t;
    const std::wstring file = a.output.filename().wstring();
    switch (a.state) {
    case State::Waiting:
        t.icon = ui::icons::Icon::QueueClock;
        t.ink = Color::TextTertiary;
        t.text = m_strings.format(Str::ApplyAutoIsoStepCaption, {{L"file", file}});
        break;
    case State::Running: {
        const auto& iso = m_state.isoRun();
        const double f = iso ? std::clamp(iso->fraction, 0.0, 1.0) : 0.0;
        t.running = true;
        t.text = m_strings.format(Str::ApplyAutoIsoRunning, {{L"stage", isoStageText(iso ? iso->stage : 2)}});
        t.fraction = static_cast<float>(f);
        t.progressText = m_strings.format(Str::ApplyPct, {{L"pct", std::to_wstring(static_cast<int>(f * 100))}});
        break;
    }
    case State::Done:
        t.icon = ui::icons::Icon::SuccessCircle;
        t.ink = Color::StatusSuccess;
        t.text = m_strings.format(Str::ApplyAutoIsoDone, {{L"file", file}, {L"size", formatBytes(a.bytes, m_language)}});
        break;
    case State::Failed:
        t.icon = ui::icons::Icon::ErrorOctagon;
        t.ink = Color::StatusError;
        t.text = m_strings.format(Str::ApplyAutoIsoFailed, {{L"e", a.error ? a.error->message : std::wstring()}});
        break;
    case State::Cancelled:
        t.icon = ui::icons::Icon::Stop;
        t.ink = Color::StatusWarning;
        t.text = m_strings.get(Str::ApplyAutoIsoCancelled);
        break;
    case State::NotStarted:
        t.icon = ui::icons::Icon::InfoCircle;
        t.ink = Color::StatusWarning;
        switch (a.skip) {
        case Skip::RunFailed:
        case Skip::None: t.text = m_strings.get(Str::ApplyAutoIsoSkipRunFailed); break;
        case Skip::Stopped: t.text = m_strings.get(Str::ApplyAutoIsoSkipStopped); break;
        case Skip::NotSaved: t.text = m_strings.get(Str::ApplyAutoIsoSkipNotSaved); break;
        case Skip::Edition: t.text = m_strings.get(Str::ApplyAutoIsoSkipEdition); break;
        case Skip::Blocked: t.text = m_strings.format(Str::ApplyAutoIsoSkipBlocked, {{L"why", isoBlockerText(a.blocker)}}); break;
        case Skip::Welcome: t.text = m_strings.get(Str::ApplyAutoIsoSkipWelcome); break;
        }
        break;
    }
    return t;
}

std::vector<ResultCard::Action> ApplyPage::isoActions(const RunSummary& s) const {
    using ui::icons::Icon;
    std::vector<ResultCard::Action> actions;
    const auto& run = m_state.applyRun();
    if (!run || !run->autoIso) {
        return actions;
    }
    if (s.iso == RunSummary::Iso::Running && m_intents.cancelIso) {
        actions.push_back({m_strings.get(Str::ApplyAutoIsoCancel), Icon::Stop, m_intents.cancelIso, false});
    } else if (s.iso == RunSummary::Iso::Done && m_intents.showIsoInFolder) {
        const std::filesystem::path output = run->autoIso->output;
        actions.push_back({m_strings.get(Str::ApplyAutoIsoShow), Icon::OpenFolder,
                           [show = m_intents.showIsoInFolder, output] { show(output); }, true});
    }
    return actions;
}

void ApplyPage::refreshIso() {
    const auto& run = m_state.applyRun();
    if (!m_result || !run || !run->autoIso || run->autoIso->state != Run::AutoIso::State::Running) {
        return;
    }
    if (const auto texts = isoTexts()) {
        m_result->setIsoProgress(texts->text, texts->fraction, texts->progressText);
    }
}

void ApplyPage::refreshClosing() {
    const auto& run = m_state.applyRun();
    if (!m_result || !run || !run->closing || run->closing->state != Run::Closing::State::Running) {
        return;
    }
    const auto& op = m_state.operation();
    m_result->setProgress(static_cast<float>(op ? std::clamp(op->fraction, 0.0, 1.0) : 0.0),
                          m_strings.format(Str::ApplyPct, {{L"pct", std::to_wstring(op ? static_cast<int>(op->fraction * 100) : 0)}}));
}

ResultCard::Texts ApplyPage::resultTexts(const RunSummary& s) const {
    const auto& run = *m_state.applyRun();
    ResultCard::Texts t;
    switch (s.outcome) {
    case Outcome::Succeeded:
        t.icon = ui::icons::Icon::SuccessCircle;
        t.ink = Color::StatusSuccess;
        t.title = m_strings.get(Str::ApplyDoneTitle);
        // D-141: a virtual disk was merged and detached — said as such, not "saved into the WIM".
        t.body = m_strings.get(virtualDiskSource() ? Str::ApplyOutcomeVhdSaved : Str::ApplyOutcomeSuccess);
        break;
    case Outcome::Skipped:
        t.icon = ui::icons::Icon::WarningTriangle;
        t.ink = Color::StatusWarning;
        t.title = m_strings.format(Str::ApplySkippedTitle, {{L"n", std::to_wstring(s.skipped)}});
        t.body = m_strings.get(Str::ApplySkippedBody);
        break;
    case Outcome::Held:
        t.icon = ui::icons::Icon::WarningTriangle;
        t.ink = Color::StatusWarning;
        t.title = m_strings.format(Str::ApplyHeldTitle, {{L"n", std::to_wstring(s.skipped)}});
        t.body = m_strings.get(Str::ApplyHeldBody);
        break;
    case Outcome::Kept:
        t.icon = ui::icons::Icon::WarningTriangle;
        t.ink = Color::StatusWarning;
        t.title = m_strings.get(Str::ApplyKeptTitle);
        t.body = m_strings.get(Str::ApplyKeptBody);
        break;
    case Outcome::Stopped:
        t.icon = ui::icons::Icon::Stop;
        t.ink = Color::StatusWarning;
        t.title = m_strings.get(Str::ApplyStoppedTitle);
        t.body = m_strings.get(Str::ApplyStoppedBody);
        if (run.result && run.result->report.sessionLost) {
            // D-131: not a cancel — DISM's session would not open again; say so.
            t.body = m_strings.format(Str::ApplySessionLost, {{L"e", run.result->report.sessionLost->message}}) + L" " + t.body;
        }
        break;
    case Outcome::CommitFailed:
        t.icon = ui::icons::Icon::ErrorOctagon;
        t.ink = Color::StatusError;
        t.title = m_strings.get(Str::ApplyFailedTitle);
        t.body = m_strings.format(Str::ApplyCommitFailedBody, {{L"e", run.result->commitError->message}});
        break;
    case Outcome::StartFailed:
    case Outcome::Running:
    case Outcome::Paused:
    case Outcome::Committing:
        t.icon = ui::icons::Icon::ErrorOctagon;
        t.ink = Color::StatusError;
        t.title = m_strings.get(Str::ApplyFailedTitle);
        if (run.error) {
            t.body = run.error->message + (run.error->context.empty() ? L"" : L" — " + run.error->context);
        }
        break;
    }
    using Close = RunSummary::Close;
    switch (s.close) {
    case Close::None: break;
    case Close::Saving:
    case Close::Discarding: {
        const bool saving = s.close == Close::Saving;
        t.icon = saving ? ui::icons::Icon::Save : ui::icons::Icon::Unmount;
        t.ink = Color::AccentText;
        t.title = m_strings.get(saving ? Str::ApplyCloseSavingTitle : Str::ApplyCloseDiscardingTitle);
        t.body = m_strings.get(saving ? Str::ApplyCloseSavingBody : Str::ApplyCloseDiscardingBody);
        const auto& op = m_state.operation();
        t.progress = static_cast<float>(op ? std::clamp(op->fraction, 0.0, 1.0) : 0.0);
        break;
    }
    case Close::Saved:
        t.icon = ui::icons::Icon::SuccessCircle;
        t.ink = Color::StatusSuccess;
        t.title = m_strings.get(Str::ApplyCloseSavedTitle);
        t.body = m_strings.get(virtualDiskSource() ? Str::ApplyOutcomeVhdSaved : Str::ApplyCloseSavedBody);
        break;
    case Close::Discarded:
        t.icon = ui::icons::Icon::Unmount;
        t.ink = Color::TextSecondary;
        t.title = m_strings.get(Str::ApplyCloseDiscardedTitle);
        t.body = m_strings.get(Str::ApplyCloseDiscardedBody);
        break;
    case Close::SaveFailed:
    case Close::DiscardFailed: {
        t.icon = ui::icons::Icon::ErrorOctagon;
        t.ink = Color::StatusError;
        t.title = m_strings.get(s.close == Close::SaveFailed ? Str::ApplyCloseSaveFailedTitle : Str::ApplyCloseDiscardFailedTitle);
        const std::wstring reason = run.closing && run.closing->error ? run.closing->error->message : std::wstring();
        t.body = m_state.mounted() ? m_strings.format(Str::ApplyCloseFailedMounted, {{L"e", reason}}) : reason;
        break;
    }
    }
    if (!run.extras.empty()) {
        std::wstring line;
        for (const auto& e : run.extras) {
            using State = AppState::ApplyRun::ExtraEdition::State;
            std::wstring outcome = e.state == State::Done
                                       ? (e.failures ? m_strings.format(Str::ApplyEditionSkipped, {{L"n", std::to_wstring(e.failures)}})
                                                     : m_strings.get(Str::ApplyEditionOk))
                                       : m_strings.get(Str::ApplyEditionFailed) + (e.error.empty() ? L"" : L" (" + e.error + L")");
            line += (line.empty() ? L"" : L" · ") + e.name + L": " + outcome;
        }
        t.body += (t.body.empty() ? L"" : L" ") + m_strings.get(Str::ApplyOtherEditionsDone) + L" " + line;
    }
    t.time = m_strings.format(Str::ApplyRunTook, {{L"t", formatDuration(s.elapsedSeconds, m_language)}});
    t.iso = isoTexts(); // D-144
    t.sizeLabel = m_strings.get(Str::OverviewSize);
    t.before = s.sizeBefore;
    t.after = s.sizeAfter;
    const std::wstring none = m_strings.get(Str::CommonNone);
    // The size after is read from the WIM once it is saved; an unsaved run has only the one before.
    t.sizeValue = s.sizeAfter ? (s.sizeBefore ? formatBytes(s.sizeBefore, m_language) : none) + L" → " +
                                    formatBytes(s.sizeAfter, m_language)
                              : (s.sizeBefore ? formatBytes(s.sizeBefore, m_language) : none);
    t.gainLabel = m_strings.get(Str::ApplyGain);
    if (s.gain > 0) {
        t.gainValue = m_strings.format(Str::ApplyGainValue, {{L"size", formatBytes(s.gain, m_language)}, {L"pct", std::to_wstring(s.gainPct)}});
        t.gainInk = Color::StatusSuccess;
    } else {
        t.gainValue = none;
        t.gainInk = Color::TextSecondary;
    }
    t.stepsLabel = m_strings.get(Str::ApplyOutcomeSteps);
    t.counts = dotCounts(s);
    return t;
}

std::vector<ResultCard::Action> ApplyPage::resultActions(const RunSummary& s) const {
    using ui::icons::Icon;
    std::vector<ResultCard::Action> actions;
    auto add = [&](Str label, std::optional<Icon> icon, const std::function<void()>& run, bool primary) {
        if (run) {
            actions.push_back({m_strings.get(label), icon, run, primary});
        }
    };
    if (s.closing()) {
        // The save / discard runs: nothing to press but the log.
        add(Str::ApplySaveLog, Icon::LogTerminal, m_intents.saveLog, false);
        return actions;
    }
    if (s.discarded()) {
        // Unmounted unsaved: the editions, to mount again; the run's report and log stay at hand.
        add(Str::CommonGoImages, Icon::LayersEditions, m_intents.goImages, true);
        add(Str::ApplySaveReport, Icon::File, m_intents.saveReport, false);
        add(Str::ApplySaveLog, Icon::LogTerminal, m_intents.saveLog, false);
        return actions;
    }
    if (s.saved() && virtualDiskSource()) {
        // D-141: the disk is the result (no setup media from it): the report and the preset.
        add(Str::ApplySaveReport, Icon::File, m_intents.saveReport, true);
        add(Str::ApplySaveToPreset, Icon::PresetBookmark, m_intents.saveToPreset, false);
        add(Str::ApplySaveLog, Icon::LogTerminal, m_intents.saveLog, false);
    } else if (s.saved()) {
        // The image is written: the setup media next; the report and the preset for later. D-144: while
        // the ISO is made after the run (or once it is made) its band has the main button.
        const bool isoOwnsIt = s.iso == RunSummary::Iso::Running || s.iso == RunSummary::Iso::Done;
        add(Str::NavIso, Icon::IsoBuild, m_intents.buildIso, !isoOwnsIt);
        add(Str::ApplySaveReport, Icon::File, m_intents.saveReport, false);
        add(Str::ApplySaveToPreset, Icon::PresetBookmark, m_intents.saveToPreset, false);
    } else if (s.waiting()) {
        // Mounted and unsaved: save what ran or let it go (the unmount dialog asks which).
        if (m_state.mounted()) {
            add(Str::ApplyHeldUnmount, Icon::Save, m_intents.unmount, true);
        }
        add(Str::ApplySaveReport, Icon::File, m_intents.saveReport, false);
        add(Str::ApplySaveLog, Icon::LogTerminal, m_intents.saveLog, false);
    } else {
        add(Str::CommonGoImages, Icon::LayersEditions, m_intents.goImages, true);
        add(Str::ApplySaveLog, Icon::LogTerminal, m_intents.saveLog, false);
    }
    return actions;
}

void ApplyPage::buildDone() {
    const auto& run = *m_state.applyRun();
    m_groups = run.groups;
    // D-138: the result card replaces the result bar and the counter row.
    const RunSummary s = summarizeRun(run, ui::nowMs());
    m_result = &add<ResultCard>(resultTexts(s), resultActions(s), isoActions(s));
    refreshClosing();

    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 28},
        {m_strings.get(Str::ApplyOperation), 0},
        {m_strings.get(Str::ApplyResult), 230, ui::TextAlign::Leading, false, 150},
        {m_strings.get(Str::ApplyDuration), 126, ui::TextAlign::Trailing, false, 90},
    });
    m_table->setFlexMin(kNameColumnMin);
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintDoneCell(c, row, column, rect);
    };
    m_doneRowCache = doneRows();
    m_skipTexts.assign(m_doneRowCache.size(), StepText{});
    m_table->setRowCount(static_cast<int>(m_doneRowCache.size()));
}

const std::wstring& ApplyPage::activeName(std::size_t step, const core::ops::Operation& op) {
    if (const std::uint64_t revision = m_state.revision(); m_activeStep != step || m_activeAt != revision) {
        m_activeStep = step;
        m_activeAt = revision;
        m_activeName = displayName(m_state, op);
    }
    return m_activeName;
}

void ApplyPage::poll() {
    if (!m_log) {
        return;
    }
    const auto buffer = m_state.logBuffer();
    if (!buffer) {
        return;
    }
    auto fresh = buffer->since(m_logVersion);
    std::vector<ui::LogLine> lines;
    for (const auto& e : fresh) {
        lines.push_back(LogsPage::toLine(e)); // the same line as on the Logs page
    }
    m_log->append(lines);
    // D-138: the elapsed / remaining time goes on between the run's own notifications (once a second).
    if (m_runHeader) {
        if (const auto& run = m_state.applyRun(); run && run->stage == Run::Stage::Running) {
            const RunSummary s = summarizeRun(*run, ui::nowMs());
            if (const auto second = static_cast<std::int64_t>(s.elapsedSeconds); second != m_runSecond) {
                m_runSecond = second;
                m_runHeader->setLine(runLine(s));
            }
        }
    }
}

// ---- cells --------------------------------------------------------------------------------

void ApplyPage::paintSummaryCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rowCache.size()) || m_planCells.size() != m_rowCache.size()) {
        return;
    }
    const Row& r = m_rowCache[static_cast<std::size_t>(row)];
    const PlanCells& cells = m_planCells[static_cast<std::size_t>(row)];
    switch (column) {
    case 0: canvas.drawText(cells.order, rect, TypeStyle::Body, Color::TextTertiary); break;
    case 1: {
        canvas.drawIcon(groupIcon(r), {rect.x, rect.y + (rect.height - 16) / 2}, Color::AccentBase);
        const float x = rect.x + ui::tokens::size::icon + 8;
        float right = rect.right();
        if (!cells.risky.empty()) {
            // "● 2 riskli" at the right of the name cell: the group holds high-risk steps.
            const float w = std::min(std::ceil(canvas.text().measure(cells.risky, TypeStyle::Small)) + 14, std::max(right - x, 0.0f));
            const RectF tag{right - w, rect.y, w, rect.height};
            canvas.fillEllipse({tag.x + 3, tag.y + tag.height / 2}, 3.0f, Color::StatusError);
            canvas.drawText(cells.risky, {tag.x + 11, tag.y, std::max(tag.width - 11, 0.0f), tag.height}, TypeStyle::Small,
                            Color::StatusError);
            right = tag.x - 8;
        }
        canvas.drawText(cells.name, {x, rect.y, std::max(right - x, 0.0f), rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case 2: canvas.drawText(cells.steps, rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing); break;
    case 3:
        canvas.drawText(cells.size, rect, TypeStyle::Mono, cells.saves ? Color::StatusSuccess : Color::TextSecondary,
                        ui::TextAlign::Trailing);
        break;
    case 4: canvas.drawText(cells.time, rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing); break;
    default: break;
    }
}

void ApplyPage::paintDoneCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    const auto& run = *m_state.applyRun();
    const auto& all = m_doneRowCache;
    if (row < 0 || row >= static_cast<int>(all.size()) || all.size() != m_skipTexts.size()) {
        return;
    }
    const Row& r = all[static_cast<std::size_t>(row)];
    if (r.failedStep >= 0) {
        // A skipped step: its name, then why, in the wide column.
        const auto& step = run.result->report.results[static_cast<std::size_t>(r.failedStep)];
        switch (column) {
        case 0: canvas.drawIcon(ui::icons::Icon::WarningTriangle, {rect.x, rect.y + (rect.height - 16) / 2}, Color::StatusWarning); break;
        case 1: {
            StepText& text = m_skipTexts[static_cast<std::size_t>(row)];
            if (const std::uint64_t revision = m_state.revision(); text.at != revision) {
                text.at = revision;
                text.name = displayName(m_state, step.step.operation);
                text.reason = skipReason(step.outcome.error());
            }
            const std::wstring& name = text.name;
            const float width = std::min(std::ceil(canvas.text().measure(name, TypeStyle::Body)), rect.width);
            canvas.drawText(name, {rect.x, rect.y, width, rect.height}, TypeStyle::Body, Color::TextPrimary);
            const float x = rect.x + width + 12;
            if (x < rect.right()) {
                canvas.drawText(text.reason, {x, rect.y, rect.right() - x, rect.height},
                                TypeStyle::Caption, Color::TextSecondary);
            }
            break;
        }
        case 2:
            canvas.drawText(m_strings.get(Str::ApplySkippedStep), {rect.x + ui::tokens::size::icon + 6, rect.y,
                                                                    rect.width - ui::tokens::size::icon - 6, rect.height},
                            TypeStyle::Caption, Color::StatusWarning);
            break;
        case 3:
            if (static_cast<std::size_t>(r.failedStep) < run.result->stepTimes.size()) {
                const double s = static_cast<double>(run.result->stepTimes[static_cast<std::size_t>(r.failedStep)].count()) / 1000.0;
                canvas.drawText(formatDuration(s, m_language), rect, TypeStyle::Mono, Color::TextTertiary,
                                ui::TextAlign::Trailing);
            }
            break;
        default: break;
        }
        return;
    }
    std::size_t ok = 0;
    std::size_t failed = 0;
    std::size_t total = 1;
    double seconds = 0;
    if (r.group) {
        total = r.group->count;
        for (std::size_t i = r.group->first; i < r.group->first + r.group->count; ++i) {
            ok += run.stepState[i] == 2 ? 1 : 0;
            failed += run.stepState[i] == 3 ? 1 : 0;
            if (run.result && i < run.result->stepTimes.size()) {
                seconds += static_cast<double>(run.result->stepTimes[i].count()) / 1000.0;
            }
        }
    } else if (run.result) {
        ok = run.result->committed ? 1 : 0;
        failed = run.result->commitError ? 1 : 0;
        seconds = static_cast<double>(run.result->commitTime.count()) / 1000.0;
    }
    const bool complete = ok == total;
    const ui::icons::Icon icon = complete ? ui::icons::Icon::SuccessCircle
                                 : failed ? ui::icons::Icon::WarningTriangle
                                          : ui::icons::Icon::QueueClock;
    const Color ink = complete ? Color::StatusSuccess : failed ? Color::StatusWarning : Color::TextTertiary;
    switch (column) {
    case 0: canvas.drawIcon(icon, {rect.x, rect.y + (rect.height - 16) / 2}, ink); break;
    case 1: {
        canvas.drawIcon(groupIcon(r), {rect.x, rect.y + (rect.height - 16) / 2}, Color::AccentBase);
        const float x = rect.x + ui::tokens::size::icon + 8;
        canvas.drawText(groupName(r), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case 2: {
        std::wstring text = std::format(L"{} / {}", ok, total);
        if (failed) {
            text += L" · " + m_strings.format(Str::ApplySkipped, {{L"n", std::to_wstring(failed)}});
        }
        canvas.drawText(text, rect, TypeStyle::Caption, failed ? Color::StatusWarning : Color::TextSecondary);
        break;
    }
    case 3:
        canvas.drawText(ok + failed ? formatDuration(seconds, m_language) : std::wstring(L"—"), rect, TypeStyle::Mono,
                        Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void ApplyPage::paintSteps(ui::Canvas& canvas, RectF area) {
    float y = area.y;
    for (const auto& line : m_stepLines) {
        if (y >= area.bottom()) {
            break;
        }
        const RectF row{area.x, y, area.width, kStepRow};
        const bool active = line.state == 1;
        const bool complete = line.state >= 2;
        if (active) {
            // The running group: the selection fill and a short accent bar, as a chosen nav item has.
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::AccentSubtle);
            canvas.fillRoundRect({row.x, row.y + 9, 3, row.height - 18}, 1.5f, Color::AccentBase);
        }
        const Color ink = active ? Color::AccentText
                          : complete ? (line.state == 3 ? Color::StatusWarning : Color::StatusSuccess)
                                     : Color::TextTertiary;
        if (active) {
            // D-142: the progress ring, where the line's 16 DIP icon is.
            ui::paintRing(canvas, ui::ringBox({row.x + 10 + ui::tokens::size::icon / 2, row.y + kStepRow / 2}, ui::tokens::size::ring),
                          m_stepRing.pose(m_ringNow), ink);
        } else {
            const ui::icons::Icon icon = complete ? (line.state == 3 ? ui::icons::Icon::WarningTriangle : ui::icons::Icon::SuccessCircle)
                                                  : ui::icons::Icon::QueueClock;
            canvas.drawIcon(icon, {row.x + 10, row.y + std::round((kStepRow - ui::tokens::size::icon) / 2)}, ink);
        }
        const float x = row.x + 10 + ui::tokens::size::icon + 10;
        canvas.drawText(line.name, {x, row.y + 4, row.right() - x - 8, 18}, active ? TypeStyle::BodySemi : TypeStyle::Body,
                        active || complete ? Color::TextPrimary : Color::TextSecondary);
        canvas.drawText(line.caption, {x, row.y + 22, row.right() - x - 8, 16}, TypeStyle::Caption, Color::TextTertiary);
        y += kStepRow + kStepGap;
    }
}

// ---- layout / paint -------------------------------------------------------------------------

void ApplyPage::layout() {
    const RectF b = bounds();
    if (m_empty) {
        m_empty->setBounds(b);
        return;
    }
    float y = b.y + kTop;
    if (live()) {
        // The head card keeps its height for the whole run (paused adds its band): nothing below moves.
        const float head = m_runHeader ? m_runHeader->preferredHeight() : 0.0f;
        if (m_runHeader) {
            m_runHeader->setBounds({b.x, y, b.width, head});
        }
        const float top = y + head + kCardGap + kListCaption;
        const float logX = b.x + kStepsWidth + 16;
        m_log->setBounds({logX, top, std::max(b.right() - logX, 0.0f), std::max(b.bottom() - top, 0.0f)});
        return;
    }
    if (m_result) {
        const float h = m_result->preferredHeight(b.width);
        m_result->setBounds({b.x, y, b.width, h});
        y += h + kCardGap;
    }
    if (m_mode == Mode::Summary) {
        if (m_planHeader) {
            m_planHeader->setBounds({b.x, y, b.width, PlanHeader::kHeight});
            y += PlanHeader::kHeight + kCardGap;
        }
        for (ui::InfoBar* bar : {m_waitBar, m_riskBar}) {
            if (bar && bar->visible()) {
                bar->setBounds({b.x, y, b.width, kInfoBar});
                y += kInfoBar + kGap;
            }
        }
        if (m_impact) {
            const float h = m_impact->preferredHeight();
            m_impact->setBounds({b.x, y, b.width, h});
            y += h + kGap;
        }
        if (!m_editions.empty()) {
            float x = b.x + kEditionsLabel;
            float rowY = y;
            for (auto* check : m_editions) {
                const ui::SizeF size = check->measure({});
                if (x + size.width > b.right() && x > b.x + kEditionsLabel) {
                    x = b.x + kEditionsLabel;
                    rowY += kEditionsRow;
                }
                check->setBounds({x, rowY + (kEditionsRow - size.height) / 2, size.width, size.height});
                x += size.width + 16;
            }
            y = rowY + kEditionsRow + kGap;
        }
        for (ui::CheckField* check : {m_pause, m_guard, m_iso}) {
            if (check) {
                const ui::SizeF size = check->measure({});
                check->setBounds({b.x + kEditionsLabel, y + (kEditionsRow - size.height) / 2, size.width, size.height});
                y += kEditionsRow + kGap;
            }
        }
        if (m_iso && m_isoChange && m_isoChange->visible()) {
            // D-144: "Değiştir…" and "ISO seçenekleri…" after the file's name; the options' line under it.
            const RectF box = m_iso->bounds();
            const float lineW = std::ceil(textWidth(m_isoLine, TypeStyle::Caption));
            const ui::SizeF size = m_isoChange->measure({});
            const ui::SizeF options = m_isoOptions->measure({});
            const float both = size.width + 4 + options.width;
            const float x = std::min(box.right() + 16 + lineW + 8, std::max(b.right() - both, box.right() + 16));
            m_isoChange->setBounds({x, box.y + (box.height - size.height) / 2, size.width, size.height});
            m_isoOptions->setBounds({x + size.width + 4, box.y + (box.height - options.height) / 2, options.width, options.height});
            if (!m_isoSummary.empty()) {
                y += kIsoSummaryLine;
            }
        }
    }
    if (m_table) {
        m_table->setBounds({b.x, y + 4, b.width, std::max(b.bottom() - y - 4, 0.0f)});
    }
}

void ApplyPage::syncWaitBar() {
    std::uint64_t done = 0;
    std::uint64_t total = 0;
    if (const auto& f = m_state.updateFetch()) {
        done += f->doneBytes;
        total += f->totalBytes;
    }
    if (const auto& f = m_state.languageFetch()) {
        done += f->doneBytes;
        total += f->totalBytes;
    }
    if (const auto& f = m_state.storeFetch()) {
        done += f->doneBytes;
        total += f->totalBytes;
    }
    const bool waiting = m_controller.waitingForDownload();
    if (!waiting) {
        m_waitDismissed = false;
    }
    const bool show = waiting && !m_waitDismissed;
    const int pct = total > 0 ? static_cast<int>(std::min<std::uint64_t>(done * 100 / total, 100)) : 0;
    m_waitBar->set(ui::InfoKind::Warning, m_strings.format(Str::ApplyWaitDownloadTitle, {{L"pct", std::to_wstring(pct)}}),
                   m_strings.get(Str::ApplyWaitDownloadBody));
    if (show != m_waitBar->visible()) {
        m_waitBar->setVisible(show);
        layout();
    }
    invalidate();
}

void ApplyPage::paint(ui::Canvas& canvas) {
    if (m_mode == Mode::Summary) {
        const RectF b = bounds();
        if (!m_editions.empty()) {
            const RectF first = m_editions.front()->bounds();
            canvas.drawText(m_strings.get(Str::ApplyOtherEditions),
                            {b.x, first.y - (kEditionsRow - first.height) / 2, kEditionsLabel - 8, kEditionsRow},
                            TypeStyle::Body, Color::TextSecondary);
        }
        if (m_pause) {
            const RectF box = m_pause->bounds();
            canvas.drawText(m_strings.get(Str::ApplyBeforeSave),
                            {b.x, box.y - (kEditionsRow - box.height) / 2, kEditionsLabel - 8, kEditionsRow},
                            TypeStyle::Body, Color::TextSecondary);
        }
        if (m_guard) {
            const RectF box = m_guard->bounds();
            const float rowY = box.y - (kEditionsRow - box.height) / 2;
            canvas.drawText(m_strings.get(Str::ApplyAfterSetup), {b.x, rowY, kEditionsLabel - 8, kEditionsRow}, TypeStyle::Body,
                            Color::TextSecondary);
            // What it does, after the box (the tooltip says it all).
            const float hintX = box.right() + 16;
            canvas.drawText(m_strings.get(Str::ApplyGuardShort), {hintX, rowY, std::max(b.right() - hintX, 0.0f), kEditionsRow},
                            TypeStyle::Caption, Color::TextTertiary);
        }
        if (m_iso) {
            // D-144: "Bitince" · [x] ISO oluştur · the file it writes (or why it cannot) · Değiştir…
            const RectF box = m_iso->bounds();
            const float rowY = box.y - (kEditionsRow - box.height) / 2;
            canvas.drawText(m_strings.get(Str::ApplyAutoIsoLabel), {b.x, rowY, kEditionsLabel - 8, kEditionsRow}, TypeStyle::Body,
                            Color::TextSecondary);
            const float lineX = box.right() + 16;
            const float lineRight = m_isoChange && m_isoChange->visible() ? m_isoChange->bounds().x - 8 : b.right();
            canvas.drawText(m_isoLine, {lineX, rowY, std::max(lineRight - lineX, 0.0f), kEditionsRow}, TypeStyle::Caption,
                            m_isoAvailable ? Color::TextTertiary : Color::StatusWarning);
            if (!m_isoSummary.empty()) {
                // D-144 addendum: what the ISO is built with ("UEFI + BIOS · etiket … · SHA-256").
                canvas.drawText(m_isoSummary, {lineX, rowY + kEditionsRow - 4, std::max(b.right() - lineX, 0.0f), kIsoSummaryLine + 4},
                                TypeStyle::Caption, Color::TextSecondary);
            }
        }
        return;
    }
    if (!live() || !m_runHeader) {
        return;
    }
    const RectF b = bounds();
    const float top = b.y + kTop + m_runHeader->preferredHeight() + kCardGap;
    canvas.drawText(m_strings.get(Str::ApplyStepsTitle), {b.x, top, kStepsWidth, 16}, TypeStyle::Caption, Color::TextSecondary);
    paintSteps(canvas, {b.x, top + kListCaption, kStepsWidth, b.bottom() - top - kListCaption});
    const float logX = b.x + kStepsWidth + 16;
    canvas.drawText(m_strings.get(Str::ApplyLiveLog), {logX, top, 200, 16}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_strings.get(Str::ApplyAutoScroll) + L" · " +
                        m_strings.get(m_log->autoScroll() ? Str::ApplyOn : Str::ApplyOff),
                    {logX, top, b.right() - logX, 16}, TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);
}

} // namespace wl::app
