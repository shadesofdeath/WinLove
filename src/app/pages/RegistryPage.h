#pragma once
// P11 Kayıt Defteri (docs/pages/11-registry.md, D-067): the user's own registry entries — values
// typed in the "Değer ekle" dialog and imported .reg files — in one table: Girdi (checkbox) ·
// Anahtar · Değer · Kapsam (✕ removes). Enter / double click edits a typed value; Space toggles,
// Delete removes. Ready-made tweaks are on the Ayarlar page.
// D-139: the header row (the section's title, "5 kayıt · 4 uygulanacak" at its right); the remove ✕ in
// a narrow column of its own; what a row writes, its scope and when, in the detail card (beside it
// the table leaves out the value and the scope).
#include "app/Localization.h"
#include "app/controllers/RegistryController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class RegistryPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void(std::size_t entry)> edit; // a typed value
    };
    RegistryPage(AppState& state, RegistryController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~RegistryPage() override;

    // D-139: the detail card.
    enum Action : int { kActionEdit = 1, kActionToggle, kActionRemove };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void activate(int row);
    void toggleEntry(int row); // the user's click / Space: the entry's tick draws in
    [[nodiscard]] std::wstring valueText(const core::RegistryWrite& write) const;
    [[nodiscard]] ui::TableCellText cellText(int row, int column) const; // the whole text of a cell (its tooltip when cut)

    AppState& m_state;
    RegistryController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
    // What a row shows of the queue and of its writes, worked out in refresh() (audit PG-03): an
    // imported .reg holds thousands of values and paint used to look every one of them up, twice a frame.
    struct RowCache {
        bool checked = false;
        bool user = false;
        bool system = false;
        std::wstring key;   // first key, "  +n" when the entry spans more
        std::wstring name;  // D-139: the entry's name, its value and its scope, made here (paint draws)
        std::wstring value;
        std::wstring scope;
    };
    std::vector<RowCache> m_rowCache;
    int m_checkedRows = 0;
    SummaryLine* m_summaryLine = nullptr;
};

} // namespace wl::app
