#pragma once
// P08 Güncellemeler (docs/pages/08-updates.md, screen 06): a 56px drop zone and the queued update
// packages in the order they will be applied (SSU → LCU → .NET → other, Planner). Columns:
// handle · Sıra · Paket · Tür · KB · Boyut · Durum (compatibility with the mounted edition).
// Delete removes the selected package from the queue.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "core/image/UpdatePackage.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <map>

namespace wl::app {

class UpdatesPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> addPackages;
        std::function<void()> goImages;
    };
    UpdatesPage(AppState& state, const Localization& strings, Language language, Intents intents);
    ~UpdatesPage() override;

    // Queue the given packages (analysed, ordered by the planner). Returns how many were new.
    static std::size_t queuePackages(AppState& state, const std::vector<std::filesystem::path>& files);
    void setDragState(ui::DropZone::DragState state);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    enum class Compat : std::uint8_t { Ok, WrongWindows, WrongArch, Unknown };
    void refresh();
    [[nodiscard]] const core::UpdateInfo& infoFor(const std::filesystem::path& path);
    [[nodiscard]] Compat compatibility(const core::UpdateInfo& info) const;
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<core::ops::Operation> m_rows; // queued AddPackage, apply order
    std::map<std::wstring, core::UpdateInfo> m_info;
    ui::DropZone* m_drop = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
