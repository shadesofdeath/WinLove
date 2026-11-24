#pragma once
// "Son kullanılanlar" (P01). 2026-11-10, the owner's request (a departure from the reference tool's
// look, like D-091's cards before it): one column of rows instead of a grid of cards. A row is two
// text lines — the file name over its Windows version — with the icon on the left and "ISO · 6,90 GB ·
// bugün 18:40" at the right in the quiet style; the pointer's row gets bg.hover, faded (ItemHover, as
// a table row), the selected one accent.subtle + the accent bar while the list has focus. One click /
// Enter opens, Delete removes, right click / Menu key the menu (Aç · Klasörde göster · SHA-256 ·
// Listeden kaldır). The x at the row's end removes it from the list: always shown on an entry whose
// file is gone (dimmed, "Dosya bulunamadı"), on the others under the pointer or on the selected row;
// → puts the keyboard on it (Enter / Space), ← back. The rows scroll inside the list when it is taller
// than its room. Every text is formed in setEntries() and measured in layout(): paint() only draws.
#include "app/Localization.h"
#include "app/state/RecentSources.h"
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Widget.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class RecentList : public ui::Widget {
public:
    // Two lines — the name (Body) over its Windows version (Caption), each its style's line height —
    // with a spacing step above and below: a little taller than a table row, as two lines read
    // better than one crowded one (D-136: from the type styles, not 12 px lines in 16 boxes).
    static constexpr float kNameLine = ui::tokens::kTypeStyles[static_cast<std::size_t>(ui::tokens::TypeStyle::Body)].lineHeight;
    static constexpr float kSummaryLine =
        ui::tokens::kTypeStyles[static_cast<std::size_t>(ui::tokens::TypeStyle::Caption)].lineHeight;
    static constexpr float kRow = kNameLine + kSummaryLine + 2 * ui::tokens::spacing::s3;
    // Where a row's icon (iconLarge) and its text start; the drop row above and the section titles
    // line up with them.
    static constexpr float kInset = ui::tokens::spacing::s5;
    static constexpr float kTextX = kInset + ui::tokens::size::iconLarge + 10.0f;

    RecentList(const Localization& strings, Language language);
    ~RecentList() override;

    std::function<void(const std::filesystem::path&)> onOpen;
    // The x of a row, Delete on the selected row, or the menu's "Listeden kaldır".
    std::function<void(const std::filesystem::path&)> onRemove;
    std::function<void(const std::filesystem::path&)> onShowInFolder; // context menu
    std::function<void(const std::filesystem::path&)> onVerifyHash;   // context menu, files only (D-058)

    // Checks once whether each entry's file is still there (never while painting).
    void setEntries(std::vector<RecentSource> entries);
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(m_rows.size()); }
    // Every row's height together; the empty list keeps one row for its quiet note.
    [[nodiscard]] float contentHeight() const noexcept;

    // Geometry and state the page and the tests ask about (window DIPs, scrolled).
    [[nodiscard]] int rowAt(ui::PointF p) const;
    [[nodiscard]] ui::RectF rowRect(int index) const;
    [[nodiscard]] ui::RectF removeRect(int index) const;
    [[nodiscard]] bool removeShown(int index) const; // the x is drawn on this row now
    [[nodiscard]] bool missing(int index) const;     // its file was not found
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    [[nodiscard]] bool removeFocused() const noexcept { return m_onRemove; } // → moved the keyboard onto the x
    [[nodiscard]] float scrollOffset() const noexcept { return m_offset; }
    [[nodiscard]] float maxScroll() const noexcept;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool tick(double now) override;
    void onPointerMove(ui::PointF p) override;
    void onPointerDown(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onFocusChanged(bool focused) override;
    void onClick() override;
    void onDoubleClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool onWheel(ui::PointF p, float lines) override;
    bool onContextMenu(ui::PointF p) override; // Aç / Klasörde göster / SHA-256 / Listeden kaldır
    [[nodiscard]] ui::RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override;
    [[nodiscard]] ui::Cursor cursor() const override;
    [[nodiscard]] ui::RectF tooltipAnchor() const override;

private:
    struct Row {
        std::filesystem::path path;
        std::wstring name;    // file or folder name
        std::wstring summary; // "11 25H2 · 26200.9445"; may be empty
        std::wstring meta;    // "ISO · 6,90 GB · bugün 18:40"
        std::wstring where;   // the folder it is in: the row's tooltip
        ui::icons::Icon icon = ui::icons::Icon::ImageWim;
        bool exists = true;
        float metaWidth = 0; // layout(): the meta's (or the missing note's) width
    };

    [[nodiscard]] float contentRight() const noexcept; // where a row's x ends (the scroll bar's room kept)
    [[nodiscard]] bool scrolls() const noexcept;
    void setOffset(float offset);
    void reveal(int index);
    void setHover(int row, bool overRemove);
    void select(int index);
    void openSelected();
    void remove(int index);

    const Localization& m_strings;
    Language m_language;
    std::vector<Row> m_rows;
    int m_hoverRow = -1;
    int m_selected = -1;
    bool m_hoverRemove = false; // pointer over the hovered row's x
    int m_downRemove = -1;      // row whose x the press started on
    bool m_onRemove = false;    // keyboard on the selected row's x
    float m_offset = 0;
    float m_noteWidth = 0; // "Dosya bulunamadı"
    int m_menuRow = -1;    // the row whose context menu is open
    bool m_measured = false; // the rows' widths are known (layout(), with a host)
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // false once destroyed (the menu's close)
    ui::ItemHover m_hoverFade;
    ui::ScrollBar* m_bar = nullptr;
};

} // namespace wl::app
