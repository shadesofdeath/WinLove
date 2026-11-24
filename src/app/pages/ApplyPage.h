#pragma once
// P05 Uygula (docs/pages/05-apply.md, screens 13 / 13b / 14 / 15). One page, its mode chosen from
// AppState (modeFor). D-138: each mode opens with its head card (apply/ApplyCards):
// Summary — the plan in one line, size before → after, time, the one filled button; then the risk /
// download bars, the impact report, the options and the plan's groups (steps, size effect, risk, time).
// Running / Paused — phase, step, percentage, steps · elapsed · left, a meter, done / skipped / waiting
// (paused: why, and its three choices); then the step list and the live log, laid out once (no jumps).
// Done — the outcome in plain words, time, size before → after and the gain, the steps, what next;
// then the per-group results and every skipped step with its reason. Empty / NoMount: EmptyState.
// The ribbon actions belong to the Shell and follow the mode.
#include "app/Localization.h"
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/controllers/ApplyController.h"
#include "app/controllers/CompatController.h"
#include "app/controllers/IsoController.h"
#include "app/pages/apply/ApplyCards.h"
#include "app/pages/apply/ImpactCard.h"
#include "app/pages/apply/RunSummary.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/LogConsole.h"
#include "ui/widgets/ProgressRing.h"
#include "ui/widgets/TableView.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class ApplyPage : public ui::Widget {
public:
    // Paused (D-106): Running's view, stopped before a save, with what to do now on top.
    enum class Mode : std::uint8_t { Empty, NoMount, Summary, Running, Paused, Done };
    [[nodiscard]] static Mode modeFor(const AppState& state);
    // The mode and, in Done, where the save / discard of what the run left mounted stands
    // (AppState::ApplyRun::closing): the Shell builds the page again when this changes.
    [[nodiscard]] static int modeKey(const AppState& state);
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
        // D-138: the head cards' buttons — the same actions as the ribbon's in that mode.
        std::function<void()> start;        // Summary: Uygula (Shell::requestApply)
        std::function<void()> openFolder;   // Paused: the mount folder
        std::function<void()> stop;         // Paused: leave it unsaved
        std::function<void()> resume;       // Paused: save and go on
        std::function<void()> buildIso;     // Done: the ISO page
        std::function<void()> saveReport;   // Done
        std::function<void()> saveToPreset; // Done
        std::function<void()> saveLog;      // Done
        // D-144: "Bitince ISO oluştur" (AutoIsoController): why it cannot be offered (nullopt: it can), the
        // file a run writes, "Değiştir…" (a save dialog), the ISO's stop and its folder.
        std::function<std::optional<IsoController::Blocker>()> isoUnavailable;
        std::function<std::filesystem::path()> isoOutput;
        std::function<void()> pickIsoOutput;
        // D-144 addendum: "ISO seçenekleri…" — the ISO page's options in a dialog (AppState::isoChoices).
        std::function<void()> isoOptions;
        std::function<void()> cancelIso;
        std::function<void(const std::filesystem::path&)> showIsoInFolder;
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
    // D-142: the running step's progress ring turns (motion.ringFrameMs apart) while the run goes on.
    bool tick(double now) override;
    [[nodiscard]] float tickIntervalMs() const override { return ui::tokens::motion::ringFrameMs; }

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
    // Audit PG-15: the tables asked rows() / doneRows() for every cell (a copy of every plan group
    // each time), and a skipped step's name parsed its recipe on every frame. The rows are made
    // once, where the page is built (a mode change builds a new page); the names once per state
    // change (a feature's name comes from the list, which may be read again).
    std::vector<Row> m_rowCache;     // rows()
    std::vector<Row> m_doneRowCache; // doneRows()
    struct StepText {
        std::uint64_t at = ~std::uint64_t{0}; // AppState::revision() it was worked out at
        std::wstring name;
        std::wstring reason;
    };
    std::vector<StepText> m_skipTexts; // one per m_doneRowCache row (used by the skipped steps)
    [[nodiscard]] const std::wstring& activeName(std::size_t step, const core::ops::Operation& op);
    std::size_t m_activeStep = static_cast<std::size_t>(-1); // the step m_activeName is of
    std::uint64_t m_activeAt = ~std::uint64_t{0};
    std::wstring m_activeName;
    [[nodiscard]] std::wstring skipReason(const Error& error) const;
    [[nodiscard]] std::wstring groupName(const Row& row) const;
    [[nodiscard]] static ui::icons::Icon groupIcon(const Row& row);
    void paintSteps(ui::Canvas& canvas, ui::RectF area);
    [[nodiscard]] bool live() const noexcept { return m_mode == Mode::Running || m_mode == Mode::Paused; }
    void paintSummaryCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);
    void paintDoneCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);
    // D-138: the head cards' texts, made when the state changes (and the run's times once a second).
    void refreshRun();
    [[nodiscard]] std::wstring runLine(const RunSummary& summary) const;
    [[nodiscard]] std::array<DotCount, 3> dotCounts(const RunSummary& summary) const;
    [[nodiscard]] ResultCard::Texts resultTexts(const RunSummary& summary) const;
    [[nodiscard]] std::vector<ResultCard::Action> resultActions(const RunSummary& summary) const;
    // 2026-11-11: the save / discard after the run — the card's meter follows the engine's work.
    void refreshClosing();
    // D-144: the option's line (the file, or why there is none) and the result card's ISO band.
    void syncIsoOption();
    void refreshIso();
    [[nodiscard]] std::wstring isoStageText(int stage) const;
    [[nodiscard]] std::wstring isoBlockerText(int blocker) const;
    [[nodiscard]] std::optional<ResultCard::Texts::Iso> isoTexts() const;
    [[nodiscard]] std::vector<ResultCard::Action> isoActions(const RunSummary& summary) const;
    // D-141: the open source is a VHD / VHDX (saved by merging, no ISO from it).
    [[nodiscard]] bool virtualDiskSource() const;
    // What each line of the step list (Running / Paused) says, made with refreshRun().
    struct StepLine {
        std::wstring name;
        std::wstring caption;
        int state = 0; // 0 waiting, 1 running, 2 done, 3 done with skipped steps
    };
    std::vector<StepLine> m_stepLines;
    ui::RingClock m_stepRing; // the running line's ring (D-142)
    double m_ringNow = 0;     // the last tick
    // The summary table's cells, made once (the plan does not change while the page lives).
    struct PlanCells {
        std::wstring order, name, steps, size, time, risky;
        bool saves = false;
    };
    std::vector<PlanCells> m_planCells;
    std::int64_t m_runSecond = -1; // the elapsed second the run line was made for

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

    PlanHeader* m_planHeader = nullptr; // Summary (D-138)
    RunHeader* m_runHeader = nullptr;   // Running / Paused (D-138)
    ResultCard* m_result = nullptr;     // Done (D-138)
    ImpactCard* m_impact = nullptr; // Summary (D-124)
    ui::InfoBar* m_waitBar = nullptr; // Summary (D-124): a download on its way into the queue
    bool m_waitDismissed = false;
    void syncWaitBar();
    ui::InfoBar* m_riskBar = nullptr;
    ui::TableView* m_table = nullptr;
    std::vector<ui::CheckField*> m_editions; // Summary: "Diğer sürümlere de uygula" (D-055)
    ui::CheckField* m_pause = nullptr;       // Summary: "Kaydetmeden önce dur" (D-106)
    ui::CheckField* m_guard = nullptr;       // Summary: "Güncellemelere karşı koru" (D-125)
    ui::CheckField* m_iso = nullptr;         // Summary: "Bitince ISO oluştur" (D-144)
    ui::Button* m_isoChange = nullptr;       // its "Değiştir…"
    ui::Button* m_isoOptions = nullptr;      // its "ISO seçenekleri…" (D-144 addendum)
    std::wstring m_isoLine;                  // the file it writes, or why it cannot
    std::wstring m_isoSummary;               // the options it is built with, one line under the file
    bool m_isoAvailable = false;
    ui::LogConsole* m_log = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
