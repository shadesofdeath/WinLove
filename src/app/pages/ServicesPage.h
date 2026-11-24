#pragma once
// P10 Servisler (docs/pages/10-services.md, screen 09): toolbar — search ("/"), Başlangıç filter,
// "Yalnızca değişenler" — and a TableView: Servis · Ad · Varsayılan · Yeni başlangıç · Risk.
// "Yeni başlangıç" opens a menu (Otomatik / Gecikmeli / El ile / Devre dışı); the choice is queued
// through ServiceController. Disabling a service others depend on shows a warning toast.
// D-139: the header row's summary ("214 servis · 2 devre dışı bırakılacak"), a marks column after the
// service (queued / held by a guard / risk dot — the Risk column's word went to its tooltip and the
// card), the selected service in the detail card (its description, start types, account, file, what
// it needs and what needs it); beside the card the key-name column is left out.
#include "app/Localization.h"
#include "app/controllers/ServiceController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class ServicesPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void(std::wstring title, std::wstring body)> warn;
    };
    ServicesPage(AppState& state, ServiceController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~ServicesPage() override;

    void focusSearch();
    // Command palette: drops the filters, selects the service's row and scrolls to it.
    void reveal(const std::wstring& name);
    [[nodiscard]] static Str startName(core::StartType start);
    // D-082 / D-139: what a guard keeping the service from being disabled says (the shell's guards).
    std::function<std::wstring(const core::ServiceEntry&)> explainGuard;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    // D-139: the detail card.
    enum Action : int { kActionChoose = 1, kActionRevert };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    void refresh();
    void refilter();
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                   std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] const core::ServiceEntry* itemAt(int row) const;
    [[nodiscard]] ui::RectF choiceRect(ui::RectF cell) const;
    void openChoice(int row);
    void choose(const core::ServiceEntry& service, core::StartType start);
    void updateSummary();
    [[nodiscard]] RowMarks marksOf(int row);

    AppState& m_state;
    ServiceController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<int> m_rows;
    // What a row shows of the queue, worked out the first time it is drawn after a state change
    // (audit PG-13: the guard check and three queue lookups ran per visible row per frame).
    struct RowState {
        std::uint64_t at = ~std::uint64_t{0}; // AppState::revision() it was worked out at
        bool changed = false;
        bool guarded = false;
        core::StartType target = core::StartType::Manual;
        core::ops::Risk risk = core::ops::Risk::Medium;
    };
    std::vector<RowState> m_rowStates; // one per m_rows
    std::wstring m_needle;
    int m_startFilter = 0; // 0 all, then kChoices order
    bool m_onlyChanged = false;
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_startBox = nullptr;
    ui::Toggle* m_changed = nullptr;
    SummaryLine* m_summaryLine = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
