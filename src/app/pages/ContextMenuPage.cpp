#include "app/pages/ContextMenuPage.h"

#include "app/Format.h"
#include "base/Text.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using core::ContextMenuEntry;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr float kTableGap = 12.0f;
constexpr float kIconGap = ui::tokens::spacing::s3;
constexpr int kMarkSlots = 1;

enum Column : int { kItem, kMarks, kPlaces, kKind, kState };
} // namespace

Str ContextMenuPage::placeName(std::wstring_view place) {
    using text::iequals;
    if (place == L"*") {
        return Str::ContextMenuPlaceFiles;
    }
    if (iequals(place, L"AllFilesystemObjects")) {
        return Str::ContextMenuPlaceAll;
    }
    if (iequals(place, L"Directory")) {
        return Str::ContextMenuPlaceDirectory;
    }
    if (iequals(place, L"Folder")) {
        return Str::ContextMenuPlaceFolder;
    }
    if (iequals(place, L"Directory\\Background")) {
        return Str::ContextMenuPlaceBackground;
    }
    if (iequals(place, L"Drive")) {
        return Str::ContextMenuPlaceDrive;
    }
    if (iequals(place, L"DesktopBackground")) {
        return Str::ContextMenuPlaceDesktop;
    }
    return Str::AppName; // the caller shows the place itself
}

std::wstring ContextMenuPage::placesText(const std::vector<std::wstring>& places) const {
    std::wstring out;
    const auto types = std::ranges::count_if(places, [](const std::wstring& p) { return p.starts_with(L"."); });
    if (types > 3) {
        out = m_strings.format(Str::ContextMenuPlaceTypes, {{L"n", std::to_wstring(types)}});
    }
    for (const auto& place : places) {
        if (types > 3 && place.starts_with(L".")) {
            continue;
        }
        const Str name = placeName(place);
        const std::wstring text = name == Str::AppName ? place : m_strings.get(name);
        if (out.find(text) == std::wstring::npos) {
            out += (out.empty() ? L"" : L", ") + text;
        }
    }
    return out;
}

Str ContextMenuPage::kindName(const ContextMenuEntry& entry) const {
    switch (entry.kind) {
    case ContextMenuEntry::Kind::Verb: return Str::ContextMenuKindVerb;
    case ContextMenuEntry::Kind::Handler: return Str::ContextMenuKindHandler;
    case ContextMenuEntry::Kind::Packaged: return Str::ContextMenuKindPackaged;
    }
    return Str::ContextMenuKindVerb;
}

ContextMenuPage::ContextMenuPage(AppState& state, ContextMenuController& controller, const Localization& strings,
                                 Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::ContextMenuSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        refilter();
    };
    m_changed = &add<ui::Toggle>(strings.get(Str::CommonOnlyChanged), false);
    m_changed->onChange = [this](bool on) {
        m_onlyChanged = on;
        refilter();
    };
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::ContextMenuColItem), 0, ui::TextAlign::Leading, true},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::ContextMenuColPlaces), 230, ui::TextAlign::Leading, true, 140},
        {strings.get(Str::ContextMenuColKind), 140, ui::TextAlign::Leading, true, 100},
        {strings.get(Str::ContextMenuColState), 110, ui::TextAlign::Leading, true, 90},
    });
    m_table->setFlexMin(kNameColumnMin);
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kItem) {
            return {};
        }
        if (const auto* e = entryAt(row)) {
            return {e->name, TypeStyle::BodyStrong, ui::tokens::size::icon + kIconGap};
        }
        if (const auto* i = itemAt(row)) {
            return {i->name, TypeStyle::BodyStrong, ui::tokens::size::icon + kIconGap};
        }
        return {};
    };
    m_table->sortKey = [this](int row, int column) -> ui::TableSortKey {
        const auto* e = entryAt(row);
        const auto* i = itemAt(row);
        switch (column) {
        case kItem: return {e ? e->name : i ? i->name : std::wstring()};
        case kPlaces: return {placesText(e ? e->places : i ? i->places : std::vector<std::wstring>{})};
        case kKind: return {m_strings.get(e ? kindName(*e) : Str::ContextMenuKindCustom)};
        default: return {};
        }
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kState) {
            toggle(row);
        }
    };
    m_table->onActivate = [this](int row) { toggle(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::MoreVertical, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ContextMenuTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::ContextMenu) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            refilter(); // our own items come and go with the queue
            updateSummary();
            detailChanged();
        }
    });
    m_controller.load();
    refresh();
}

ContextMenuPage::~ContextMenuPage() {
    m_state.unsubscribe(m_subscription);
}

void ContextMenuPage::selectRow(int row) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        m_table->setSelected(row);
        detailChanged();
    }
}

void ContextMenuPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

bool ContextMenuPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

const ContextMenuEntry* ContextMenuPage::entryAt(int row) const {
    const auto& list = m_state.contextMenuList();
    if (!list || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    const int e = m_rows[static_cast<std::size_t>(row)].entry;
    return e >= 0 && e < static_cast<int>(list->items.size()) ? &list->items[static_cast<std::size_t>(e)] : nullptr;
}

const core::ContextMenuItem* ContextMenuPage::itemAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    const int i = m_rows[static_cast<std::size_t>(row)].item;
    return i >= 0 && i < static_cast<int>(m_items.size()) ? &m_items[static_cast<std::size_t>(i)] : nullptr;
}

void ContextMenuPage::showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                                std::function<void()> onAction) {
    m_empty->setContent(icon, m_strings.get(title), std::move(body));
    if (action) {
        m_empty->setAction(m_strings.get(*action)).onInvoke = std::move(onAction);
    } else {
        m_empty->hideAction();
    }
    m_empty->setVisible(true);
}

void ContextMenuPage::refresh() {
    const auto& list = m_state.contextMenuList();
    const bool ready = list && list->status == AppState::ContextMenuList::Status::Ready;
    if (!m_state.mounted()) {
        showEmpty(ui::icons::Icon::MoreVertical, Str::ContextMenuNoMountTitle, m_strings.get(Str::ContextMenuNoMountBody),
                  Str::CommonGoImages, m_intents.goImages);
    } else if (!list || list->status == AppState::ContextMenuList::Status::Loading) {
        m_controller.load();
        showEmpty(ui::icons::Icon::MoreVertical, Str::ContextMenuLoadingTitle, m_strings.get(Str::ContextMenuLoadingBody),
                  std::nullopt, {});
    } else if (list->status == AppState::ContextMenuList::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::ContextMenuFailedTitle, list->error.message + L" — " + list->error.context,
                  Str::CommonRetry, [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_changed, m_summaryLine, m_table}) {
        w->setVisible(ready);
    }
    refilter();
    updateSummary();
    layout();
    invalidate();
    detailChanged();
}

void ContextMenuPage::refilter() {
    const int selected = m_table->selected();
    m_rows.clear();
    m_items = m_controller.items();
    const auto& list = m_state.contextMenuList();
    auto matches = [&](const std::wstring& name, const std::wstring& id, const std::wstring& extra) {
        return m_needle.empty() || wl::text::fold(name).find(m_needle) != std::wstring::npos ||
               wl::text::fold(id).find(m_needle) != std::wstring::npos || wl::text::fold(extra).find(m_needle) != std::wstring::npos;
    };
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (matches(m_items[i].name, m_items[i].id, m_items[i].command)) {
            m_rows.push_back({-1, static_cast<int>(i)});
        }
    }
    if (list && list->status == AppState::ContextMenuList::Status::Ready) {
        for (std::size_t i = 0; i < list->items.size(); ++i) {
            const auto& e = list->items[i];
            if (m_onlyChanged && !m_controller.changed(e)) {
                continue;
            }
            if (!matches(e.name, e.id, e.source + L" " + e.command)) {
                continue;
            }
            m_rows.push_back({static_cast<int>(i), -1});
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    if (selected >= 0 && selected < static_cast<int>(m_rows.size())) {
        m_table->setSelected(selected);
    }
    invalidate();
}

void ContextMenuPage::updateSummary() {
    std::vector<SummaryPart> parts;
    if (const auto& list = m_state.contextMenuList(); list && list->status == AppState::ContextMenuList::Status::Ready) {
        parts.push_back({m_strings.format(Str::ContextMenuSummary, {{L"n", formatCount(list->items.size(), m_language)}})});
        if (const int n = m_controller.changedCount(); n > 0) {
            const std::wstring text =
                m_strings.format(Str::ContextMenuSummaryChange, {{L"n", formatCount(static_cast<std::size_t>(n), m_language)}});
            parts.push_back({text, SummaryPart::Tone::Change, text});
        }
    }
    m_summaryLine->set(std::move(parts));
}

void ContextMenuPage::toggle(int row) {
    if (const auto* e = entryAt(row)) {
        const ContextMenuEntry copy = *e; // the queue's notification rebuilds the rows
        m_controller.setHidden(copy, !m_controller.hidden(copy));
    }
}

DetailModel ContextMenuPage::detail() const {
    DetailModel model;
    const int row = m_table->visible() ? m_table->selected() : -1;
    if (const auto* i = itemAt(row)) {
        model.icon = ui::icons::Icon::MoreVertical;
        model.title = i->name;
        model.identity = L"WinLove." + i->id;
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::ContextMenuStateAdded), DetailTone::Change};
        model.facts.push_back({m_strings.get(Str::ContextMenuColPlaces), placesText(i->places)});
        model.facts.push_back({m_strings.get(Str::ContextMenuFactCommand), i->command, DetailTone::Normal, true});
        if (!i->icon.empty()) {
            model.facts.push_back({m_strings.get(Str::ContextMenuAddIcon), i->icon, DetailTone::Normal, true});
        }
        if (i->extended) {
            model.facts.push_back({m_strings.get(Str::ContextMenuAddExtended), m_strings.get(Str::CommonYes)});
        }
        model.notes.push_back({ui::icons::Icon::InfoCircle, m_strings.get(Str::ContextMenuNoteWin11)});
        model.actions.push_back({kActionRemove, m_strings.get(Str::ContextMenuRemove)});
        return model;
    }
    const auto* e = entryAt(row);
    if (!e) {
        return model;
    }
    const bool hidden = m_controller.hidden(*e);
    const bool changed = m_controller.changed(*e);
    model.icon = ui::icons::Icon::MoreVertical;
    model.title = e->name;
    model.identity = e->kind == ContextMenuEntry::Kind::Verb ? L"HKCR\\" + e->id : e->clsid;
    model.state = {changed ? ui::icons::Icon::QueueClock : ui::icons::Icon::Check,
                   m_strings.get(hidden ? Str::ContextMenuStateHidden : Str::ContextMenuStateShown),
                   changed ? DetailTone::Change : DetailTone::Quiet};
    model.facts.push_back({m_strings.get(Str::ContextMenuColPlaces), placesText(e->places)});
    model.facts.push_back({m_strings.get(Str::ContextMenuColKind), m_strings.get(kindName(*e))});
    model.facts.push_back({m_strings.get(Str::DetailAtApply), m_strings.get(hidden ? Str::ContextMenuStateHidden : Str::ContextMenuStateShown),
                           changed ? DetailTone::Change : DetailTone::Normal});
    if (!e->command.empty()) {
        model.facts.push_back({m_strings.get(Str::ContextMenuFactCommand), e->command, DetailTone::Normal, true});
    }
    if (!e->source.empty()) {
        model.facts.push_back({m_strings.get(Str::ContextMenuFactSource), e->source, DetailTone::Normal, true});
    }
    if (e->extended) {
        model.facts.push_back({m_strings.get(Str::ContextMenuAddExtended), m_strings.get(Str::CommonYes)});
    }
    model.notes.push_back({ui::icons::Icon::InfoCircle, m_strings.get(e->kind == ContextMenuEntry::Kind::Verb
                                                                          ? Str::ContextMenuNoteVerb
                                                                          : Str::ContextMenuNoteBlocked)});
    model.actions.push_back({kActionToggle, m_strings.get(hidden ? Str::ContextMenuShow : Str::ContextMenuHide)});
    if (changed) {
        model.actions.push_back({kActionRevert, m_strings.get(Str::ContextMenuRevert)});
    }
    return model;
}

void ContextMenuPage::detailAction(int id) {
    const int row = m_table->selected();
    if (const auto* i = itemAt(row); i && id == kActionRemove) {
        const std::wstring itemId = i->id;
        m_controller.removeItem(itemId);
        return;
    }
    const auto* e = entryAt(row);
    if (!e) {
        return;
    }
    const ContextMenuEntry copy = *e;
    if (id == kActionToggle) {
        m_controller.setHidden(copy, !m_controller.hidden(copy));
    } else if (id == kActionRevert) {
        m_controller.setHidden(copy, copy.hidden);
    }
}

void ContextMenuPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto* e = entryAt(row);
    const auto* i = itemAt(row);
    if (!e && !i) {
        return;
    }
    const bool changed = i != nullptr || m_controller.changed(*e);
    switch (column) {
    case kMarks: {
        RowMarks marks;
        marks.mark = changed ? RowMark::Queued : RowMark::None;
        paintMarks(canvas, rect, marks, kMarkSlots);
        break;
    }
    case kItem: {
        canvas.drawIcon(i ? ui::icons::Icon::Add : ui::icons::Icon::MoreVertical,
                        {rect.x, rect.y + std::round((rect.height - ui::tokens::size::icon) / 2)}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + kIconGap;
        canvas.drawText(e ? e->name : i->name, {x, rect.y, rect.right() - x, rect.height},
                        cell.selected || changed ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kPlaces:
        canvas.drawText(placesText(e ? e->places : i->places), rect, TypeStyle::Body, Color::TextSecondary);
        break;
    case kKind:
        canvas.drawText(m_strings.get(e ? kindName(*e) : Str::ContextMenuKindCustom), rect, TypeStyle::Body, Color::TextPrimary);
        break;
    case kState: {
        const bool hidden = e && m_controller.hidden(*e);
        const Str state = i ? Str::ContextMenuStateAdded : hidden ? Str::ContextMenuStateHidden : Str::ContextMenuStateShown;
        canvas.drawIcon(hidden ? ui::icons::Icon::EyeOff : ui::icons::Icon::Eye,
                        {rect.x, rect.y + std::round((rect.height - ui::tokens::size::icon) / 2)},
                        changed ? Color::AccentText : cell.hoveredCell ? Color::TextPrimary : Color::TextTertiary);
        const float x = rect.x + ui::tokens::size::icon + kIconGap;
        canvas.drawText(m_strings.get(state), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body,
                        changed ? Color::AccentText : Color::TextPrimary);
        break;
    }
    default: break;
    }
}

void ContextMenuPage::layout() {
    const RectF b = bounds();
    float x = b.x;
    m_search->setWidth(detailShown() ? 180.0f : 260.0f);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_changed}) {
        const ui::SizeF size = w->measure({});
        w->setBounds({x, b.y, size.width, kToolbar});
        x += size.width + kGap;
    }
    m_summaryLine->setBounds({x, b.y, std::max(b.right() - x, 0.0f), kToolbar});
    const float top = b.y + kToolbar + kTableGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kPlaces, kKind});
    m_empty->setBounds(b);
}

void ContextMenuPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::ContextMenuNoResults));
    }
}

} // namespace wl::app
