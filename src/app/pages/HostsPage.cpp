#include "app/pages/HostsPage.h"
#include "app/pages/PageHeader.h"

#include "app/pages/PageBits.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 0.0f;
constexpr float kSummary = 28.0f;
constexpr int kMarkSlots = 3; // 2c: queued, risk, recommended (as Görevler)
enum Column : int { kList, kMarks, kEntries, kState };
} // namespace

HostsPage::HostsPage(AppState& state, HostsController& controller, const Localization& strings, Language language,
                     std::function<void()> goImages, std::function<void()> importFile)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_importFile(std::move(importFile)) {
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::HostsList), 0},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::HostsEntries), 126, ui::TextAlign::Trailing},
        {strings.get(Str::CommonStatus), 196},
    });
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kList || row < 0 || row > static_cast<int>(m_controller.lists().size())) {
            return {};
        }
        return {isCustomRow(row) ? m_strings.get(Str::HostsCustom) : m_controller.lists()[static_cast<std::size_t>(row)].name.get(m_language),
                TypeStyle::Body, ui::Checkbox::kBox + 8};
    };
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        if (column != kMarks || row < 0 || row >= static_cast<int>(m_rows.size())) {
            return {};
        }
        return marksTooltip(m_strings, marksOf(row), m_strings.get(stateWord(row)), {});
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kList && p.x < m_table->cellRect(row, kList).x + ui::TableView::kCellPad + ui::Checkbox::kBox + 6) {
            activate(row);
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onSelect = [this](int) {
        invalidate();
        detailChanged();
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        if (key.virtualKey == VK_DELETE && isCustomRow(m_table->selected())) {
            m_controller.clearCustom();
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Network, strings.get(Str::HostsNoMountTitle), strings.get(Str::HostsNoMountBody));
    m_summaryLine = &add<SummaryLine>();
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::HostsTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::ImageValues || change == AppState::Change::Queue) {
            refresh();
        }
    });
    refresh();
}

HostsPage::~HostsPage() {
    m_state.unsubscribe(m_subscription);
}

bool HostsPage::isCustomRow(int row) const {
    return row == static_cast<int>(m_controller.lists().size());
}

void HostsPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_table->setVisible(mounted);
    m_table->setRowCount(static_cast<int>(m_controller.lists().size()) + 1);
    // 2c: each row's state once per change, for the marks, the Durum column and the card.
    m_rows.clear();
    m_rows.reserve(m_controller.lists().size() + 1);
    for (const auto& list : m_controller.lists()) {
        const auto* queued = m_state.changes().find(core::ops::OpKind::SetHosts, list.id);
        m_rows.push_back({m_controller.on(list), queued != nullptr, queued && queued->value.empty(), m_controller.inImage(list)});
    }
    {
        const auto* queued = m_state.changes().find(core::ops::OpKind::SetHosts, HostsController::kCustom);
        m_rows.push_back({!m_controller.customEntries().empty(), queued != nullptr, queued && queued->value.empty(),
                          m_controller.customInImage()});
    }
    // D-139: what Uygula will block, at the header row's right in the accent (every page's summary).
    const auto entries = m_controller.totalEntries();
    m_summaryLine->set({{m_strings.format(Str::HostsSummary, {{L"n", std::to_wstring(entries)}}),
                         entries > 0 ? SummaryPart::Tone::Change : SummaryPart::Tone::Quiet}});
    m_summaryLine->setVisible(mounted);
    m_table->refresh();
    layout();
    invalidate();
    detailChanged();
}

void HostsPage::activate(int row) {
    if (row < 0) {
        return;
    }
    if (isCustomRow(row)) {
        if (m_controller.customEntries().empty()) {
            if (m_importFile) {
                m_importFile();
            }
        } else {
            m_controller.clearCustom();
        }
        return;
    }
    if (row < static_cast<int>(m_controller.lists().size())) {
        const auto& list = m_controller.lists()[static_cast<std::size_t>(row)];
        m_controller.toggle(list);
        if (m_controller.on(list)) {
            m_table->revealCheck(row); // the user's tick draws in
        }
    }
}

RowMarks HostsPage::marksOf(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    RowMarks marks;
    marks.mark = m_rows[static_cast<std::size_t>(row)].queued ? RowMark::Queued : RowMark::None;
    if (!isCustomRow(row)) {
        const auto& list = m_controller.lists()[static_cast<std::size_t>(row)];
        marks.risk = list.risk;
        marks.recommended = list.recommended;
    }
    return marks;
}

Str HostsPage::stateWord(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return Str::HostsStateOff;
    }
    const RowState& s = m_rows[static_cast<std::size_t>(row)];
    return s.queued ? (s.removing ? Str::HostsWillRemove : Str::HostsWillWrite) : s.inImage ? Str::HostsInImage : Str::HostsStateOff;
}

void HostsPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const bool custom = isCustomRow(row);
    const HostsList* list = custom ? nullptr : &m_controller.lists()[static_cast<std::size_t>(row)];
    const RowState& state = m_rows[static_cast<std::size_t>(row)];
    switch (column) {
    case kList: {
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               state.on ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell, cell.checkReveal);
        const float x = rect.x + ui::Checkbox::kBox + 8;
        const std::wstring& name = custom ? m_strings.get(Str::HostsCustom) : list->name.get(m_language);
        canvas.drawText(name, {x, rect.y, rect.right() - x, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    case kMarks: paintMarks(canvas, rect, marksOf(row), kMarkSlots); break;
    case kEntries: {
        const std::size_t n = custom ? m_controller.customEntries().size() : list->entries.size();
        canvas.drawText(std::to_wstring(n), rect, TypeStyle::Mono, n ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
        break;
    }
    case kState:
        canvas.drawText(m_strings.get(stateWord(row)), rect, TypeStyle::Caption, state.queued ? Color::AccentText : Color::TextTertiary);
        break;
    default: break;
    }
}

void HostsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_summaryLine->setBounds({b.x, b.y + kTop, b.width, kSummary});
    const float top = b.y + kTop + kSummary + 8;
    const float line = detailShown() ? 0.0f : kDetailLine; // beside the card the line under the table is the card's
    m_table->setBounds({b.x, top, b.width, std::min(ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(m_table->rowCount()) + 1,
                                                    std::max(b.bottom() - top - line, 0.0f))});
    fitColumns(*m_table, b.width, detailShown(), {kEntries});
}

void HostsPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible() || detailShown()) {
        return;
    }
    const RectF b = bounds();
    const RectF detail{b.x, m_table->bounds().bottom() + 4, b.width, kDetailLine};
    const int row = m_table->selected();
    if (isCustomRow(row)) {
        paintDetail(canvas, detail, L"", m_strings.get(Str::HostsCustomHint));
    } else if (row >= 0 && row < static_cast<int>(m_controller.lists().size())) {
        const auto& list = m_controller.lists()[static_cast<std::size_t>(row)];
        paintDetail(canvas, detail, L"", list.description.get(m_language));
    } else {
        paintDetail(canvas, detail, L"", m_strings.get(Str::HostsHint));
    }
}

DetailModel HostsPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return model;
    }
    const bool custom = isCustomRow(row);
    const RowState& state = m_rows[static_cast<std::size_t>(row)];
    const auto& entries = custom ? m_controller.customEntries() : m_controller.lists()[static_cast<std::size_t>(row)].entries;
    const std::wstring count = std::to_wstring(entries.size());
    model.icon = custom ? ui::icons::Icon::Import : ui::icons::Icon::Network;
    model.title = custom ? m_strings.get(Str::HostsCustom) : m_controller.lists()[static_cast<std::size_t>(row)].name.get(m_language);
    model.subtitle = m_strings.format(custom ? Str::HostsCardCustomSubtitle : Str::HostsCardSubtitle, {{L"n", count}});
    if (state.queued) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(state.removing ? Str::HostsStateWillRemove : Str::HostsStateWillWrite),
                       DetailTone::Change};
    } else if (state.inImage) {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::HostsStateInImage), DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Remove, m_strings.get(Str::HostsStateNotSelected), DetailTone::Quiet};
    }
    if (custom) {
        model.about = m_strings.get(entries.empty() ? Str::HostsCustomEmpty : Str::HostsCustomHint);
    } else {
        model.about = m_controller.lists()[static_cast<std::size_t>(row)].description.get(m_language);
    }
    model.facts.push_back({m_strings.get(Str::HostsEntries), count});
    model.facts.push_back({m_strings.get(Str::CommonStatus), m_strings.get(stateWord(row)), state.queued ? DetailTone::Change : DetailTone::Normal});
    if (!custom) {
        const auto& list = m_controller.lists()[static_cast<std::size_t>(row)];
        model.facts.push_back({m_strings.get(Str::CommonRisk), m_strings.get(riskText(list.risk)),
                               list.risk == core::ops::Risk::Low    ? DetailTone::Success
                               : list.risk == core::ops::Risk::High ? DetailTone::Error
                                                                    : DetailTone::Warning,
                               false, true});
        model.facts.push_back({m_strings.get(Str::MarksRecommended), m_strings.get(list.recommended ? Str::CommonYes : Str::CommonNo)});
    }
    // What the table only counts: the names the list sends to 0.0.0.0 (the card shows what fits).
    if (!entries.empty()) {
        DetailList names{m_strings.get(Str::HostsCardDomains), {}, true};
        names.lines.reserve(entries.size());
        for (const auto& entry : entries) {
            names.lines.push_back(entry.name);
        }
        model.lists.push_back(std::move(names));
    }
    const Str action = custom ? (entries.empty() ? Str::HostsImport : Str::HostsActionClear)
                              : (state.on ? Str::HostsActionUnblock : Str::HostsActionBlock);
    model.actions.push_back({kActionToggle, m_strings.get(action)});
    return model;
}

void HostsPage::detailAction(int id) {
    if (id == kActionToggle) {
        activate(m_table->selected()); // the row's own click / Space
    }
}

} // namespace wl::app
