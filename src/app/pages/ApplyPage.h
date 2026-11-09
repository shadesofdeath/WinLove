#pragma once
// P05 Uygula (docs/pages/05-apply.md, screens 13 / 13b / 14 / 15). One page, four modes chosen
// from AppState (modeFor): Summary (queue → counters, risk + time InfoBars, ordered steps),
// Running (2px progress, step list, live log), Done (result InfoBar, counters, per-group
// results) and Empty. The header actions belong to the Shell and follow the mode.
#include "app/Localization.h"
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/controllers/ApplyController.h"
#include "app/controllers/CompatController.h"
#include "app/pages/apply/ImpactCard.h"
#include "app/pages/apply/StatStrip.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/LogConsole.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class ApplyPage : public ui::Widget {
public:
    // Paused (D-106): Running's view, stopped before a save, with what to do now on top.
    enum class Mode : std::uint8_t { Empty, NoMount, Summary, Running, Paused, Done };
    [[nodiscard]] static Mode modeFor(const AppState& state);
    // A held or kept run is in the mounted image, unsaved, and the queue holds only what it left
    // (its failed steps): Kaydet saves what ran instead of sending the user back to Apply.
    [[nodiscard]] static bool awaitingSave(const AppState& state);

    struct Intents {
        std::function<void()> review;     // "İncele" on the risk InfoBar (opens the confirm dialog)
        std::function<void()> goImages;   // EmptyState action
        std::function<void()> goFeatures;
        std::function<void()> unmount;    // held run (audit A7): the unmount dialog — save or discard
        // D-124: the impact report — what the queue breaks (guards that are off), switch one on, the dialog.
        std::function<std::vector<CompatController::Impact>()> impact;
        std::function<void(const std::wstring& guard)> keep;
        std::function<void()> openCompat;
    };

    ApplyPage(AppState& state, ApplyController& controller, const ImageSettingsCatalog& settings,
              const Localization& strings, Language language, Intents intents);
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
        int failedStep = -1; // Done only: a plan step that was skipped (listed under the groups)
    };
    void buildSummary();
    void buildRunning();
    void buildDone();
    void buildEmpty();
    [[nodiscard]] std::vector<Row> rows() const;
    [[nodiscard]] std::vector<Row> doneRows() const; // rows() + one line per skipped step
    [[nodiscard]] std::wstring skipReason(const Error& error) const;
    [[nodiscard]] std::wstring groupName(const Row& row) const;
    [[nodiscard]] static ui::icons::Icon groupIcon(const Row& row);
    void paintSteps(ui::Canvas& canvas, ui::RectF area);
    [[nodiscard]] bool live() const noexcept { return m_mode == Mode::Running || m_mode == Mode::Paused; }
    [[nodiscard]] float liveTop() const; // Running / Paused: where the progress bar starts
    void paintSummaryCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);
    void paintDoneCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    ApplyController& m_controller;
    const ImageSettingsCatalog& m_settings; // names the high-risk settings (risk bar)
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    Mode m_mode;
    std::size_t m_subscription = 0;
    core::ops::ApplyPlan m_plan;                    // Summary
    std::vector<core::ops::PlanGroup> m_groups;     // Summary / Running / Done
    std::uint64_t m_logVersion = 0;

    StatStrip* m_stats = nullptr;
    ImpactCard* m_impact = nullptr; // Summary (D-124)
    ui::InfoBar* m_waitBar = nullptr; // Summary (D-124): a download on its way into the queue
    bool m_waitDismissed = false;
    void syncWaitBar();
    ui::InfoBar* m_riskBar = nullptr;
    ui::InfoBar* m_infoBar = nullptr;
    ui::TableView* m_table = nullptr;
    std::vector<ui::CheckField*> m_editions; // Summary: "Diğer sürümlere de uygula" (D-055)
    ui::CheckField* m_pause = nullptr;       // Summary: "Kaydetmeden önce dur" (D-106)
    ui::CheckField* m_guard = nullptr;       // Summary: "Güncellemelere karşı koru" (D-125)
    ui::LogConsole* m_log = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
