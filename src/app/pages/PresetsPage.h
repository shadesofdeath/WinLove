#pragma once
// P15 Presetler (docs/pages/15-presets.md, screen 17): the library on the left (360px: name,
// number of changes; Yükle / Dışa aktar / Sil under it) and the comparison on the right:
// A and B (any preset, or the current queue), a swap button, "Aynıları göster", the summary
// "+9 eklendi · −26 kaldırıldı · 4 değişti" and the diff rows (+ success, − error, ~ warning).
#include "app/Localization.h"
#include "app/controllers/PresetController.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <vector>

namespace wl::app {

class PresetsPage : public ui::Widget {
public:
    struct Intents {
        std::function<void(std::size_t index)> load;
        std::function<void(std::size_t index)> exportPreset;
        std::function<void(std::size_t index)> remove;
    };
    PresetsPage(AppState& state, PresetController& controller, const Localization& strings, Intents intents);
    ~PresetsPage() override;

    // The library changed (saved, imported, deleted): rebuild list, choices and the diff.
    void reloadList();

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void updateDiff();
    [[nodiscard]] Preset choice(int index) const; // a preset, or the current queue (last choice)
    void paintListCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void paintDiffCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    PresetController& m_controller;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::TableView* m_list = nullptr;
    ui::Button* m_load = nullptr;
    ui::Button* m_export = nullptr;
    ui::Button* m_delete = nullptr;
    ui::Dropdown* m_a = nullptr;
    ui::Dropdown* m_b = nullptr;
    ui::Button* m_swap = nullptr;
    ui::Toggle* m_same = nullptr;
    ui::TableView* m_diff = nullptr;
    std::vector<std::size_t> m_counts; // changes per preset (list column)
    std::vector<PresetController::DiffRow> m_rows;
    PresetController::DiffSummary m_summary;
};

} // namespace wl::app
