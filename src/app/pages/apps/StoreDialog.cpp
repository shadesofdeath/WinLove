#include "app/pages/apps/StoreDialog.h"

#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <windows.h>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kWidth = 680.0f;
constexpr int kVisibleRows = 10;
constexpr float kStatus = 20.0f;

class Picker : public ui::Widget, public StoreDialogHandle {
public:
    Picker(const Localization& strings, StoreDialogActions& actions) : m_strings(strings), m_actions(actions) {
        m_search = &add<ui::SearchBox>(strings.get(Str::AppsStoreSearch));
        m_search->onSubmit = [this] {
            if (!m_search->text().empty() && m_actions.search) {
                m_actions.search(m_search->text());
            }
        };
        m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
            {strings.get(Str::AppsStoreColApp), 0},
            {strings.get(Str::AppsStoreColPublisher), 220},
            {strings.get(Str::AppsStoreColId), 130},
        });
        m_table->setAccessible(ui::AccessRole::Group, strings.get(Str::AppsStoreTitle));
        m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
            if (row < 0 || row >= static_cast<int>(m_results.size())) {
                return;
            }
            const auto& r = m_results[static_cast<std::size_t>(row)];
            switch (column) {
            case 0: c.drawText(r.name, rect, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary); break;
            case 1: c.drawText(r.publisher, rect, TypeStyle::Caption, Color::TextSecondary); break;
            case 2: c.drawText(r.productId, rect, TypeStyle::Mono, Color::TextTertiary); break;
            default: break;
            }
        };
        m_table->onSelect = [this](int) {
            if (onSelectionChanged) {
                onSelectionChanged();
            }
        };
        m_table->onActivate = [this](int) {
            if (onActivate) {
                onActivate();
            }
        };
    }
    std::function<void()> onSelectionChanged;
    std::function<void()> onActivate;

    [[nodiscard]] ui::SearchBox* search() const { return m_search; }
    [[nodiscard]] const core::StoreSearchResult* selected() const {
        const int row = m_table->selected();
        return row >= 0 && row < static_cast<int>(m_results.size()) ? &m_results[static_cast<std::size_t>(row)] : nullptr;
    }

    void setResults(const std::wstring& query, std::vector<core::StoreSearchResult> results) override {
        m_searching = false;
        m_query = query;
        m_results = std::move(results);
        m_table->setRowCount(static_cast<int>(m_results.size()));
        if (!m_results.empty()) {
            m_table->setSelected(0);
        } else {
            m_table->clearSelection();
        }
        m_table->refresh();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        invalidate();
    }
    void setSearching(bool searching) override {
        m_searching = searching;
        invalidate();
    }

    void layout() override {
        const RectF b = bounds();
        m_search->setBounds({b.x, b.y, 320, ui::tokens::size::control});
        m_table->setBounds({b.x, b.y + ui::tokens::size::control + 8, b.width,
                            ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows + 1});
    }
    void paint(ui::Canvas& canvas) override {
        const RectF t = m_table->bounds();
        std::wstring status;
        Color ink = Color::TextTertiary;
        if (m_searching) {
            status = m_strings.get(Str::AppsStoreSearching);
        } else if (!m_query.empty() && m_results.empty()) {
            status = m_strings.format(Str::AppsStoreNone, {{L"query", m_query}});
        } else if (m_query.empty()) {
            status = m_strings.get(Str::AppsStoreHint);
        }
        if (!status.empty() && m_results.empty()) {
            canvas.drawText(status, {t.x, t.y + ui::TableView::kHeader, t.width, ui::TableView::kRow}, TypeStyle::Caption, ink,
                            ui::TextAlign::Center);
        }
        canvas.drawText(m_strings.get(Str::AppsStoreNote), {t.x, t.bottom() + 6, t.width, kStatus}, TypeStyle::Caption, Color::TextTertiary);
    }
    [[nodiscard]] static float height() {
        return ui::tokens::size::control + 8 + ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows + 1 + 6 + kStatus;
    }

private:
    const Localization& m_strings;
    StoreDialogActions& m_actions;
    ui::SearchBox* m_search = nullptr;
    ui::TableView* m_table = nullptr;
    std::vector<core::StoreSearchResult> m_results;
    std::wstring m_query;
    bool m_searching = false;
};

} // namespace

StoreDialog makeStoreDialog(const Localization& strings, std::wstring architecture, StoreDialogActions actions) {
    auto dialog = std::make_unique<ui::Dialog>(strings.get(Str::AppsStoreTitle),
                                               strings.format(Str::AppsStoreBody, {{L"arch", architecture}}),
                                               ui::icons::Icon::Download, Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto shared = std::make_shared<StoreDialogActions>(std::move(actions));
    auto* picker = &raw->setContent<Picker>(Picker::height(), strings, *shared);
    raw->addButton(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), shared->close);
    auto* primary = &raw->addButton(
        ui::ButtonKind::Primary, strings.get(Str::AppsStoreInstall),
        [picker, shared] {
            const auto* app = picker->selected();
            if (!app) {
                return;
            }
            const auto chosen = *app;
            shared->close();
            if (shared->install) {
                shared->install(chosen);
            }
        },
        /*primary=*/true);
    primary->setEnabled(false);
    picker->onSelectionChanged = [picker, primary] { primary->setEnabled(picker->selected() != nullptr); };
    picker->onActivate = [primary] {
        if (primary->enabled() && primary->onInvoke) {
            primary->onInvoke();
        }
    };
    raw->onCancel = shared->close;
    // The actions live as long as the primary button's handler (it holds `shared`; the picker refers to it).
    return StoreDialog{std::move(dialog), picker->search(), picker};
}

} // namespace wl::app
