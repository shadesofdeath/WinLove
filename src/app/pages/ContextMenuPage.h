#pragma once
// D-155 Sağ tık menüsü (docs/pages/24-context-menu.md): the image's right-click menu entries and our
// own items in one table — search ("/"), "Yalnızca değişenler"; columns Öğe · marks · Nerede · Tür ·
// Durum. A click on Durum (or Enter / Space) hides or shows the entry; an item of ours leaves with
// Kaldır. "Öğe ekle…" is the shell's dialog (the ribbon).
#include "app/Localization.h"
#include "app/controllers/ContextMenuController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class ContextMenuPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void()> addItem; // the shell's dialog
    };
    ContextMenuPage(AppState& state, ContextMenuController& controller, const Localization& strings, Language language,
                    Intents intents);
    ~ContextMenuPage() override;

    void focusSearch();
    void selectRow(int row); // render demos, tests
    [[nodiscard]] static Str placeName(std::wstring_view place);
    [[nodiscard]] std::wstring placesText(const std::vector<std::wstring>& places) const;
    [[nodiscard]] int rowCount() const noexcept { return static_cast<int>(m_rows.size()); } // tests

    enum Action : int { kActionToggle = 1, kActionRevert, kActionRemove };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    // A row: an entry of the image (index into the list) or one of our queued items.
    struct Row {
        int entry = -1;          // index into AppState::contextMenuList()->items
        int item = -1;           // index into m_items
    };
    void refresh();
    void refilter();
    void updateSummary();
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action, std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] const core::ContextMenuEntry* entryAt(int row) const;
    [[nodiscard]] const core::ContextMenuItem* itemAt(int row) const;
    void toggle(int row);
    [[nodiscard]] Str kindName(const core::ContextMenuEntry& entry) const;

    AppState& m_state;
    ContextMenuController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<core::ContextMenuItem> m_items; // the queue's, at the last refilter
    std::vector<Row> m_rows;
    std::wstring m_needle;
    bool m_onlyChanged = false;
    ui::SearchBox* m_search = nullptr;
    ui::Toggle* m_changed = nullptr;
    SummaryLine* m_summaryLine = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
