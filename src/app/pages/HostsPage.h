#pragma once
// D-049 Hosts: block lists for the image's hosts file and the user's own imported entries.
// A table Liste (checkbox, name) · Girdi · Risk · Durum; the last row is "İçe aktarılan girdiler"
// (removable). Under it: the selected list's description. Header actions (Shell): "Önerilenleri
// seç", "Hosts dosyası içe aktar…".
#include "app/Localization.h"
#include "app/controllers/HostsController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class HostsPage : public ui::Widget {
public:
    HostsPage(AppState& state, HostsController& controller, const Localization& strings, Language language,
              std::function<void()> goImages, std::function<void()> importFile);
    ~HostsPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void activate(int row);
    [[nodiscard]] bool isCustomRow(int row) const;
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);

    AppState& m_state;
    HostsController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_importFile;
    std::size_t m_subscription = 0;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
