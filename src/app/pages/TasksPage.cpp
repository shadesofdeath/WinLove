#include "app/pages/TasksPage.h"

#include "app/Format.h"
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
constexpr float kToolbarTop = 0.0f;
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr int kMarkSlots = 3; // D-139: state, risk, recommended
enum Column : int { kTask, kMarks, kCategory, kState };
} // namespace

TasksPage::TasksPage(AppState& state, TaskController& controller, const Localization& strings, Language language,
                     std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_search = &add<ui::SearchBox>(strings.get(Str::TasksSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
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
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::TasksTask), 0},
        {L"", marksColumnWidth(kMarkSlots)},
        {.title = strings.get(Str::CommonCategory), .width = 230, .minWidth = 160},
        {.title = strings.get(Str::CommonStatus), .width = 196, .minWidth = 120},
    });
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kTask || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return {};
        }
        return {m_all[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])].name.get(m_language), TypeStyle::Body,
                ui::Checkbox::kBox + 8};
    };
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        if (column != kMarks || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return {};
        }
        const auto& t = m_all[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
        const auto& state = m_states[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
        return marksTooltip(m_strings, marksOf(row), m_strings.get(state.off ? Str::TasksStateWillDisable : Str::TasksStateWillEnable), {},
                            t.notes.get(m_language));
    };
    m_table->setFlexMin(kNameColumnMin); // D-136: the task's name keeps its room at a narrow window
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kTask && p.x < m_table->cellRect(row, kTask).x + ui::TableView::kCellPad + ui::Checkbox::kBox + 6) {
            activate(row);
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onSelect = [this](int) {
        invalidate();
        detailChanged();
    };
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
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_filter, m_summaryLine, m_table}) {
        w->setVisible(mounted);
    }
    rebuild();
    layout();
    detailChanged();
}

void TasksPage::rebuild() {
    m_all = m_controller.tasks();
    m_states.clear();
    m_states.reserve(m_all.size());
    int offCount = 0;
    int queuedOff = 0;
    int queuedAll = 0;
    for (const auto& t : m_all) {
        const TaskState state{m_controller.off(t.path), m_controller.queued(t.path), m_controller.inImage(t.path)};
        offCount += state.off ? 1 : 0;
        queuedAll += state.queued ? 1 : 0;
        queuedOff += state.queued && state.off ? 1 : 0;
        m_states.push_back(state);
    }
    // D-139: what the list has, quietly, then what Uygula changes (in the accent).
    std::vector<SummaryPart> parts{{m_strings.format(Str::TasksSummary, {{L"n", std::to_wstring(offCount)}, {L"total", std::to_wstring(m_all.size())}})}};
    if (queuedAll > 0) {
        parts.push_back({m_strings.format(queuedOff == queuedAll ? Str::TasksSummaryOff : Str::TasksSummaryAny, {{L"n", std::to_wstring(queuedAll)}}),
                         SummaryPart::Tone::Change});
    }
    m_summaryLine->set(std::move(parts));
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
        if (!m_needle.empty() && wl::text::fold(t.name.get(m_language)).find(m_needle) == std::wstring::npos &&
            wl::text::fold(t.path).find(m_needle) == std::wstring::npos) {
            continue;
        }
        m_rows.push_back(static_cast<int>(i));
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    invalidate();
    detailChanged();
}

void TasksPage::activate(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const int index = m_rows[static_cast<std::size_t>(row)];
    m_controller.toggle(m_all[static_cast<std::size_t>(index)]);
    // rebuild() ran in the queue's notification: the user's tick draws in when the row is still
    // that task (a filter may have taken it out).
    if (row < static_cast<int>(m_rows.size()) && m_rows[static_cast<std::size_t>(row)] == index &&
        static_cast<std::size_t>(index) < m_states.size() && m_states[static_cast<std::size_t>(index)].off) {
        m_table->revealCheck(row);
    }
}

void TasksPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    if (index >= m_all.size() || index >= m_states.size()) {
        return;
    }
    const auto& t = m_all[index];
    const TaskState& state = m_states[index];
    switch (column) {
    case kTask: {
        const bool off = state.off;
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               off ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell, cell.checkReveal);
        // D-139: only the name; "önerilen" is a mark of its own column.
        const float x = rect.x + ui::Checkbox::kBox + 8;
        canvas.drawText(t.name.get(m_language), {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kMarks: paintMarks(canvas, rect, marksOf(row), kMarkSlots); break;
    case kCategory: canvas.drawText(categoryName(t.category), rect, TypeStyle::Caption, Color::TextSecondary); break;
    case kState: {
        const bool queued = state.queued;
        const bool off = state.off;
        const bool image = state.inImage;
        Str text = off ? Str::TasksStateOff : Str::TasksStateOn;
        Color ink = Color::TextTertiary;
        if (queued) {
            text = off ? Str::TasksWillDisable : Str::TasksWillEnable;
            ink = Color::AccentText;
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
    // D-139: the header row — search, Kategori, the summary at the right; the table; under it the selected
    // task's line, unless the detail card beside the page says it.
    const RectF b = bounds();
    m_empty->setBounds(b);
    const float y = b.y + kToolbarTop;
    const float sw = detailShown() ? 180.0f : 240.0f;
    m_search->setWidth(sw);
    m_search->setBounds({b.x, y, sw, kToolbar});
    const float fw = std::min(m_filter->measure({}).width, detailShown() ? 200.0f : 260.0f);
    m_filter->setBounds({b.x + sw + kGap, y, fw, kToolbar});
    const float sx = m_filter->bounds().right() + kHeaderGap;
    m_summaryLine->setBounds({sx, y, std::max(b.right() - sx, 0.0f), kToolbar});
    const float top = y + kToolbar + 12;
    const float line = detailShown() ? 0.0f : kDetailLine;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top - line, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kCategory});
}

void TasksPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    if (detailShown()) {
        return; // the selected task is the detail card's
    }
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

namespace wl::app {

RowMarks TasksPage::marksOf(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    if (index >= m_all.size() || index >= m_states.size()) {
        return {};
    }
    const auto& t = m_all[index];
    return {m_states[index].queued ? RowMark::Queued : RowMark::None, t.risk, t.recommended};
}

DetailModel TasksPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return model;
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    if (index >= m_all.size() || index >= m_states.size()) {
        return model;
    }
    const auto& t = m_all[index];
    const TaskState& state = m_states[index];
    model.icon = ui::icons::Icon::QueueClock;
    model.title = t.name.get(m_language);
    model.subtitle = categoryName(t.category);
    model.identity = t.path;
    if (state.queued) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(state.off ? Str::TasksStateWillDisable : Str::TasksStateWillEnable),
                       DetailTone::Change};
    } else if (state.inImage) {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::TasksStateInImage), DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::TasksCardOn), DetailTone::Quiet};
    }
    model.about = t.notes.get(m_language);
    if (model.about.empty()) {
        model.about = m_strings.get(Str::TasksDetailHint);
    }
    const Str word = state.queued ? (state.off ? Str::TasksWillDisable : Str::TasksWillEnable)
                     : state.inImage ? Str::TasksInImage
                     : state.off     ? Str::TasksStateOff
                                     : Str::TasksStateOn;
    model.facts.push_back({m_strings.get(Str::CommonStatus), m_strings.get(word), state.queued ? DetailTone::Change : DetailTone::Normal});
    model.facts.push_back({m_strings.get(Str::CommonRisk), m_strings.get(riskText(t.risk)),
                           t.risk == core::ops::Risk::Low ? DetailTone::Success : t.risk == core::ops::Risk::High ? DetailTone::Error : DetailTone::Warning,
                           false, true});
    model.facts.push_back({m_strings.get(Str::MarksRecommended), m_strings.get(t.recommended ? Str::CommonYes : Str::CommonNo)});
    model.actions.push_back({kActionToggle, m_strings.get(state.off ? Str::TasksActionOn : Str::TasksActionOff)});
    return model;
}

void TasksPage::detailAction(int id) {
    if (id == kActionToggle) {
        activate(m_table->selected()); // the row's own Space / double click
    }
}

} // namespace wl::app
