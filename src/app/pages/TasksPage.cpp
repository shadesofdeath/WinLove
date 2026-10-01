#include "app/pages/TasksPage.h"

#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
enum Column : int { kTask, kCategory, kRisk, kState };
} // namespace

TasksPage::TasksPage(AppState& state, TaskController& controller, const Localization& strings, Language language,
                     std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_search = &add<ui::SearchBox>(strings.get(Str::TasksSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::lower(text);
        rebuild();
    };
    std::vector<std::wstring> filters{strings.get(Str::CommonAll)};
    for (const auto& c : m_controller.categories()) {
        filters.push_back(c.name.get(language));
    }
    filters.push_back(strings.get(Str::TasksCustom));
    m_filter = &add<ui::Dropdown>(strings.get(Str::CommonCategory), std::move(filters), 0);
    m_filter->onChange = [this](int index) {
        m_category = index;
        rebuild();
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::TasksTask), 0},
        {strings.get(Str::CommonCategory), 200},
        {strings.get(Str::CommonRisk), 90},
        {strings.get(Str::CommonStatus), 170},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kTask && p.x < m_table->cellRect(row, kTask).x + ui::TableView::kCellPad + ui::Checkbox::kBox + 6) {
            activate(row);
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onSelect = [this](int) { invalidate(); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::QueueClock, strings.get(Str::TasksNoMountTitle),
                                   strings.get(Str::TasksNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::TasksTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::ImageValues) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            rebuild(); // a custom task may have come or gone
        }
    });
    refresh();
}

TasksPage::~TasksPage() {
    m_state.unsubscribe(m_subscription);
}

void TasksPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

bool TasksPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

std::wstring TasksPage::categoryName(const std::string& id) const {
    for (const auto& c : m_controller.categories()) {
        if (c.id == id) {
            return c.name.get(m_language);
        }
    }
    return m_strings.get(Str::TasksCustom);
}

void TasksPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_filter, m_table}) {
        w->setVisible(mounted);
    }
    rebuild();
    layout();
}

void TasksPage::rebuild() {
    m_all = m_controller.tasks();
    const auto& cats = m_controller.categories();
    m_rows.clear();
    for (std::size_t i = 0; i < m_all.size(); ++i) {
        const auto& t = m_all[i];
        if (m_category > 0) {
            const bool custom = m_category == static_cast<int>(cats.size()) + 1;
            if (custom ? t.category != "custom"
                       : t.category != cats[static_cast<std::size_t>(m_category - 1)].id) {
                continue;
            }
        }
        if (!m_needle.empty() && wl::text::lower(t.name.get(m_language)).find(m_needle) == std::wstring::npos &&
            wl::text::lower(t.path).find(m_needle) == std::wstring::npos) {
            continue;
        }
        m_rows.push_back(static_cast<int>(i));
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    invalidate();
}

void TasksPage::activate(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    m_controller.toggle(m_all[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])]);
}

void TasksPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& t = m_all[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
    switch (column) {
    case kTask: {
        const bool off = m_controller.off(t.path);
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               off ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell);
        const float x = rect.x + ui::Checkbox::kBox + 8;
        const std::wstring& name = t.name.get(m_language);
        const auto style = cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body;
        const float nameW = std::min(rect.right() - x, std::ceil(canvas.text().measure(name, style)));
        canvas.drawText(name, {x, rect.y, nameW, rect.height}, style, Color::TextPrimary);
        if (t.recommended) {
            const float rx = x + nameW + 8;
            canvas.drawText(m_strings.get(Str::TasksRecommended), {rx, rect.y, std::max(rect.right() - rx, 0.0f), rect.height},
                            TypeStyle::Caption, Color::TextTertiary);
        }
        break;
    }
    case kCategory: canvas.drawText(categoryName(t.category), rect, TypeStyle::Caption, Color::TextSecondary); break;
    case kRisk: paintRisk(canvas, rect, t.risk, m_strings); break;
    case kState: {
        const bool queued = m_controller.queued(t.path);
        const bool off = m_controller.off(t.path);
        const bool image = m_controller.inImage(t.path);
        Str text = off ? Str::TasksStateOff : Str::TasksStateOn;
        Color ink = Color::TextTertiary;
        if (queued) {
            text = off ? Str::TasksWillDisable : Str::TasksWillEnable;
            ink = Color::AccentBase;
        } else if (image) {
            text = Str::TasksInImage;
        }
        canvas.drawText(m_strings.get(text), rect, TypeStyle::Caption, ink);
        break;
    }
    default: break;
    }
}

void TasksPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    const float y = b.y + kToolbarTop;
    m_search->setWidth(240);
    m_search->setBounds({b.x, y, 240, kToolbar});
    const float fw = std::min(m_filter->measure({}).width, 260.0f);
    m_filter->setBounds({b.x + 240 + kGap, y, fw, kToolbar});
    const float top = y + kToolbar + 12;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top - kDetailLine, 0.0f)});
}

void TasksPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    // Summary right of the toolbar.
    int off = 0;
    for (const auto& t : m_all) {
        off += m_controller.off(t.path) ? 1 : 0;
    }
    const float left = m_filter->bounds().right() + kGap;
    canvas.drawText(m_strings.format(Str::TasksSummary, {{L"n", std::to_wstring(off)}, {L"total", std::to_wstring(m_all.size())}}),
                    {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
    const RectF detail{b.x, m_table->bounds().bottom(), b.width, kDetailLine};
    const int row = m_table->selected();
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        const auto& t = m_all[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
        const std::wstring& notes = t.notes.get(m_language);
        paintDetail(canvas, detail, t.path, notes.empty() ? m_strings.get(Str::TasksDetailHint) : notes);
    } else {
        paintDetail(canvas, detail, L"", m_strings.get(Str::TasksDetailHint));
    }
}

} // namespace wl::app
