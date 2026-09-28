#pragma once
// P01 Kaynak (docs/pages/01-source.md): DropZone + live system card, error InfoBar, recent list.
// The page does not open anything itself: it raises intents (open file / folder / path / live)
// and the Shell runs them on the engine thread.
#include "app/pages/source/LiveCard.h"
#include "app/pages/source/RecentList.h"
#include "app/state/AppState.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/InfoBar.h"

#include <functional>

namespace wl::app {

class SourcePage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> pickFile;
        std::function<void(const std::filesystem::path&)> openPath;
        std::function<void()> editLive;
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

    AppState& m_state;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::DropZone* m_drop = nullptr;
    LiveCard* m_live = nullptr;
    ui::InfoBar* m_error = nullptr;
    RecentList* m_recent = nullptr;
};

} // namespace wl::app
