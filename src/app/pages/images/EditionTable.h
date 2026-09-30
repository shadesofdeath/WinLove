#pragma once
// Editions table (screen 02, datagrid.md): column header 24 + 24px rows with 1px separators.
// Columns: checkbox 28 · "Index · Ad" (flex, layers icon + 6) · arch 80 · build 120 (mono) ·
// language 72 · size 96 (mono, right) · status 120. Several rows can be marked (the check boxes
// show them; rules in EditionSelection.h), one of them is the primary row.
// ↑↓ Home End move (Shift extends), Ctrl+A all, Enter / double click = activate (mount),
// Del = delete, F2 = rename, right click = row menu.
#include "app/Localization.h"
#include "app/pages/images/EditionSelection.h"
#include "core/image/ImageInfo.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <vector>

namespace wl::app {

class EditionTable : public ui::Widget {
public:
    enum class RowState : std::uint8_t { Normal, Mounted, Working, Failed };

    EditionTable(const Localization& strings, Language language);

    std::function<void(int primary, std::vector<int> marked)> onSelect; // edition indexes
    std::function<void(int index)> onActivate;
    std::function<void()> onDelete;                        // Del: the marked editions
    std::function<void()> onRename;                        // F2: the primary one
    // Right click on a row. A marked row keeps the marks (the menu acts on all of them); any
    // other row becomes the only one.
    std::function<bool(int index, ui::PointF at)> onMenu;

    void setImages(std::vector<core::ImageInfo> images);
    void setSelection(std::optional<int> primary, const std::vector<int>& marked); // edition indexes
    // Mounted / in-progress / failed row and whether other rows are dimmed (operation running).
    void setRowState(std::optional<int> index, RowState state, bool dimOthers);
    [[nodiscard]] float contentHeight() const noexcept;

    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onPointerDown(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onDoubleClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool onContextMenu(ui::PointF p) override;
    [[nodiscard]] ui::RectF focusRect() const override;

private:
    [[nodiscard]] int rowAt(ui::PointF p) const;
    [[nodiscard]] ui::RectF rowRect(int row) const;
    [[nodiscard]] int rowOfIndex(std::optional<int> index) const;
    void publish(); // tells onSelect what is marked now

    const Localization& m_strings;
    Language m_language;
    std::vector<core::ImageInfo> m_images;
    EditionSelection m_selection; // rows
    std::optional<int> m_stateIndex;
    RowState m_state = RowState::Normal;
    bool m_dimOthers = false;
    int m_hoverRow = -1;
};

} // namespace wl::app
