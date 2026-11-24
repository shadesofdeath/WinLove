#include "app/pages/ServicesPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "core/image/Services.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using core::ServiceEntry;
using core::StartType;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 0.0f;
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr float kTableGap = 12.0f;
// The start type's box in its cell: a small control (D-136: from the tokens), at most kChoiceW wide.
constexpr float kChoiceH = ui::tokens::size::controlSmall;
constexpr float kChoiceW = 176.0f;
constexpr float kIconGap = ui::tokens::spacing::s3; // the service's icon → its name

constexpr int kMarkSlots = 2; // D-139: state, risk

enum Column : int { kService, kMarks, kName, kDefault, kNew };

constexpr StartType kChoices[] = {StartType::Auto, StartType::AutoDelayed, StartType::Manual, StartType::Disabled};

// Boot/System services are listed under "Otomatik" in the filter (they start with Windows).
bool matchesFilter(StartType start, int filter) {
    if (filter == 0) {
        return true;
    }
    const StartType want = kChoices[filter - 1];
    if (want == StartType::Auto) {
        return start == StartType::Auto || start == StartType::Boot || start == StartType::System;
    }
    return start == want;
}
} // namespace

Str ServicesPage::startName(StartType start) {
    switch (start) {
    case StartType::Boot: return Str::ServicesStartBoot;
    case StartType::System: return Str::ServicesStartSystem;
    case StartType::Auto: return Str::ServicesStartAuto;
    case StartType::AutoDelayed: return Str::ServicesStartAutoDelayed;
    case StartType::Manual: return Str::ServicesStartManual;
    case StartType::Disabled: return Str::ServicesStartDisabled;
    }
    return Str::ServicesStartManual;
}

ServicesPage::ServicesPage(AppState& state, ServiceController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::ServicesSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        refilter();
    };
    std::vector<std::wstring> filters{strings.get(Str::CommonAll)};
    for (const auto start : kChoices) {
        filters.push_back(strings.get(startName(start)));
    }
    m_startBox = &add<ui::Dropdown>(strings.get(Str::ServicesStartFilter), std::move(filters), 0);
    m_startBox->onChange = [this](int index) {
        m_startFilter = index;
        refilter();
    };
    m_changed = &add<ui::Toggle>(strings.get(Str::CommonOnlyChanged), false);
    m_changed->onChange = [this](bool on) {
        m_onlyChanged = on;
        refilter();
    };
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::ServicesService), 0, ui::TextAlign::Leading, true},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::CommonName), 206, ui::TextAlign::Leading, true, 130},
        {strings.get(Str::ServicesDefault), 172, ui::TextAlign::Leading, true, 140},
        {.title = strings.get(Str::ServicesNewStart), .width = 212, .minWidth = kChoiceW + ui::tokens::spacing::s4},
    });
    // D-139: a mark says what it means on hover; the risk's word is there and in the card.
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        const auto* item = itemAt(row);
        if (column != kMarks || !item) {
            return {};
        }
        const RowMarks marks = marksOf(row);
        const std::wstring change =
            m_strings.format(Str::ServicesWillBecome, {{L"start", m_strings.get(startName(m_controller.target(*item)))}});
        return marksTooltip(m_strings, marks, change, explainGuard ? explainGuard(*item) : std::wstring(),
                            m_controller.notes(*item, m_language));
    };
    m_table->onSelect = [this](int) { detailChanged(); };
    // D-136: the service's name keeps its room at a narrow window; a cut name or id shows whole on hover.
    m_table->setFlexMin(kNameColumnMin);
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        const auto* item = itemAt(row);
        if (!item) {
            return {};
        }
        if (column == kService) {
            return {item->displayName, TypeStyle::BodyStrong, ui::tokens::size::icon + kIconGap};
        }
        return column == kName ? ui::TableCellText{item->name, TypeStyle::Mono} : ui::TableCellText{};
    };
    // D-091: sorting by a header click.
    m_table->sortKey = [this](int row, int column) -> ui::TableSortKey {
        const auto* item = itemAt(row);
        if (!item) {
            return {};
        }
        switch (column) {
        case kService: return {item->displayName};
        case kName: return {item->name};
        case kDefault: return {m_strings.get(startName(item->start))};
        default: return {};
        }
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kNew) {
            openChoice(row);
        }
    };
    m_table->onActivate = [this](int row) { openChoice(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::ServicesGear, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ServicesTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Services) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_onlyChanged || m_startFilter != 0) {
                refilter();
            } else {
                m_table->refresh();
                invalidate();
            }
            updateSummary();
            detailChanged();
        }
    });
    m_controller.load();
    refresh();
}

ServicesPage::~ServicesPage() {
    m_state.unsubscribe(m_subscription);
}

void ServicesPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

void ServicesPage::reveal(const std::wstring& name) {
    m_search->setText({});
    m_needle.clear();
    m_startBox->setSelected(0);
    m_startFilter = 0;
    m_changed->setOn(false, /*animated=*/false);
    m_onlyChanged = false;
    refilter();
    for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
        if (const auto* item = itemAt(row); item && item->name == name) {
            m_table->setSelected(row);
            if (host()) {
                host()->setFocus(m_table, /*visible=*/false);
            }
            return;
        }
    }
}

bool ServicesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

const ServiceEntry* ServicesPage::itemAt(int row) const {
    const auto& list = m_state.serviceList();
    if (!list || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    return &list->items[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
}

void ServicesPage::showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                             std::function<void()> onAction) {
    m_empty->setContent(icon, m_strings.get(title), std::move(body));
    if (action) {
        m_empty->setAction(m_strings.get(*action)).onInvoke = std::move(onAction);
    } else {
        m_empty->hideAction();
    }
    m_empty->setVisible(true);
}

void ServicesPage::refresh() {
    const auto& list = m_state.serviceList();
    const bool ready = list && list->status == AppState::ServiceList::Status::Ready;
    if (!m_state.mounted()) {
        showEmpty(ui::icons::Icon::ServicesGear, Str::ServicesNoMountTitle, m_strings.get(Str::ServicesNoMountBody),
                  Str::CommonGoImages, m_intents.goImages);
    } else if (!list || list->status == AppState::ServiceList::Status::Loading) {
        m_controller.load();
        // D-139: a quiet line under the page's icon while the list is read (no spinner that never turns).
        showEmpty(ui::icons::Icon::ServicesGear, Str::ServicesLoadingTitle, m_strings.get(Str::ServicesLoadingBody),
                  std::nullopt, {});
    } else if (list->status == AppState::ServiceList::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::ServicesFailedTitle,
                  list->error.message + L" — " + list->error.context, Str::CommonRetry,
                  [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_startBox, m_changed, m_summaryLine, m_table}) {
        w->setVisible(ready);
    }
    refilter();
    updateSummary();
    layout();
    invalidate();
    detailChanged();
}

void ServicesPage::updateSummary() {
    // "214 servis · 2 devre dışı bırakılacak" (or "· 3 değişecek" when not every change disables).
    const auto& list = m_state.serviceList();
    std::vector<SummaryPart> parts;
    if (list && list->status == AppState::ServiceList::Status::Ready) {
        parts.push_back({m_strings.format(Str::ServicesSummary, {{L"n", formatCount(list->items.size(), m_language)}})});
        std::size_t changed = 0;
        std::size_t disabled = 0;
        for (const auto& service : list->items) {
            if (m_controller.changed(service)) {
                ++changed;
                disabled += m_controller.target(service) == StartType::Disabled ? 1 : 0;
            }
        }
        if (changed > 0) {
            parts.push_back({m_strings.format(disabled == changed ? Str::ServicesSummaryDisable : Str::ServicesSummaryChange,
                                              {{L"n", formatCount(changed, m_language)}}),
                             SummaryPart::Tone::Change, m_strings.format(Str::ServicesSummaryChange, {{L"n", formatCount(changed, m_language)}})});
        }
    }
    m_summaryLine->set(std::move(parts));
}

RowMarks ServicesPage::marksOf(int row) {
    RowMarks marks;
    const auto* item = itemAt(row);
    if (!item || static_cast<std::size_t>(row) >= m_rowStates.size()) {
        return marks;
    }
    RowState& state = m_rowStates[static_cast<std::size_t>(row)];
    if (const std::uint64_t revision = m_state.revision(); state.at != revision) {
        state.at = revision;
        state.changed = m_controller.changed(*item);
        state.guarded = m_controller.guarded(*item);
        state.target = m_controller.target(*item);
        state.risk = m_controller.risk(*item);
    }
    marks.mark = state.changed ? RowMark::Queued : state.guarded ? RowMark::Held : RowMark::None;
    marks.risk = state.risk;
    return marks;
}

DetailModel ServicesPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    const auto* item = m_table->visible() ? itemAt(row) : nullptr;
    if (!item) {
        return model;
    }
    const StartType target = m_controller.target(*item);
    const bool changed = m_controller.changed(*item);
    const bool guarded = m_controller.guarded(*item);
    const auto risk = m_controller.risk(*item);
    model.icon = ui::icons::Icon::ServicesGear;
    model.title = item->displayName;
    model.identity = item->name;
    if (changed) {
        model.state = {ui::icons::Icon::QueueClock,
                       m_strings.format(Str::ServicesWillBecome, {{L"start", m_strings.get(startName(target))}}), DetailTone::Change};
    } else if (guarded) {
        model.state = {ui::icons::Icon::ShieldCheck, m_strings.get(Str::MarksHeld), DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::ServicesKeeps), DetailTone::Quiet};
    }
    // What it is: the image's own description, then the catalog's note.
    model.about = item->description.starts_with(L"@") ? std::wstring() : item->description;
    if (const std::wstring notes = m_controller.notes(*item, m_language); !notes.empty()) {
        model.about += (model.about.empty() ? L"" : L"\n") + notes;
    }
    model.facts.push_back({m_strings.get(Str::ServicesDefault), m_strings.get(startName(item->start))});
    model.facts.push_back({m_strings.get(Str::DetailAtApply), m_strings.get(startName(target)),
                           changed ? DetailTone::Change : DetailTone::Normal});
    model.facts.push_back({m_strings.get(Str::RiskColumn), m_strings.get(riskText(risk)),
                           risk == core::ops::Risk::Low ? DetailTone::Success : risk == core::ops::Risk::High ? DetailTone::Error : DetailTone::Warning,
                           false, true});
    if (!item->account.empty()) {
        model.facts.push_back({m_strings.get(Str::ServicesAccount), item->account});
    }
    if (!item->imagePath.empty()) {
        model.facts.push_back({m_strings.get(Str::DetailPath), item->imagePath, DetailTone::Normal, true});
    }
    if (guarded && explainGuard) {
        model.notes.push_back({ui::icons::Icon::ShieldCheck, explainGuard(*item) + L"\n" + m_strings.get(Str::CompatLockedHint)});
    }
    if (target == StartType::Disabled) {
        if (const auto dependents = m_controller.activeDependents(*item); !dependents.empty()) {
            model.notes.push_back({ui::icons::Icon::WarningTriangle,
                                   m_strings.format(Str::ServicesDependentsWarn, {{L"name", item->displayName},
                                                                                  {L"n", std::to_wstring(dependents.size())}}),
                                   DetailTone::Warning});
        }
    }
    if (!item->dependsOn.empty()) {
        model.lists.push_back({m_strings.get(Str::DetailDependsOn), item->dependsOn, true});
    }
    if (const auto& list = m_state.serviceList(); list) {
        if (auto users = core::dependentsOf(list->items, item->name); !users.empty()) {
            model.lists.push_back({m_strings.get(Str::DetailNeededBy), std::move(users), true});
        }
    }
    model.actions.push_back({kActionChoose, m_strings.get(Str::ServicesChooseStart)});
    if (changed) {
        model.actions.push_back({kActionRevert, m_strings.get(Str::ServicesRevert)});
    }
    return model;
}

void ServicesPage::detailAction(int id) {
    const int row = m_table->selected();
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    if (id == kActionChoose) {
        openChoice(row); // the cell's own menu, where the cell is
    } else if (id == kActionRevert) {
        const ServiceEntry service = *item; // a copy: the queue's notification rebuilds the rows
        choose(service, service.start);
    }
}

void ServicesPage::refilter() {
    m_rows.clear();
    const auto& list = m_state.serviceList();
    if (list && list->status == AppState::ServiceList::Status::Ready) {
        for (std::size_t i = 0; i < list->items.size(); ++i) {
            const auto& s = list->items[i];
            if (m_onlyChanged && !m_controller.changed(s)) {
                continue;
            }
            if (!matchesFilter(m_controller.target(s), m_startFilter)) {
                continue;
            }
            if (!m_needle.empty() && wl::text::fold(s.displayName).find(m_needle) == std::wstring::npos &&
                wl::text::fold(s.name).find(m_needle) == std::wstring::npos) {
                continue;
            }
            m_rows.push_back(static_cast<int>(i));
        }
    }
    m_rowStates.assign(m_rows.size(), RowState{});
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

RectF ServicesPage::choiceRect(RectF cell) const {
    return {cell.x, cell.y + std::round((cell.height - kChoiceH) / 2), std::min(cell.width - ui::tokens::spacing::s4, kChoiceW), kChoiceH};
}

void ServicesPage::openChoice(int row) {
    const auto* item = itemAt(row);
    if (!item || !host()) {
        return;
    }
    std::vector<std::wstring> names;
    int selected = -1;
    const StartType current = m_controller.target(*item);
    for (std::size_t i = 0; i < std::size(kChoices); ++i) {
        names.push_back(m_strings.get(startName(kChoices[i])));
        if (kChoices[i] == current) {
            selected = static_cast<int>(i);
        }
    }
    const std::wstring name = item->name;
    auto popup = std::make_unique<ui::MenuPopup>(
        choiceRect(m_table->cellRect(row, kNew)), std::move(names), std::max(selected, 0),
        [this, name](int index) {
            const auto& list = m_state.serviceList();
            if (!list) {
                return;
            }
            const auto it = std::ranges::find(list->items, name, &ServiceEntry::name);
            if (it != list->items.end()) {
                choose(*it, kChoices[static_cast<std::size_t>(index)]);
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false, /*owner=*/this);
}

void ServicesPage::choose(const ServiceEntry& service, StartType start) {
    if (!m_controller.set(service, start)) {
        return; // a compatibility guard keeps it (D-082): the controller said why
    }
    if (start == StartType::Disabled && m_intents.warn) {
        const auto dependents = m_controller.activeDependents(service);
        if (!dependents.empty()) {
            std::wstring list;
            for (std::size_t i = 0; i < dependents.size() && i < 6; ++i) {
                list += (i ? L", " : L"") + dependents[i];
            }
            if (dependents.size() > 6) {
                list += L" …";
            }
            m_intents.warn(m_strings.format(Str::ServicesDependentsWarn, {{L"name", service.displayName},
                                                                           {L"n", std::to_wstring(dependents.size())}}),
                           list);
        }
    }
}

void ServicesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    if (static_cast<std::size_t>(row) >= m_rowStates.size()) {
        return;
    }
    const RowMarks marks = marksOf(row); // brings the row's state up to date (cached per revision)
    const RowState& state = m_rowStates[static_cast<std::size_t>(row)];
    const bool changed = state.changed;
    switch (column) {
    case kMarks:
        paintMarks(canvas, rect, marks, kMarkSlots);
        break;
    case kService: {
        canvas.drawIcon(ui::icons::Icon::ServicesGear, {rect.x, rect.y + std::round((rect.height - ui::tokens::size::icon) / 2)},
                        Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + kIconGap;
        canvas.drawText(item->displayName, {x, rect.y, rect.right() - x, rect.height},
                        cell.selected || changed ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kName:
        canvas.drawText(item->name, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    case kDefault:
        canvas.drawText(m_strings.get(startName(item->start)), rect, TypeStyle::Body, Color::TextPrimary);
        break;
    case kNew: {
        const RectF box = choiceRect(rect);
        const Color border = changed ? Color::AccentBase : cell.hoveredCell ? Color::TextTertiary : Color::LineStrong;
        canvas.fillRoundRect(box, ui::tokens::radius::r2, Color::BgInput);
        canvas.strokeRoundRect(box, ui::tokens::radius::r2, border);
        const float chevronX = box.right() - 4 - ui::tokens::size::icon;
        float textRight = chevronX;
        if (state.guarded) {
            // D-082: a compatibility guard keeps it from being disabled.
            textRight -= ui::tokens::size::icon + 2;
            canvas.drawIcon(ui::icons::Icon::Lock, {textRight, box.y + (box.height - ui::tokens::size::icon) / 2},
                            Color::TextTertiary);
        }
        canvas.drawText(m_strings.get(startName(state.target)),
                        {box.x + 6, box.y, textRight - box.x - 8, box.height}, TypeStyle::Body, Color::TextPrimary);
        canvas.drawIcon(ui::icons::Icon::ChevronDown, {chevronX, box.y + (box.height - ui::tokens::size::icon) / 2},
                        Color::TextTertiary);
        break;
    }
    default: break;
    }
}

void ServicesPage::layout() {
    // D-139: the header row — search, Başlangıç, "Yalnızca değişenler", the summary at the right.
    const RectF b = bounds();
    const float y = b.y + kToolbarTop;
    float x = b.x;
    // Beside the detail card the row is short: the search box and the filter give the summary room.
    m_search->setWidth(detailShown() ? 160.0f : 240.0f);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_startBox, m_changed}) {
        const ui::SizeF size = w->measure({});
        const float width = w == m_startBox && detailShown() ? std::min(size.width, 180.0f) : size.width;
        w->setBounds({x, y, width, kToolbar});
        x += width + kGap;
    }
    m_summaryLine->setBounds({x, y, std::max(b.right() - x, 0.0f), kToolbar});
    const float top = y + kToolbar + kTableGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    // Beside the detail card the service's key name is the card's (its identity line).
    fitColumns(*m_table, b.width, detailShown(), {kName, kDefault});
    m_empty->setBounds(b);
}

void ServicesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::ServicesNoResults));
    }
}

} // namespace wl::app
