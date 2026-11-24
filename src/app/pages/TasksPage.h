#pragma once
// D-048 Görevler: scheduled tasks switched off after setup. Toolbar: search ("/") and category;
// a table Görev (checkbox, name) · Kategori · Risk · Durum; under it the selected task's path and
// what switching it off means. Header actions (Shell): "Önerilenleri kapat", "Görev ekle…".
// D-139: the header row's summary ("18 / 38 görev kapalı · 13 kapatılacak"); a marks column after the
// task — queued, the risk's dot, "önerilen" as its mark (it was appended to the name; the Risk column's
// word went to the tooltip and the card); the selected task in the detail card (beside it the line under
// the table and the category column are the card's).
#include "app/Localization.h"
#include "app/controllers/TaskController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class TasksPage : public ui::Widget, public DetailSource {
public:
    TasksPage(AppState& state, TaskController& controller, const Localization& strings, Language language,
              std::function<void()> goImages);
    ~TasksPage() override;

    void focusSearch();
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    // D-139: the detail card.
    enum Action : int { kActionToggle = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    void refresh();
    void rebuild();
    void activate(int row);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] std::wstring categoryName(const std::string& id) const;
    [[nodiscard]] RowMarks marksOf(int row) const;

    AppState& m_state;
    TaskController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<TaskEntry> m_all;
    std::vector<int> m_rows; // indexes into m_all
    // What the queue and the image say of each task, asked in rebuild() — which every change of
    // either comes through (audit PG-19: paint asked for every task on every frame).
    struct TaskState {
        bool off = false;
        bool queued = false;
        bool inImage = false;
    };
    std::vector<TaskState> m_states; // one per m_all
    SummaryLine* m_summaryLine = nullptr; // "n / total görev kapalı · n kapatılacak" right of the toolbar
    std::wstring m_needle;
    int m_category = 0; // 0 all, then categories, then "custom"
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_filter = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
