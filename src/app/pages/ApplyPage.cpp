#include "app/pages/ApplyPage.h"

#include "app/Format.h"
#include "base/Utf8.h"
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

namespace {

constexpr float kTop = 12.0f;
constexpr float kGap = 8.0f;
constexpr float kInfoBar = 32.0f;
constexpr float kStepsWidth = 420.0f;
constexpr float kStepRow = 32.0f;
constexpr float kProgress = 2.0f;

// Counter categories of screen 13.
enum Category : int { kComponents, kFeatures, kUpdates, kDrivers, kRegistry, kServices, kTweaks, kCategories };

Category categoryOf(OpKind kind) {
    switch (kind) {
    case OpKind::RemovePackage:
    case OpKind::RemoveAppx: return kComponents;
    case OpKind::EnableFeature:
    case OpKind::DisableFeature:
    case OpKind::RemoveCapability: return kFeatures;
    case OpKind::AddPackage: return kUpdates;
    case OpKind::AddDriver: return kDrivers;
    case OpKind::SetRegistryValue: return kRegistry;
    case OpKind::SetServiceStart: return kServices;
    }
    return kTweaks;
}

ui::LogLine toLine(const log::Entry& e) {
    ui::LogLevel level = ui::LogLevel::Info;
    if (e.level == log::Level::Warn) {
        level = ui::LogLevel::Warn;
    } else if (e.level == log::Level::Error) {
        level = ui::LogLevel::Error;
    } else if (e.level == log::Level::Debug || e.level == log::Level::Trace) {
        level = ui::LogLevel::Debug;
    }
    const auto local = std::chrono::zoned_time(std::chrono::current_zone(), e.time).get_local_time();
    return {std::format(L"{:%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(local)), level,
            utf8::toWide(e.source), e.message};
}

} // namespace

ApplyPage::Mode ApplyPage::modeFor(const AppState& state) {
    const auto& run = state.applyRun();
    if (run && run->stage != Run::Stage::Done) {
        return Mode::Running;
    }
    if (state.mounted() && !state.changes().empty()) {
        return Mode::Summary;
    }
    if (run) {
        return Mode::Done;
    }
    return state.mounted() ? Mode::Empty : Mode::NoMount;
}

std::wstring ApplyPage::displayName(const AppState& state, const core::ops::Operation& op) {
    if (const auto& features = state.optionalFeatures()) {
        for (const auto& item : features->items) {
            if (item.name == op.target) {
                return item.displayName;
            }
        }
    }
    return op.target;
}

ApplyPage::ApplyPage(AppState& state, ApplyController& controller, const Localization& strings, Language language,
                     Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)),
      m_mode(modeFor(state)) {
    setAccessible(ui::AccessRole::Group, strings.get(Str::ApplyTitle));
    switch (m_mode) {
    case Mode::Summary: buildSummary(); break;
    case Mode::Running: buildRunning(); break;
    case Mode::Done: buildDone(); break;
    default: buildEmpty(); break;
    }
    // Live repaint while running; mode switches are handled by the Shell (it rebuilds the page).
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Apply && m_mode == Mode::Running) {
            invalidate();
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

std::wstring ApplyPage::groupName(const Row& row) const {
    if (!row.group) {
        return m_strings.get(Str::ApplyOpsCommitUnmount);
    }
    switch (row.group->phase) {
    case Phase::Remove: return m_strings.get(Str::ApplyOpsComponents);
    case Phase::Features: return m_strings.get(Str::ApplyOpsFeatures);
    case Phase::Drivers: return m_strings.get(Str::ApplyOpsDrivers);
    case Phase::Updates: return m_strings.get(Str::ApplyOpsUpdates);
    case Phase::Settings: return m_strings.get(Str::ApplyOpsSettings);
    }
    return {};
}

ui::icons::Icon ApplyPage::groupIcon(const Row& row) {
    if (!row.group) {
        return ui::icons::Icon::ImageWim;
    }
    switch (row.group->phase) {
    case Phase::Remove: return ui::icons::Icon::ComponentsRemove;
    case Phase::Features: return ui::icons::Icon::PuzzleFeatures;
    case Phase::Drivers: return ui::icons::Icon::DriverChip;
    case Phase::Updates: return ui::icons::Icon::UpdateDownload;
    case Phase::Settings: return ui::icons::Icon::Registry;
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
    case Mode::Done: {
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
        return {m_strings.get(Str::ApplyDoneTitle),
                m_strings.format(Str::ApplyDoneDesc,
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
        m_empty->setAction(m_strings.get(Str::ApplyGoImages)).onInvoke = m_intents.goImages;
    } else {
        m_empty = &add<ui::EmptyState>(ui::icons::Icon::ApplyPlay, m_strings.get(Str::ApplyEmptyTitle),
                                       m_strings.get(Str::ApplyEmptyBody));
        m_empty->setAction(m_strings.get(Str::ApplyGoFeatures)).onInvoke = m_intents.goFeatures;
    }
}

void ApplyPage::buildSummary() {
    m_plan = m_controller.currentPlan();
    m_groups = core::ops::groups(m_plan);

    // Counters: count + size effect per category.
    std::array<int, kCategories> counts{};
    std::array<std::int64_t, kCategories> sizes{};
    for (const auto& op : m_state.changes().operations()) {
        ++counts[categoryOf(op.kind)];
        sizes[categoryOf(op.kind)] += op.sizeDelta;
    }
    const std::array<Str, kCategories> labels{Str::ApplyStatComponents, Str::ApplyStatFeatures, Str::ApplyStatUpdates,
                                              Str::ApplyStatDrivers,    Str::ApplyStatRegistry, Str::ApplyStatServices,
                                              Str::ApplyStatTweaks};
    std::vector<StatStrip::Stat> stats;
    for (int i = 0; i < kCategories; ++i) {
        StatStrip::Stat s{m_strings.get(labels[static_cast<std::size_t>(i)]), std::to_wstring(counts[static_cast<std::size_t>(i)]),
                          L"–"};
        const auto delta = sizes[static_cast<std::size_t>(i)];
        if (delta < 0) {
            s.detail = formatBytes(static_cast<std::uint64_t>(-delta), m_language);
            s.detailInk = Color::StatusSuccess;
        } else if (delta > 0) {
            s.detail = L"+" + formatBytes(static_cast<std::uint64_t>(delta), m_language);
        }
        stats.push_back(std::move(s));
    }
    m_stats = &add<StatStrip>();
    m_stats->setStats(std::move(stats));

    const auto risky = m_controller.highRisk();
    if (!risky.empty()) {
        std::wstring items;
        for (std::size_t i = 0; i < risky.size() && i < 3; ++i) {
            items += (i ? L", " : L"") + displayName(m_state, risky[i]);
        }
        if (risky.size() > 3) {
            items += L" …";
        }
        m_riskBar = &add<ui::InfoBar>(ui::InfoKind::Warning,
                                      m_strings.format(Str::ApplyHighRiskN, {{L"n", std::to_wstring(risky.size())}}),
                                      m_strings.format(Str::ApplyHighRiskBody, {{L"items", items}}),
                                      m_strings.get(Str::CommonClose));
        m_riskBar->setAction(m_strings.get(Str::ApplyReview), m_intents.review);
        m_riskBar->onClose = [this] {
            m_riskBar->setVisible(false);
            layout();
        };
    }
    double seconds = core::ops::kCommitSeconds;
    for (const auto& g : m_groups) {
        seconds += g.estimateSeconds;
    }
    const std::wstring range = formatDuration(seconds * 0.7, m_language, true) + L"–" +
                               formatDuration(seconds * 1.4, m_language, true).substr(1);
    m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Info, m_strings.format(Str::ApplyEtaTitle, {{L"t", range}}),
                                  m_strings.get(Str::ApplyEtaBody), m_strings.get(Str::CommonClose));
    m_infoBar->onClose = [this] {
        m_infoBar->setVisible(false);
        layout();
    };

    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {m_strings.get(Str::ApplyOrder), 48},
        {m_strings.get(Str::ApplyOperation), 0},
        {m_strings.get(Str::ApplySteps), 100, ui::TextAlign::Trailing},
        {m_strings.get(Str::ApplyEta), 110, ui::TextAlign::Trailing},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintSummaryCell(c, row, column, rect);
    };
    m_table->setRowCount(static_cast<int>(rows().size()));
}

void ApplyPage::buildRunning() {
    const auto& run = m_state.applyRun();
    m_groups = run->groups;
    m_logVersion = run->logVersion;
    m_log = &add<ui::LogConsole>();
    m_log->newLinesText = [this](std::size_t n) { return m_strings.format(Str::LogsNewLines, {{L"n", std::to_wstring(n)}}); };
    poll();
}

void ApplyPage::buildDone() {
    const auto& run = *m_state.applyRun();
    m_groups = run.groups;
    // Result InfoBar.
    if (run.result) {
        const auto& r = *run.result;
        const std::size_t failures = r.report.failures();
        if (r.commitError) {
            m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Error, m_strings.get(Str::ApplyFailedTitle),
                                          m_strings.format(Str::ApplyCommitFailedBody, {{L"e", r.commitError->message}}),
                                          m_strings.get(Str::CommonClose));
        } else if (!r.report.completed) {
            m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Warning, m_strings.get(Str::ApplyStoppedTitle),
                                          m_strings.get(Str::ApplyStoppedBody), m_strings.get(Str::CommonClose));
        } else if (failures > 0) {
            m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Warning,
                                          m_strings.format(Str::ApplySkippedTitle, {{L"n", std::to_wstring(failures)}}),
                                          m_strings.get(Str::ApplySkippedBody), m_strings.get(Str::CommonClose));
        } else {
            const std::wstring after = run.sizeAfter ? formatBytes(run.sizeAfter, m_language) : std::wstring(L"—");
            const std::uint64_t gain = run.sizeBefore > run.sizeAfter && run.sizeAfter ? run.sizeBefore - run.sizeAfter : 0;
            m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Success, m_strings.get(Str::ApplySuccess),
                                          m_strings.format(Str::ApplySuccessBody, {{L"after", after},
                                                                                   {L"gain", formatBytes(gain, m_language)},
                                                                                   {L"w", L"0"}}),
                                          m_strings.get(Str::CommonClose));
        }
    } else if (run.error) {
        m_infoBar = &add<ui::InfoBar>(ui::InfoKind::Error, m_strings.get(Str::ApplyFailedTitle),
                                      run.error->message + L" — " + run.error->context, m_strings.get(Str::CommonClose));
    }
    if (m_infoBar) {
        m_infoBar->onClose = [this] {
            m_infoBar->setVisible(false);
            layout();
        };
    }
    // Counters: before, after, gain, duration, warnings, errors.
    std::vector<StatStrip::Stat> stats;
    const bool haveAfter = run.sizeAfter > 0;
    stats.push_back({m_strings.get(Str::ApplyBefore), run.sizeBefore ? formatBytes(run.sizeBefore, m_language) : L"—"});
    stats.push_back({m_strings.get(Str::ApplyAfter), haveAfter ? formatBytes(run.sizeAfter, m_language) : L"—"});
    if (haveAfter && run.sizeBefore > run.sizeAfter) {
        const auto gain = run.sizeBefore - run.sizeAfter;
        const int pct = static_cast<int>(std::lround(100.0 * static_cast<double>(gain) / static_cast<double>(run.sizeBefore)));
        stats.push_back({m_strings.get(Str::ApplyGain), formatBytes(gain, m_language) + std::format(L" · %{}", pct),
                         {}, Color::StatusSuccess});
    } else {
        stats.push_back({m_strings.get(Str::ApplyGain), L"—"});
    }
    const double seconds = run.result ? static_cast<double>(run.result->elapsed.count()) / 1000.0 : 0.0;
    stats.push_back({m_strings.get(Str::ApplyDuration), formatDuration(seconds, m_language)});
    const std::size_t failures = run.result ? run.result->report.failures() : 0;
    stats.push_back({m_strings.get(Str::ApplyWarnings), std::to_wstring(failures), {},
                     failures ? Color::StatusWarning : Color::TextPrimary});
    const int errors = (run.error ? 1 : 0) + (run.result && run.result->commitError ? 1 : 0);
    stats.push_back({m_strings.get(Str::ApplyErrors), std::to_wstring(errors), {},
                     errors ? Color::StatusError : Color::TextPrimary});
    m_stats = &add<StatStrip>();
    m_stats->setStats(std::move(stats));

    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 28},
        {m_strings.get(Str::ApplyOperation), 0},
        {m_strings.get(Str::ApplyResult), 200},
        {m_strings.get(Str::ApplyDuration), 110, ui::TextAlign::Trailing},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintDoneCell(c, row, column, rect);
    };
    m_table->setRowCount(static_cast<int>(rows().size()));
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
        lines.push_back(toLine(e));
    }
    m_log->append(lines);
}

// ---- cells --------------------------------------------------------------------------------

void ApplyPage::paintSummaryCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    const auto all = rows();
    if (row < 0 || row >= static_cast<int>(all.size())) {
        return;
    }
    const Row& r = all[static_cast<std::size_t>(row)];
    const std::size_t steps = r.group ? r.group->count : 1;
    const double seconds = r.group ? r.group->estimateSeconds : core::ops::kCommitSeconds;
    switch (column) {
    case 0: canvas.drawText(std::to_wstring(row + 1), rect, TypeStyle::Body, Color::TextPrimary); break;
    case 1: {
        canvas.drawIcon(groupIcon(r), {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(groupName(r), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case 2:
        canvas.drawText(std::to_wstring(steps), rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    case 3:
        canvas.drawText(formatDuration(seconds, m_language, true), rect, TypeStyle::Mono, Color::TextPrimary,
                        ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void ApplyPage::paintDoneCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    const auto& run = *m_state.applyRun();
    const auto all = rows();
    if (row < 0 || row >= static_cast<int>(all.size())) {
        return;
    }
    const Row& r = all[static_cast<std::size_t>(row)];
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
    case 0: canvas.drawIcon(icon, {rect.x, rect.y + 4}, ink); break;
    case 1: {
        canvas.drawIcon(groupIcon(r), {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(groupName(r), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case 2: {
        std::wstring text = std::format(L"{} / {}", ok, total);
        if (failed) {
            text += L" · " + m_strings.format(Str::ApplySkipped, {{L"n", std::to_wstring(failed)}});
        }
        canvas.drawIcon(icon, {rect.x, rect.y + 4}, ink);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(text, {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Caption, Color::TextSecondary);
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
    const auto& run = *m_state.applyRun();
    const auto all = rows();
    float y = area.y;
    for (const auto& r : all) {
        std::size_t done = 0;
        std::size_t total = 1;
        bool active = false;
        bool failed = false;
        std::wstring current;
        if (r.group) {
            total = r.group->count;
            for (std::size_t i = r.group->first; i < r.group->first + r.group->count; ++i) {
                done += run.stepState[i] >= 2 ? 1 : 0;
                failed = failed || run.stepState[i] == 3;
                if (run.stepState[i] == 1) {
                    active = true;
                    current = displayName(m_state, run.plan.steps[i].operation);
                }
            }
        } else {
            active = run.stage == Run::Stage::Committing;
            done = run.result && run.result->committed ? 1 : 0;
        }
        const RectF row{area.x, y, area.width, kStepRow};
        if (active) {
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::AccentSubtle);
        }
        const bool complete = done == total && !active;
        const ui::icons::Icon icon = active     ? ui::icons::Icon::Spinner
                                     : complete ? (failed ? ui::icons::Icon::WarningTriangle : ui::icons::Icon::SuccessCircle)
                                                : ui::icons::Icon::QueueClock;
        const Color ink = active ? Color::AccentBase
                          : complete ? (failed ? Color::StatusWarning : Color::StatusSuccess)
                                     : Color::TextTertiary;
        canvas.drawIcon(icon, {row.x + 8, row.y + 8}, ink);
        const float x = row.x + 8 + ui::tokens::size::icon + 8;
        canvas.drawText(groupName(r), {x, row.y + 1, row.right() - x - 8, 16},
                        active ? TypeStyle::BodyStrong : TypeStyle::Body,
                        active || complete ? Color::TextPrimary : Color::TextSecondary);
        std::wstring caption = active || complete ? std::format(L"{} / {}", done, total)
                                                  : m_strings.format(Str::ApplyStepsN, {{L"n", std::to_wstring(total)}});
        if (active && !current.empty()) {
            caption += L" · " + current;
        }
        canvas.drawText(caption, {x, row.y + 16, row.right() - x - 8, 14}, TypeStyle::Caption, Color::TextTertiary);
        y += kStepRow;
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
    if (m_mode == Mode::Running) {
        const float top = y + kProgress + 16;
        const float logX = b.x + kStepsWidth + 16;
        m_log->setBounds({logX, top + 20, std::max(b.right() - logX, 0.0f), std::max(b.bottom() - top - 20, 0.0f)});
        return;
    }
    if (m_mode == Mode::Done && m_infoBar && m_infoBar->visible()) {
        m_infoBar->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + kGap + 4;
    }
    if (m_stats) {
        m_stats->setBounds({b.x, y, b.width, StatStrip::kHeight});
        y += StatStrip::kHeight + kGap;
    }
    if (m_mode == Mode::Summary) {
        for (ui::InfoBar* bar : {m_riskBar, m_infoBar}) {
            if (bar && bar->visible()) {
                bar->setBounds({b.x, y, b.width, kInfoBar});
                y += kInfoBar + kGap;
            }
        }
    }
    if (m_table) {
        m_table->setBounds({b.x, y + 4, b.width, std::max(b.bottom() - y - 4, 0.0f)});
    }
}

void ApplyPage::paint(ui::Canvas& canvas) {
    if (m_mode != Mode::Running) {
        return;
    }
    const RectF b = bounds();
    const auto& run = *m_state.applyRun();
    const float y = b.y + kTop;
    canvas.progressBar({b.x, y, b.width, kProgress}, static_cast<float>(std::clamp(run.fraction, 0.0, 1.0)));
    const float top = y + kProgress + 16;
    paintSteps(canvas, {b.x, top, kStepsWidth, b.bottom() - top});
    const float logX = b.x + kStepsWidth + 16;
    canvas.drawText(m_strings.get(Str::ApplyLiveLog), {logX, top, 200, 16}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_strings.get(Str::ApplyAutoScroll) + L" · " +
                        m_strings.get(m_log->autoScroll() ? Str::ApplyOn : Str::ApplyOff),
                    {logX, top, b.right() - logX, 16}, TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);
}

} // namespace wl::app
