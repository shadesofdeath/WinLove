#pragma once
// D-154 Grup İlkesi (docs/pages/23-policies.md): the image's Administrative Templates as one table —
// search ("/"), Kapsam (Bilgisayar / Kullanıcı), the root category, "Yalnızca değişenler"; columns
// İlke · marks · Kategori · Kapsam · Durum. Durum opens a menu (Yapılandırılmadı / Etkin / Devre dışı);
// Etkin on a policy with options opens them (the shell's dialog), as gpedit does. The detail card
// says what the policy does (its explain text), where it is, what it writes.
#include "app/Localization.h"
#include "app/controllers/PolicyController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class PoliciesPage : public ui::Widget, public DetailSource {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void(std::wstring title, std::wstring body)> warn;
        std::function<void(PolicyController::Row row)> editOptions; // the options dialog (Etkin)
    };
    PoliciesPage(AppState& state, PolicyController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~PoliciesPage() override;

    void focusSearch();
    void selectRow(int row); // render demos, tests
    [[nodiscard]] static Str stateName(core::policy::PolicyState state);
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests
    [[nodiscard]] int rowCount() const noexcept { return static_cast<int>(m_rows.size()); } // tests
    void search(const std::wstring& text); // tests, the command palette

    enum Action : int { kActionChoose = 1, kActionOptions, kActionRevert };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    void refresh();
    void rebuildIndex();
    void refilter();
    void updateSummary();
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action, std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] const PolicyController::Row* rowAt(int row) const;
    [[nodiscard]] ui::RectF choiceRect(ui::RectF cell) const;
    void openChoice(int row);
    void choose(PolicyController::Row row, core::policy::PolicyState state);
    [[nodiscard]] RowMarks marksOf(int row);

    AppState& m_state;
    PolicyController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    // Per catalog row (PolicyController::rows()), worked out once per catalog: the folded text a
    // search looks in, and the root category (index into m_roots).
    const core::policy::PolicyCatalog* m_indexed = nullptr;
    std::vector<std::wstring> m_haystack;
    std::vector<int> m_rootOf;
    std::vector<std::wstring> m_rootIds;   // the filter's roots, by name
    std::vector<int> m_rows;               // shown: indexes into PolicyController::rows()
    struct RowState {
        std::uint64_t at = ~std::uint64_t{0};
        bool changed = false;
        core::policy::PolicyState state = core::policy::PolicyState::NotConfigured;
    };
    std::vector<RowState> m_rowStates;
    std::wstring m_needle;
    int m_scope = 0; // 0 both, 1 machine, 2 user
    int m_root = 0;  // 0 all, then m_rootIds + 1
    bool m_onlyChanged = false;
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_scopeBox = nullptr;
    ui::Dropdown* m_rootBox = nullptr;
    ui::Toggle* m_changed = nullptr;
    SummaryLine* m_summaryLine = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
