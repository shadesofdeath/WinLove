#pragma once
// P14 Kurulum Sonrası (docs/pages/14-post-setup.md, screen 12): a toolbar (when the steps run,
// "Hata olursa devam et", step count + estimate) and the ordered step table:
// handle · # · Adım (icon + name) · Tür · Kaynak (mono) · Bekle (tag; click toggles it).
// Enter / double click edits, Delete removes, Alt+↑ / Alt+↓ moves a step.
#include "app/Localization.h"
#include "app/controllers/PostSetupController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>

namespace wl::app {

class PostSetupPage : public ui::Widget {
public:
    struct Intents {
        std::function<void(std::size_t index)> editStep;
        std::function<void()> goImages;
    };
    PostSetupPage(AppState& state, PostSetupController& controller, const Localization& strings, Language language,
                  Intents intents);
    ~PostSetupPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    bool onTableKey(const ui::KeyEvent& key);

    AppState& m_state;
    PostSetupController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::Dropdown* m_when = nullptr;
    ui::Toggle* m_continue = nullptr;
    ui::InfoBar* m_note = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
