#pragma once
// P04 Özellikler as the reference tool's page (D-111, research README §7.6): two tabs, "Talep Üzerine
// Özellikler on/n" (capabilities, under the reference tool's categories: Diller · Yazı Tipleri · Sürücüler ·
// RSAT · Diğer, from the capability id) and "İsteğe Bağlı Özellikler on/n" (optional features). One
// 23.5 row each: the check circle (ticked = it stays / will be on), the name, its description,
// "Etkin" (a green check when the image has it on now) and "Yüklendi" (the size of what is not
// installed). Unticking queues the disable / removal; nothing touches the image until "Uygula".
// Without a mounted image, while loading or on failure: an EmptyState.
#include "app/Localization.h"
#include "app/controllers/FeatureController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"

#include <set>

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
    void setAllExpanded(bool expanded);

    // The reference tool's capability categories, from the id ("Language.Fonts.Jpan~~~und-JPAN~0.0.1.0" → Fonts).
    enum class Category : std::uint8_t { Languages, Fonts, Drivers, Rsat, Other };
    [[nodiscard]] static Category categoryOf(const std::wstring& capabilityName);
    // D-114: "Language.Basic~~~tr-TR~0.0.1.0" → "tr-TR"; empty for no language capability.
    [[nodiscard]] static std::wstring languageOf(const std::wstring& capabilityName);
    [[nodiscard]] static std::wstring languageTitle(const std::wstring& tag); // "Türkçe (Türkiye) - Türkçe (Türkiye) - tr-TR"

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    enum class Filter : std::uint8_t { All, On, Off, Queued };
    struct Row {
        int category = -1; // a category row (capabilities tab), else -1
        int item = -1;     // index into optionalFeatures()->items
        int depth = 0;     // D-114: indent (language group, feature tree)
        std::wstring group; // D-114: a language group row ("tr-TR") under Diller
    };
    std::set<std::wstring> m_openGroups; // language groups open (closed by default, as the reference tool)
    void refresh(); // state changed: data, empty states, rows
    void refilter(); // search / filter / tab changed
    void updateTabs();
    void showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                   std::function<void()> onAction);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);
    [[nodiscard]] const core::OptionalFeature* itemAt(int row) const;
    [[nodiscard]] bool visible(const core::OptionalFeature& item) const;
    [[nodiscard]] std::wstring tooltipOf(int row) const;

    AppState& m_state;
    FeatureController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<Row> m_rows;
    std::set<int> m_collapsed; // capability categories closed
    bool m_capabilities = true; // the tab shown
    std::wstring m_needle;
    Filter m_filter = Filter::All;
    ui::TabBar* m_tabs = nullptr;
    ui::Dropdown* m_filterBox = nullptr;
    ui::SearchBox* m_search = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
