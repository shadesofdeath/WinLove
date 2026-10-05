#pragma once
// P11 Kayıt Defteri (docs/pages/11-registry.md, D-067): the user's own registry entries — values
// typed in the "Değer ekle" dialog and imported .reg files — in one table: Girdi (checkbox) ·
// Anahtar · Değer · Kapsam (✕ removes). Enter / double click edits a typed value; Space toggles,
// Delete removes. Ready-made tweaks are on the Ayarlar page.
#include "app/Localization.h"
#include "app/controllers/RegistryController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <string>

namespace wl::app {

class RegistryPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void(std::size_t entry)> edit; // a typed value
    };
    RegistryPage(AppState& state, RegistryController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~RegistryPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void activate(int row);
    [[nodiscard]] std::wstring valueText(const core::RegistryWrite& write) const;

    AppState& m_state;
    RegistryController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
