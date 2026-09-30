#pragma once
// D-048 Görevler: scheduled tasks switched off after setup. Toolbar: search ("/") and category;
// a table Görev (checkbox, name) · Kategori · Risk · Durum; under it the selected task's path and
// what switching it off means. Header actions (Shell): "Önerilenleri kapat", "Görev ekle…".
#include "app/Localization.h"
#include "app/controllers/TaskController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class TasksPage : public ui::Widget {
public:
    TasksPage(AppState& state, TaskController& controller, const Localization& strings, Language language,
              std::function<void()> goImages);
    ~TasksPage() override;

    void focusSearch();
    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    void refresh();
    void rebuild();
    void activate(int row);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] std::wstring categoryName(const std::string& id) const;

    AppState& m_state;
    TaskController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<TaskEntry> m_all;
    std::vector<int> m_rows; // indexes into m_all
    std::wstring m_needle;
    int m_category = 0; // 0 all, then categories, then "custom"
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_filter = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
