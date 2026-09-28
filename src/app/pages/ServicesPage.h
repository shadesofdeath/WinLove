#pragma once
// P10 Servisler (docs/pages/10-services.md, screen 09): toolbar — search ("/"), Başlangıç filter,
// "Yalnızca değişenler" — and a TableView: Servis · Ad · Varsayılan · Yeni başlangıç · Risk.
// "Yeni başlangıç" opens a menu (Otomatik / Gecikmeli / El ile / Devre dışı); the choice is queued
// through ServiceController. Disabling a service others depend on shows a warning toast.
#include "app/Localization.h"
#include "app/controllers/ServiceController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <vector>

namespace wl::app {

class ServicesPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void(std::wstring title, std::wstring body)> warn;
    };
    ServicesPage(AppState& state, ServiceController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~ServicesPage() override;

    void focusSearch();
    [[nodiscard]] static Str startName(core::StartType start);

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

    AppState& m_state;
    ServiceController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<int> m_rows;
    std::wstring m_needle;
    int m_startFilter = 0; // 0 all, then kChoices order
    bool m_onlyChanged = false;
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_startBox = nullptr;
    ui::Toggle* m_changed = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
