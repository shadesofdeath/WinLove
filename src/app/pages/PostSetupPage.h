#pragma once
// P14 Kurulum Sonrası (docs/pages/14-post-setup.md, screen 12): a toolbar (when the steps run,
// "Hata olursa devam et", step count + estimate) and the ordered step table:
// handle · # · Adım (icon + name) · Tür · Kaynak (mono) · Bekle (tag; click toggles it).
// Enter / double click edits, Delete removes, Alt+↑ / Alt+↓ moves a step.
// 2c (after D-139): the selected step in the detail card — its whole command / source the table cuts,
// when and in which order it runs, waiting, what an error does, a step that is not valid; its buttons
// are the row's own Enter / "Bekle" click / Delete. Beside the card the table leaves Kaynak, then Tür,
// to it. Steps have no risk and no state of their own (all of them are the plan): no marks column.
#include "app/Localization.h"
#include "app/controllers/PostSetupController.h"
#include "app/pages/DetailCard.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class PostSetupPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void(std::size_t index)> editStep;
        std::function<void()> goImages;
    };
    PostSetupPage(AppState& state, PostSetupController& controller, const Localization& strings, Language language,
                  Intents intents);
    ~PostSetupPage() override;

    // The detail card (2c).
    enum Action : int { kActionEdit = 1, kActionWait, kActionDelete };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    void selectRowForTests(int row) { m_table->setSelected(row); }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    bool onTableKey(const ui::KeyEvent& key);
    // The "Kaynak" cell and the toolbar's summary, worked out once per state change (audit PG-19:
    // every source cell copied its step into a plan and validated it, on every frame).
    struct StepText {
        std::wstring source;
        bool problem = false;
    };
    void syncTexts();
    std::vector<StepText> m_stepTexts; // one per step of the plan
    std::wstring m_summary;
    std::uint64_t m_textsAt = ~std::uint64_t{0}; // AppState::revision() they are good for

    AppState& m_state;
    PostSetupController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::Dropdown* m_when = nullptr;
    ui::Toggle* m_continue = nullptr;
    class SummaryLine* m_summaryLine = nullptr; // D-139: "7 adım · ~4 dk tahmini" at the toolbar's right
    ui::InfoBar* m_note = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
