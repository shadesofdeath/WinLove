#pragma once
// P01 Kaynak (docs/pages/01-source.md): DropZone, error InfoBar, the images mounted on this PC
// (D-064: any folder, any tool — double-click takes one over), recent list.
// The page does not open anything itself: it raises intents (open file / path)
// and the Shell runs them on the engine thread.
#include "app/pages/source/RecentList.h"
#include "app/state/AppState.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class SourcePage : public ui::Widget {
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

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refreshRecent();
    void refreshMounts();
    void paintMountCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, bool selected);

    AppState& m_state;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::DropZone* m_drop = nullptr;
    ui::InfoBar* m_error = nullptr;
    RecentList* m_recent = nullptr;
    ui::TableView* m_mounts = nullptr;
    std::vector<core::MountCheck> m_mountRows;
};

} // namespace wl::app
