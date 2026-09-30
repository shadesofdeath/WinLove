#pragma once
// Command palette per 03_components/log-console-inspector-diff-palette.md (screen 21), shown
// through Host::pushModal (scrim + input trap): a 560px panel 120px from the top — bg.overlay,
// 1px line.strong, r3, elevation.dialog. 40px search row (icon, input, Esc keycap), then the
// groups: "Sonuçlar" 32px rows (icon, name, caption; the selected one bg.raised + ↵) and
// "Komutlar" 24px rows (icon, name, shortcut). ↑↓ move (wrapping), Enter runs, Esc / Ctrl+K /
// a click outside close. The palette only lists and picks; what an item does is the owner's.
#include "app/shell/PaletteIndex.h"
#include "ui/widgets/SearchBox.h"

#include <functional>

namespace wl::app {

class CommandPalette : public ui::Widget {
public:
    struct Labels {
        std::wstring placeholder;
        std::wstring escKey;
        std::wstring enterKey;
        std::wstring results;
        std::wstring commands;
        std::wstring noResults;
        std::wstring noResultsHint;
    };
    using Search = std::function<PaletteResults(const std::wstring& query)>;
    CommandPalette(Labels labels, Search search);

    // Both destroy the palette (the owner pops the modal): onClose alone for Esc and friends;
    // for a pick, onClose first and then onRun with a copy of the item.
    std::function<void()> onClose;
    std::function<void(const PaletteItem&)> onRun;

    [[nodiscard]] ui::SearchBox& input() noexcept { return *m_input; }
    void setQuery(std::wstring query); // as if typed
    [[nodiscard]] ui::RectF panel() const noexcept { return m_panel; }
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    [[nodiscard]] const PaletteResults& found() const noexcept { return m_found; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    void onPointerMove(ui::PointF p) override;
    void onPointerUp(ui::PointF p) override;
    void onClick() override;
    bool onWheel(ui::PointF p, float lines) override;

private:
    void refresh(); // the query changed
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] const PaletteItem& itemAt(int index) const;
    [[nodiscard]] ui::RectF rowRect(int index) const;
    [[nodiscard]] int rowAt(ui::PointF p) const;
    void select(int index);
    void run(int index);
    void updateCompletion();
    void paintRow(ui::Canvas& canvas, const PaletteItem& item, ui::RectF row, bool selected) const;

    Labels m_labels;
    Search m_search;
    std::wstring m_query;
    PaletteResults m_found;
    int m_selected = 0;
    ui::RectF m_panel{};
    ui::SearchBox* m_input = nullptr;
    ui::PointF m_lastUp{};
};

} // namespace wl::app
