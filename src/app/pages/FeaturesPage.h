#pragma once
// P04 Özellikler as the reference tool's page (D-111, research README §7.6): two tabs, "Talep Üzerine
// Özellikler on/n" (capabilities, under the reference tool's categories: Diller · Yazı Tipleri · Sürücüler ·
// RSAT · Diğer, from the capability id) and "İsteğe Bağlı Özellikler on/n" (optional features). One
// 23.5 row each: the check circle (ticked = it stays / will be on), the name, its description,
// "Etkin" (a green check when the image has it on now) and "Yüklendi" (the size of what is not
// installed). Unticking queues the disable / removal; nothing touches the image until "Uygula".
// Without a mounted image, while loading or on failure: an EmptyState.
// D-139: the header row ("Tümü ▾", search, "9 / 10 açık · 2 değişecek" at the right), the column heads
// in the table's own header, a marks column after the name (queued / held / permanent), the category
// rows in semibold, the selected feature in the detail card (beside it the description column goes:
// the card says it in full).
#include "app/Localization.h"
#include "app/controllers/FeatureController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>

namespace wl::app {

class FeaturesPage : public ui::Widget, public DetailSource {
public:
    FeaturesPage(AppState& state, FeatureController& controller, const Localization& strings, Language language,
                 std::function<void()> goToImages);
    ~FeaturesPage() override;

    void focusSearch();
    // Command palette: drops the filters, selects the feature's row and scrolls to it.
    void reveal(const std::wstring& name);
    [[nodiscard]] static Str statusName(FeatureController::Status status);
    void setAllExpanded(bool expanded);

    // The reference tool's capability categories, from the id ("Language.Fonts.Jpan~~~und-JPAN~0.0.1.0" → Fonts).
    enum class Category : std::uint8_t { Languages, Fonts, Drivers, Rsat, Other };
    [[nodiscard]] static Category categoryOf(const std::wstring& capabilityName);
    // D-114: "Language.Basic~~~tr-TR~0.0.1.0" → "tr-TR"; empty for no language capability.
    [[nodiscard]] static std::wstring languageOf(const std::wstring& capabilityName);
    [[nodiscard]] static std::wstring languageTitle(const std::wstring& tag); // "Türkçe (Türkiye) - Türkçe (Türkiye) - tr-TR"

    // D-082 / D-139: what keeps a capability in the image, in the guards' words (the shell's).
    std::function<std::wstring(const core::OptionalFeature&)> explainGuard;
    [[nodiscard]] std::wstring summaryText() const { return m_summary->text(); } // tests

    // D-139: the detail card.
    enum Action : int { kActionToggle = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

    // Tests (2026-11-10 polish): what a category / language row says at its right, and the column
    // heads of the filter row against the cells under them.
    [[nodiscard]] int rowCount() const noexcept { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] std::wstring rowCountText(int row) const;
    [[nodiscard]] std::wstring rowSizeText(int row) const;
    [[nodiscard]] ui::RectF enabledHead() const noexcept { return m_headEnabled; }
    [[nodiscard]] ui::RectF sizeHead() const noexcept { return m_headSize; }
    [[nodiscard]] ui::RectF cellBox(int column) const; // a cell's text box (row 0), as paintCell gets it

private:
    enum class Filter : std::uint8_t { All, On, Off, Queued };
    static constexpr std::uint64_t kNoRevision = ~std::uint64_t{0}; // AppState::revision() never gets there
    struct Row {
        int category = -1; // a category row (capabilities tab), else -1
        int item = -1;     // index into optionalFeatures()->items
        int depth = 0;     // D-114: indent (language group, feature tree)
        std::wstring group; // D-114: a language group row ("tr-TR") under Diller
        std::wstring title; // a language group's caption (languageTitle asks Windows: once, in refilter)
        // An item row's circle, worked out the first time the row is drawn after a state change
        // (audit PG-05 / PG-13: the guard check ran for every visible row on every frame).
        std::uint64_t stateAt = kNoRevision;
        TreeCheck check = TreeCheck::Off;
        bool locked = false;
        RowMark mark = RowMark::None; // D-139: its marks cell, worked out with the circle (never per frame)
    };
    void syncRow(Row& row, const core::OptionalFeature& item) const; // the circle, the lock, the mark of a row
    // How much of a category / of a language stays on: one pass over the list per state change,
    // not one per group row per frame (audit PG-05) - and, since the 2026-11-10 polish, never from
    // paint: refresh() and the queue's notification run it. `count` / `size`: what the row says at
    // its right ("9/10", the not-installed size of what is under it, as the "Yüklendi" column).
    struct Counts {
        int on = 0;
        int all = 0;
        std::uint64_t bytes = 0;
        std::wstring count;
        std::wstring size;
    };
    void updateCounts();
    [[nodiscard]] static TreeCheck checkOf(const Counts& counts) {
        return counts.on == 0 ? TreeCheck::Off : counts.on == counts.all ? TreeCheck::On : TreeCheck::Mixed;
    }
    std::array<Counts, 5> m_categoryCounts{};
    std::map<std::wstring, Counts> m_languageCounts;
    std::uint64_t m_countsAt = kNoRevision;
    std::set<std::wstring> m_openGroups; // language groups open (closed by default, as the reference tool)
    void refresh(); // state changed: data, empty states, rows
    void updateSummary();
    [[nodiscard]] RowMarks marksOf(int row) const;
    void refilter(); // search / filter / tab changed
    void updateTabs();
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                   std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);
    [[nodiscard]] const core::OptionalFeature* itemAt(int row) const;
    [[nodiscard]] bool visible(const core::OptionalFeature& item) const;
    [[nodiscard]] std::wstring tooltipOf(int row) const;
    void toggleByUser(int row); // an item row's circle / Space: the tick draws in
    void revealGroupCheck(int row); // after a category's / language's box was clicked
    [[nodiscard]] const Counts* countsOf(const Row& row) const; // a category / language row's
    // The column heads in the filter row, where their cells are (layout()).
    ui::RectF m_headDescription{};
    ui::RectF m_headEnabled{};
    ui::RectF m_headSize{};

    AppState& m_state;
    FeatureController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<Row> m_rows;
    std::set<int> m_collapsed; // capability categories closed
    bool m_capabilities = true; // the tab shown
    std::wstring m_needle;
    Filter m_filter = Filter::All;
    ui::TabBar* m_tabs = nullptr;
    ui::Dropdown* m_filterBox = nullptr;
    ui::SearchBox* m_search = nullptr;
    SummaryLine* m_summary = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
