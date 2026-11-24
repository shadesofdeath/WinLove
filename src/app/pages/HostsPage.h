#pragma once
// D-049 Hosts: block lists for the image's hosts file and the user's own imported entries.
// A table Liste (checkbox, name) · marks · Girdi · Durum; the last row is "İçe aktarılan girdiler"
// (removable). Under it: the selected list's description. Header actions (Shell): "Önerilenleri
// seç", "Hosts dosyası içe aktar…".
// 2c (after D-139): the marks column after the name — queued, the risk's dot (the Risk column's word
// went to the tooltip and the card), "önerilen"; the selected list in the detail card (what it says,
// its state and the host names it blocks, which the table only counts; beside it the line under the
// table is the card's).
#include "app/Localization.h"
#include "app/controllers/HostsController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <vector>

namespace wl::app {

class HostsPage : public ui::Widget, public DetailSource {
public:
    HostsPage(AppState& state, HostsController& controller, const Localization& strings, Language language,
              std::function<void()> goImages, std::function<void()> importFile);
    ~HostsPage() override;

    // The detail card (2c).
    enum Action : int { kActionToggle = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    void selectRowForTests(int row) { m_table->setSelected(row); }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void activate(int row);
    [[nodiscard]] bool isCustomRow(int row) const;
    [[nodiscard]] RowMarks marksOf(int row) const;
    [[nodiscard]] Str stateWord(int row) const; // the Durum column's word
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);

    // What the queue and the image say of each row, asked in refresh() (every change comes through it).
    struct RowState {
        bool on = false;
        bool queued = false;
        bool removing = false; // queued as an empty section
        bool inImage = false;
    };
    std::vector<RowState> m_rows; // the lists, then the custom row

    AppState& m_state;
    HostsController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_importFile;
    std::size_t m_subscription = 0;
    ui::TableView* m_table = nullptr;
    class SummaryLine* m_summaryLine = nullptr; // D-139: "n alan adı engellenir" at the header row's right (refresh())
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
