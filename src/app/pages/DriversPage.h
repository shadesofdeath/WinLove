#pragma once
// P09 Sürücüler (docs/pages/09-drivers.md, screen 07): INF packages from scanned folders as a
// class → INF tri-state tree (Ad · Sağlayıcı · Sürüm · Boyut). Checked INFs are queued as AddDriver.
// Toolbar: search ("/"), Sınıf, Mimari (defaults to the mounted edition's architecture).
// Second tab "İmajdaki sürücüler" (D-052): the image's third-party drivers — Sürücü (checkbox =
// remove) · Sınıf · Sağlayıcı · Sürüm · Tarih; a boot-critical one says so.
// Third tab "Kurulum ortamı (boot.wim)" (D-056, D-134): the same tree, the tick = into boot.wim's Setup
// image at ISO / USB time. A row says what it is for Setup (storage / wired network / Wi-Fi, no use
// there / a virtual adapter); one of another architecture or one boot.wim has already is dimmed with
// its reason (tooltip). The packages the medium's boot.wim carries come first, as a group of their own.
// The line under the tree: when the drivers go in, or a storage driver the installed Windows lacks.
// D-139: every tab's header row has its summary at the right ("1 klasör · 10 INF · 104 MB · 3 eklenecek
// · 40 MB"); class rows in semibold; on "İmajdaki sürücüler" the "kaldırılacak" / "önyükleme için kritik"
// words appended to the name became marks of their own column (queued / the risk's dot); the selected
// INF or driver in the detail card (beside it the provider / version / date columns are the card's).
#include "app/Localization.h"
#include "app/controllers/ImageDriverController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "app/state/AppState.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wl::app {

class DriversPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> scanFolder;
        std::function<void()> goImages;
    };
    DriversPage(AppState& state, ImageDriverController& images, const Localization& strings, Language language,
                Intents intents);
    ~DriversPage() override;

    void focusSearch();
    void showImageTab(); // render demo: the second tab without reading DISM
    void showBootTab();  // render demo: the third tab
    [[nodiscard]] bool bootTabShown() const { return bootTab(); } // D-134: "Bu bilgisayardan al" ticks for boot.wim
    void setArchFilterForDemo(int index); // render demo: every architecture listed (the dimmed ARM64 row)
    // Friendly class name ("Ağ (Net)") for the common setup classes, else the class itself.
    [[nodiscard]] static std::wstring className(const std::wstring& cls, const Localization& strings);

    // D-139: the detail card and the header row's summary.
    enum Action : int { kActionToggle = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override;
    void detailAction(int id) override;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    struct Group {
        bool media = false; // D-134: the medium's boot.wim packages (rows: Row::media)
        std::wstring cls;
        std::vector<int> infs; // indexes into driverScan().infs (visible after filters)
        int total = 0;
        std::uint64_t size = 0;
    };
    struct Row {
        int group = 0;
        int inf = -1;   // index into driverScan().infs; -1 = group row (or a media row)
        int media = -1; // D-134: index into the medium's boot.wim drivers
        // What the row draws, worked out the first time it is drawn (audit PG-12: a class row looked
        // every one of its INFs up in the queue, and every row built its texts, on every frame).
        // The texts hold for the row's life (rebuild() makes new rows); the check box until the
        // state changes next.
        bool texts = false;
        std::wstring name;     // the class's name / the INF's file name
        std::wstring detail;   // "n INF" / "provider · version"
        std::wstring sizeText;
        std::uint64_t checkAt = ~std::uint64_t{0}; // AppState::revision() `check` is good for
        std::uint8_t check = 0;                    // ui::CheckState
        // D-134 (boot tab), with the check: a row that cannot be ticked, and the word at its right.
        bool locked = false;
        std::wstring tag;
        ui::tokens::Color tagInk = ui::tokens::Color::TextTertiary;
    };
    void updateSummary(); // the header row's line (state notifications, tab changes; never paint)
    void toggleImageRow(int row);
    [[nodiscard]] RowMarks imageMarks(int row) const;
    void refresh();
    void rebuild();
    // D-134: why scanned INF `inf` cannot be ticked for boot.wim (another architecture / there already).
    [[nodiscard]] std::optional<Str> lockOf(int inf) const;
    [[nodiscard]] const std::vector<core::WimDriver>* mediaBoot() const; // nullptr: not read (yet)
    [[nodiscard]] std::wstring tooltipOf(int row) const;
    void syncBootDetail(); // the line under the boot tab's tree (refresh(), queue changes; never paint)
    std::wstring m_bootDetail;
    bool m_bootDetailWarn = false;
    std::wstring m_bootNote;         // what the last tick did besides (a storage driver queued for the image)
    core::Architecture m_mediaArch = core::Architecture::Unknown;
    std::size_t m_classesFor = ~std::size_t{0}; // the scan the class list was made from (its INF count)
    [[nodiscard]] bool queued(int inf) const;
    void toggle(int inf);
    void toggleGroup(const Group& group);
    void toggleRow(int row); // the user's click / Space on a row: its tick draws in
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void paintImageCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] bool imageTab() const { return m_tabs->selected() == 1; }
    [[nodiscard]] bool bootTab() const { return m_tabs->selected() == 2; } // D-056: same tree, checkbox = boot.wim

    AppState& m_state;
    ImageDriverController& m_images;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<Group> m_groups;
    std::vector<Row> m_rows;
    std::set<std::wstring> m_collapsed;
    std::wstring m_needle;
    int m_classFilter = 0;
    int m_archFilter = 0; // 0 all, 1 x64, 2 arm64, 3 x86
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_class = nullptr;
    ui::Dropdown* m_arch = nullptr;
    SummaryLine* m_summaryLine = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
    ui::TabBar* m_tabs = nullptr;
    ui::TableView* m_imageTable = nullptr;
};

} // namespace wl::app
