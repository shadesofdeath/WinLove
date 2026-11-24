#pragma once
// P01 Kaynak (docs/pages/01-source.md): the drop row, error / host InfoBars, the images mounted on
// this PC (D-064: any folder, any tool — double-click takes one over), the recent list.
// 2026-11-10 (the owner's request, away from the reference tool's tall zone and D-091's cards): the
// drop target is one row and the recent sources a compact list that takes the rest of the card and
// scrolls in it; only when even a few of its rows do not fit does the page itself scroll.
// The page does not open anything itself: it raises intents (open file / path)
// and the Shell runs them on the engine thread.
#include "app/pages/source/DropRow.h"
#include "app/pages/source/RecentList.h"
#include "app/shell/PageView.h"
#include "app/state/AppState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class SourcePage : public ui::Widget, public ScrollableBody {
public:
    struct Intents {
        std::function<void()> pickFile;
        std::function<void(const std::filesystem::path&)> openPath;
        std::function<void(const std::filesystem::path&)> removePath; // out of the recent list
        std::function<void(const std::filesystem::path&)> showInFolder; // Explorer, with the entry selected
        std::function<void(const std::filesystem::path&)> verifyHash;   // SHA-256 dialog (D-058)
        std::function<void(const std::filesystem::path& folder)> adoptMount;                      // D-064
        std::function<void(const std::filesystem::path& folder, const std::wstring& edition)> discardMount;
    };

    SourcePage(AppState& state, const Localization& strings, Language language, Intents intents);
    ~SourcePage() override;

    void setLoading(bool loading);
    void showError(const std::wstring& message);
    void setDragState(ui::DropZone::DragState state);

    // The recent list (tests: its rows, the x, the scroll range).
    [[nodiscard]] RecentList& recentList() noexcept { return *m_recent; }
    [[nodiscard]] DropRow& dropRow() noexcept { return *m_drop; }
    // ScrollableBody: everything above the list and a few of its rows.
    [[nodiscard]] float contentHeight() const override { return m_content; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refreshRecent();
    void refreshMounts();
    void refreshHost(); // D-081: a warning when this PC's DISM files are not whole
    void paintMountCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, bool selected);

    AppState& m_state;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    DropRow* m_drop = nullptr;
    ui::InfoBar* m_error = nullptr;
    ui::InfoBar* m_host = nullptr;
    ui::TableView* m_mounts = nullptr;
    RecentList* m_recent = nullptr;
    std::vector<core::MountCheck> m_mountRows;
    std::vector<std::wstring> m_mountLabels; // the image column's text per row (refreshMounts)
    // layout(): where the section titles and the mounts' hint go, and the height the page needs.
    ui::RectF m_mountsTitle{};
    ui::RectF m_mountsHint{};
    ui::RectF m_recentTitle{};
    float m_content = 0;
};

} // namespace wl::app
