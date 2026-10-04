#pragma once
// D-050 / D-054 Uygulamalar: two tabs.
// - "Eklenecek uygulamalar": a 56px drop zone for .appx / .msix (bundles), then Uygulama ·
//   Yayıncı · Sürüm · Mimari · Bağımlılıklar; Delete removes. Under it: what the selected package
//   needs and whether it was found.
// - "Varsayılan uygulamalar": Tarayıcı quick pick, then Tür (extension / protocol) · Uygulama ·
//   ProgId; Delete takes a row out.
// Header actions (Shell): "Paket ekle…", "XML içe aktar…", "Bu bilgisayardan al".
#include "app/Localization.h"
#include "app/controllers/AppsController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"


#include <functional>

namespace wl::app {

class AppsPage : public ui::Widget {
public:
    AppsPage(AppState& state, AppsController& controller, const Localization& strings, Language language,
             std::function<void()> addPackages, std::function<void()> goImages, std::function<void()> stopStore = {});
    ~AppsPage() override;

    void setDragState(ui::DropZone::DragState state);
    void showDefaultsTab();
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    [[nodiscard]] bool defaultsTab() const { return m_tabs->selected() == 1; }
    void paintAppCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void paintAssocCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    AppsController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<core::AppxInstall> m_apps;
    std::vector<core::AppAssociation> m_assoc;
    ui::TabBar* m_tabs = nullptr;
    ui::DropZone* m_drop = nullptr;
    class FetchStrip* m_store = nullptr; // D-066: a Store download, where the drop zone is
    ui::TableView* m_appTable = nullptr;
    ui::Dropdown* m_browser = nullptr;
    ui::TableView* m_assocTable = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
