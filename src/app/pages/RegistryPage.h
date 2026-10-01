#pragma once
// P11 Kayıt Defteri (docs/pages/11-registry.md, screen 08): a 2-column grid of tweak categories
// ("{s} / {n} seçili") + the selected category's table: Tweak (checkbox) · Anahtar · Kapsam.
// The last category, "Özel .reg", lists imported .reg files (checkbox, value count, remove).
#include "app/Localization.h"
#include "app/controllers/RegistryController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class CategoryGrid;

class RegistryPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> goImages;
    };
    RegistryPage(AppState& state, RegistryController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~RegistryPage() override;

    void showCustom(); // after an import: select "Özel .reg"
    [[nodiscard]] static Str categoryName(std::string_view id);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void select(int category);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void activate(int row);
    [[nodiscard]] bool isCustom() const;
    [[nodiscard]] std::string categoryId(int index) const;

    AppState& m_state;
    RegistryController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    int m_category = 0;
    std::vector<int> m_rows; // tweak indexes, or import indexes for "custom"
    CategoryGrid* m_grid = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
