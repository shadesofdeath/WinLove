#pragma once
// P04 Özellikler (docs/pages/04-features.md, screen 05): toolbar — search ("/"), state filter,
// "Yalnızca değişenler" — and a TableView: Özellik · Tür · Durum · Hedef (toggle) · Boyut.
// Toggle clicks / Space queue changes through FeatureController; nothing touches the image
// until "Uygula". Without a mounted image, while loading or on failure: an EmptyState.
#include "app/Localization.h"
#include "app/controllers/FeatureController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <vector>

namespace wl::app {

class FeaturesPage : public ui::Widget {
public:
    FeaturesPage(AppState& state, FeatureController& controller, const Localization& strings, Language language,
                 std::function<void()> goToImages);
    ~FeaturesPage() override;

    void focusSearch();
    // Command palette: drops the filters, selects the feature's row and scrolls to it.
    void reveal(const std::wstring& name);
    [[nodiscard]] static Str statusName(FeatureController::Status status);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    enum class Filter : std::uint8_t { All, On, Off, Queued };
    void refresh();         // state changed: data, empty states, rows
    void refilter();        // search / filter changed
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                   std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] const core::OptionalFeature* itemAt(int row) const;

    AppState& m_state;
    FeatureController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<int> m_rows; // indexes into optionalFeatures()->items, after filtering
    std::wstring m_needle;
    Filter m_filter = Filter::All;
    bool m_onlyChanged = false;
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_stateBox = nullptr;
    ui::Toggle* m_changed = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
