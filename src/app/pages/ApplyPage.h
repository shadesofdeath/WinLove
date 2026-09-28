#pragma once
// P05 Uygula (docs/pages/05-apply.md, screens 13 / 13b / 14 / 15). One page, four modes chosen
// from AppState (modeFor): Summary (queue → counters, risk + time InfoBars, ordered steps),
// Running (2px progress, step list, live log), Done (result InfoBar, counters, per-group
// results) and Empty. The header actions belong to the Shell and follow the mode.
#include "app/Localization.h"
#include "app/controllers/ApplyController.h"
#include "app/pages/apply/StatStrip.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/LogConsole.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class ApplyPage : public ui::Widget {
public:
    enum class Mode : std::uint8_t { Empty, NoMount, Summary, Running, Done };
    [[nodiscard]] static Mode modeFor(const AppState& state);

    struct Intents {
        std::function<void()> review;     // "İncele" on the risk InfoBar (opens the confirm dialog)
        std::function<void()> goImages;   // EmptyState action
        std::function<void()> goFeatures;
    };

    ApplyPage(AppState& state, ApplyController& controller, const Localization& strings, Language language,
              Intents intents);
    ~ApplyPage() override;

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    void poll(); // Running: pull new log lines (Shell timer)

    // Header texts for the Shell (title, description) in the current mode.
    [[nodiscard]] std::pair<std::wstring, std::wstring> header() const;
    // Human name of a queued operation ("OpenSSH İstemcisi" instead of the DISM name).
    [[nodiscard]] static std::wstring displayName(const AppState& state, const core::ops::Operation& op);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    struct Row { // one line of the step tables: a plan group or the final commit
        std::optional<core::ops::PlanGroup> group;
    };
    void buildSummary();
    void buildRunning();
    void buildDone();
    void buildEmpty();
    [[nodiscard]] std::vector<Row> rows() const;
    [[nodiscard]] std::wstring groupName(const Row& row) const;
    [[nodiscard]] static ui::icons::Icon groupIcon(const Row& row);
    void paintSteps(ui::Canvas& canvas, ui::RectF area);
    void paintSummaryCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);
    void paintDoneCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    ApplyController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    Mode m_mode;
    std::size_t m_subscription = 0;
    core::ops::ApplyPlan m_plan;                    // Summary
    std::vector<core::ops::PlanGroup> m_groups;     // Summary / Running / Done
    std::uint64_t m_logVersion = 0;

    StatStrip* m_stats = nullptr;
    ui::InfoBar* m_riskBar = nullptr;
    ui::InfoBar* m_infoBar = nullptr;
    ui::TableView* m_table = nullptr;
    ui::LogConsole* m_log = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
