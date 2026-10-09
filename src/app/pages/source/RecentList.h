#pragma once
// "Son kullanılanlar" (screen 01), D-091 as cards: a grid of 64px cards (≥ 280 wide) — icon tile,
// name, Windows version, type · size · last opened — hover bg.raised, selection accent.subtle, one
// click / Enter opens, the x (hover) or Delete removes, right click the menu. One focusable widget
// with roving selection (←→↑↓ Home End), painted directly — at most 10 entries.
#include "app/Localization.h"
#include "app/state/RecentSources.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <vector>

namespace wl::app {

class RecentList : public ui::Widget {
public:
    RecentList(const Localization& strings, Language language);

    std::function<void(const std::filesystem::path&)> onOpen;
    // The x at the end of the hovered / selected row, or Delete on the selected row.
    std::function<void(const std::filesystem::path&)> onRemove;
    std::function<void(const std::filesystem::path&)> onShowInFolder; // context menu
    std::function<void(const std::filesystem::path&)> onVerifyHash;   // context menu, files only (D-058)

    void setEntries(std::vector<RecentSource> entries);
    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    // The rows of cards at the list's width.
    [[nodiscard]] float contentHeight() const noexcept;

    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onPointerDown(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    void onDoubleClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool onContextMenu(ui::PointF p) override; // Aç / Klasörde göster / Listeden kaldır
    [[nodiscard]] ui::RectF focusRect() const override;

private:
    [[nodiscard]] int columnCount() const noexcept;
    [[nodiscard]] int rowAt(ui::PointF p) const;
    [[nodiscard]] ui::RectF rowRect(int index) const;
    [[nodiscard]] ui::RectF removeRect(int index) const;
    void select(int index);
    void openSelected();
    void remove(int index);

    const Localization& m_strings;
    Language m_language;
    std::vector<RecentSource> m_entries;
    std::vector<bool> m_exists;
    int m_hoverRow = -1;
    int m_selected = -1;
    bool m_hoverRemove = false; // pointer over the hovered row's x
    int m_downRemove = -1;      // row whose x the press started on
};

} // namespace wl::app
