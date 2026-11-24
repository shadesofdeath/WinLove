#pragma once
// P08 Güncellemeler (docs/pages/08-updates.md, screen 06): a 56px drop zone and the queued update
// packages in the order they will be applied (SSU → LCU → .NET → other, Planner). Columns:
// handle · Sıra · Paket · Tür · KB · Boyut · Durum (compatibility with the mounted edition).
// Delete removes the selected package from the queue.
// D-139: a header row between the drop zone and the table — the list's title, "4 paket · 1,2 GB · 1 uyumsuz"
// at its right; what a package is read once per queue change (refresh), not when a row is first drawn;
// the selected package in the detail card (beside it the Tür / KB columns are the card's).
#include "app/Localization.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageHeader.h"
#include "app/state/AppState.h"
#include "core/image/UpdatePackage.h"
#include "ui/widgets/DropZone.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <map>

namespace wl::app {

class UpdatesPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> addPackages;
        std::function<void()> goImages;
        std::function<void()> stopFetch; // D-046: stop the catalog search / download
    };
    UpdatesPage(AppState& state, const Localization& strings, Language language, Intents intents);
    ~UpdatesPage() override;

    // Queue the given packages (analysed, ordered by the planner). Returns how many were new.
    static std::size_t queuePackages(AppState& state, const std::vector<std::filesystem::path>& files,
                                     std::optional<core::UpdateKind> kind = std::nullopt);
    // D-152: the page's Sıfırla — every package it lists leaves the queue (one undo step); language
    // packs are the Diller page's and stay.
    static void resetQueued(AppState& state);
    void setDragState(ui::DropZone::DragState state);

    // D-139: the detail card.
    enum Action : int { kActionRemove = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    enum class Compat : std::uint8_t { Ok, WrongWindows, WrongArch, Unknown };
    // What a row shows, made in refresh() (the package's file was analysed on its first paint).
    struct RowText {
        ui::icons::Icon fileIcon = ui::icons::Icon::MsuFile;
        std::wstring file;
        std::wstring kind;
        std::wstring kb;
        std::wstring size;
        Compat compat = Compat::Unknown;
        std::wstring compatText;
    };
    std::vector<RowText> m_texts; // one per m_rows
    [[nodiscard]] std::wstring compatText(Compat compat, const core::UpdateInfo& info) const;
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
    class FetchStrip;
    ui::DropZone* m_drop = nullptr;
    FetchStrip* m_fetch = nullptr; // in the drop zone's place while the catalog is searched / downloaded
    SummaryLine* m_summaryLine = nullptr;
    float m_titleY = 0; // the header row's top (layout)
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
