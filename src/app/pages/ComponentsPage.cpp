#include "app/pages/ComponentsPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "app/pages/components/ComponentInspector.h"
#include "base/Text.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>

namespace wl::app {

using core::ops::Risk;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Item = ComponentController::Item;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
constexpr float kInfoBar = 32.0f;
constexpr float kIndent = 16.0f;
constexpr float kChevron = 16.0f;

enum Column : int { kName, kRisk, kSize };
} // namespace

ComponentsPage::ComponentsPage(AppState& state, ComponentController& controller, const Localization& strings,
                               Language language, std::function<void()> goToImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_goToImages(std::move(goToImages)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::ComponentsSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::lower(text);
        rebuildRows();
    };
    m_category = &add<ui::Dropdown>(strings.get(Str::ComponentsCategory), std::vector<std::wstring>{strings.get(Str::CommonAll)}, 0);
    m_category->onChange = [this](int index) {
        m_categoryFilter = index;
        rebuildRows();
    };
    m_risk = &add<ui::Dropdown>(strings.get(Str::RiskColumn),
                                std::vector<std::wstring>{strings.get(Str::CommonAll), strings.get(Str::RiskLow),
                                                          strings.get(Str::RiskMedium), strings.get(Str::RiskHigh)},
                                0);
    m_risk->onChange = [this](int index) {
        m_riskFilter = index;
        rebuildRows();
    };
    m_selectedOnly = &add<ui::Toggle>(strings.get(Str::ComponentsOnlySelected), false);
    m_selectedOnly->onChange = [this](bool on) {
        m_onlySelected = on;
        rebuildRows();
    };
    m_riskBar = &add<ui::InfoBar>(ui::InfoKind::Warning, L"", L"", strings.get(Str::CommonClose));
    m_riskBar->setVisible(false);
    m_riskBar->onClose = [this] {
        m_riskBar->setVisible(false);
        layout();
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::CommonName), 0},
        {strings.get(Str::RiskColumn), 120},
        {strings.get(Str::CommonSize), 96, ui::TextAlign::Trailing},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) { click(row, column, p); };
    m_table->onActivate = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        const auto& group = m_groups[static_cast<std::size_t>(r.group)];
        if (r.item < 0) {
            m_controller.toggleGroup(group);
        } else {
            m_controller.toggle(group.items[static_cast<std::size_t>(r.item)]);
        }
    };
    m_table->onSelect = [this](int) {
        if (onSelectionChanged) {
            onSelectionChanged();
        }
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::ComponentsRemove, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ComponentsTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Components) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_onlySelected) {
                rebuildRows();
            }
            updateRiskBar();
            m_table->refresh();
            invalidate();
            if (onSelectionChanged) {
                onSelectionChanged();
            }
        }
    });
    m_controller.load();
    refresh();
}

ComponentsPage::~ComponentsPage() {
    m_state.unsubscribe(m_subscription);
}

void ComponentsPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

void ComponentsPage::reveal(const std::wstring& packageName) {
    m_search->setText({});
    m_needle.clear();
    m_category->setSelected(0);
    m_categoryFilter = 0;
    m_risk->setSelected(0);
    m_riskFilter = 0;
    m_selectedOnly->setOn(false, /*animated=*/false);
    m_onlySelected = false;
    for (const auto& group : m_groups) {
        if (std::ranges::any_of(group.items, [&](const Item& item) { return item.packageName == packageName; })) {
            m_collapsed.erase(group.catalogIndex);
        }
    }
    rebuildRows();
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        const Row& r = m_rows[row];
        if (r.item >= 0 &&
            m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)].packageName == packageName) {
            m_table->setSelected(static_cast<int>(row));
            if (host()) {
                host()->setFocus(m_table, /*visible=*/false);
            }
            return;
        }
    }
}

bool ComponentsPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

void ComponentsPage::setAllExpanded(bool expanded) {
    m_collapsed.clear();
    if (!expanded) {
        for (const auto& g : m_groups) {
            m_collapsed.insert(g.catalogIndex);
        }
    }
    rebuildRows();
}

bool ComponentsPage::allExpanded() const {
    return m_collapsed.empty();
}

std::optional<Item> ComponentsPage::selectedItem() const {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return std::nullopt;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.item < 0) {
        return std::nullopt;
    }
    return m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)];
}

std::wstring ComponentsPage::selectedGroupName() const {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    return m_groups[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)].group)].name;
}

void ComponentsPage::refresh() {
    const auto& list = m_state.appxList();
    const bool ready = list && list->status == AppState::AppxList::Status::Ready;
    if (!m_state.mounted()) {
        m_empty->setContent(ui::icons::Icon::ComponentsRemove, m_strings.get(Str::ComponentsEmptyTitle),
                            m_strings.get(Str::ComponentsEmptyBody));
        m_empty->setAction(m_strings.get(Str::CommonGoImages)).onInvoke = m_goToImages;
        m_empty->setVisible(true);
    } else if (!list || list->status == AppState::AppxList::Status::Loading) {
        m_controller.load();
        m_empty->setContent(ui::icons::Icon::Spinner, m_strings.get(Str::ComponentsReading),
                            m_strings.get(Str::ComponentsReadingBody));
        m_empty->hideAction();
        m_empty->setVisible(true);
    } else if (list->status == AppState::AppxList::Status::Failed) {
        m_empty->setContent(ui::icons::Icon::ErrorOctagon, m_strings.get(Str::ComponentsFailedTitle),
                            list->error.message + L" — " + list->error.context);
        m_empty->setAction(m_strings.get(Str::CommonRetry)).onInvoke = [this] { m_controller.load(/*force=*/true); };
        m_empty->setVisible(true);
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_category, m_risk, m_selectedOnly, m_table}) {
        w->setVisible(ready);
    }
    // The picked category survives a rebuild (the system components arrive a few seconds after the
    // apps): it is kept by group identity, not by position — groups come and go with the image.
    const int keptGroup = m_categoryFilter > 0 && m_categoryFilter <= static_cast<int>(m_groups.size())
                              ? m_groups[static_cast<std::size_t>(m_categoryFilter - 1)].catalogIndex
                              : -1;
    m_groups = m_controller.groups();
    std::vector<std::wstring> categories{m_strings.get(Str::CommonAll)};
    m_categoryFilter = 0;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        categories.push_back(m_groups[g].name);
        if (m_groups[g].catalogIndex == keptGroup) {
            m_categoryFilter = static_cast<int>(g) + 1;
        }
    }
    m_category->setItems(std::move(categories), m_categoryFilter);
    rebuildRows();
    updateRiskBar();
    layout();
    invalidate();
}

bool ComponentsPage::itemVisible(const Item& item) const {
    if (m_onlySelected && !m_controller.queued(item)) {
        return false;
    }
    if (m_riskFilter > 0 && static_cast<int>(item.risk) != m_riskFilter - 1) {
        return false;
    }
    if (!m_needle.empty() && wl::text::lower(item.name).find(m_needle) == std::wstring::npos &&
        wl::text::lower(item.identity).find(m_needle) == std::wstring::npos) {
        return false;
    }
    return true;
}

void ComponentsPage::rebuildRows() {
    // The table keeps the selection as a row index; rows move when filters or the queue change,
    // so remember WHAT was selected and find it again (else the inspector would act on the app
    // that slid into that row).
    std::wstring selectedPackage;
    int selectedGroup = -1;
    if (const int row = m_table->selected(); row >= 0 && row < static_cast<int>(m_rows.size())) {
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        if (r.item >= 0) {
            selectedPackage = m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)].packageName;
        } else {
            selectedGroup = r.group;
        }
    }
    m_rows.clear();
    const bool filtering = !m_needle.empty() || m_riskFilter > 0 || m_onlySelected;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        if (m_categoryFilter > 0 && static_cast<int>(g) != m_categoryFilter - 1) {
            continue;
        }
        const auto& group = m_groups[g];
        std::vector<int> visible;
        for (std::size_t i = 0; i < group.items.size(); ++i) {
            if (itemVisible(group.items[i])) {
                visible.push_back(static_cast<int>(i));
            }
        }
        if (filtering && visible.empty()) {
            continue; // only matching branches
        }
        m_rows.push_back({static_cast<int>(g), -1, static_cast<int>(visible.size())});
        // Filtering expands matching branches regardless of the collapse state.
        if (filtering || !m_collapsed.contains(group.catalogIndex)) {
            for (const int i : visible) {
                m_rows.push_back({static_cast<int>(g), i, 0});
            }
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    int found = -1;
    for (std::size_t i = 0; i < m_rows.size() && (!selectedPackage.empty() || selectedGroup >= 0); ++i) {
        const Row& r = m_rows[i];
        const bool match = r.item >= 0 ? !selectedPackage.empty() &&
                                             m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)].packageName ==
                                                 selectedPackage
                                       : selectedPackage.empty() && r.group == selectedGroup;
        if (match) {
            found = static_cast<int>(i);
            break;
        }
    }
    if (found >= 0) {
        m_table->setSelected(found, /*reveal=*/false);
    } else {
        m_table->clearSelection();
    }
    invalidate();
    if (onSelectionChanged) {
        onSelectionChanged();
    }
}

void ComponentsPage::updateRiskBar() {
    // The first queued high-risk app names the warning (design: "Defender kaldırılıyor:"). Deep
    // removal (D-060) outranks it: its warning says what cannot be undone.
    const Item* risky = nullptr;
    std::size_t count = 0;
    std::size_t deep = 0;
    for (const auto& g : m_groups) {
        for (const auto& item : g.items) {
            if (item.risk == Risk::High && m_controller.queued(item)) {
                risky = risky ? risky : &item;
                ++count;
                deep += item.deep ? 1 : 0;
            }
        }
    }
    if (deep > 0) {
        m_riskBar->set(ui::InfoKind::Error, m_strings.format(Str::ComponentsDeepTitle, {{L"n", std::to_wstring(deep)}}),
                       m_strings.get(Str::ComponentsDeepWarning));
        m_riskBar->setVisible(true);
        layout();
        return;
    }
    if (!risky) {
        if (m_riskBar->visible()) {
            m_riskBar->setVisible(false);
            layout();
        }
        return;
    }
    std::wstring body = risky->notes;
    if (count > 1) {
        body += (body.empty() ? L"" : L" ") + m_strings.format(Str::ApplyHighRiskN, {{L"n", std::to_wstring(count)}});
    }
    // The store cleanup and the shrink run something; the rest is removed.
    const bool runs = risky->kind == ComponentController::Item::Kind::Cleanup || risky->kind == ComponentController::Item::Kind::Shrink;
    m_riskBar->set(ui::InfoKind::Warning,
                   m_strings.format(runs ? Str::ComponentsRunWarning : Str::ComponentsDepWarning, {{L"name", risky->name}}), body);
    m_riskBar->setVisible(true);
    layout();
}

void ComponentsPage::click(int row, int column, ui::PointF p) {
    if (column != kName || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const RectF cell = m_table->cellRect(row, column);
    const float x0 = cell.x + ui::TableView::kCellPad + (r.item < 0 ? 0.0f : kIndent);
    if (r.item < 0 && p.x >= x0 && p.x < x0 + kChevron) {
        if (m_collapsed.contains(group.catalogIndex)) {
            m_collapsed.erase(group.catalogIndex);
        } else {
            m_collapsed.insert(group.catalogIndex);
        }
        rebuildRows();
        return;
    }
    const float boxX = x0 + kChevron + 4;
    if (p.x >= boxX - 2 && p.x < boxX + ui::Checkbox::kBox + 4) {
        if (r.item < 0) {
            m_controller.toggleGroup(group);
        } else {
            m_controller.toggle(group.items[static_cast<std::size_t>(r.item)]);
        }
    }
}

void ComponentsPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const bool isGroup = r.item < 0;
    const Item* item = isGroup ? nullptr : &group.items[static_cast<std::size_t>(r.item)];
    const bool filtering = !m_needle.empty() || m_riskFilter > 0 || m_onlySelected;
    switch (column) {
    case kName: {
        float x = rect.x + (isGroup ? 0.0f : kIndent);
        if (isGroup) {
            const bool open = filtering || !m_collapsed.contains(group.catalogIndex);
            canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {x, rect.y + 4},
                            Color::TextTertiary);
        }
        x += kChevron + 4;
        const auto state = isGroup ? m_controller.check(group) : (m_controller.queued(*item) ? ComponentController::Check::On
                                                                                            : ComponentController::Check::Off);
        // D-082: a guard or a kept app holds it — it cannot be ticked; the inspector says why.
        const bool held = !isGroup && !m_controller.block(*item).empty();
        if (held) {
            canvas.pushOpacity(ui::tokens::opacity::disabled);
        }
        ui::Checkbox::paintBox(canvas, {x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               state == ComponentController::Check::On        ? ui::CheckState::On
                               : state == ComponentController::Check::Partial ? ui::CheckState::Indeterminate
                                                                              : ui::CheckState::Off,
                               cell.hoveredCell && !held);
        if (held) {
            canvas.popOpacity();
        }
        x += ui::Checkbox::kBox + 8;
        canvas.drawIcon(isGroup ? ui::icons::Icon::Folder : held ? ui::icons::Icon::Lock : ComponentInspector::iconOf(item->kind),
                        {x, rect.y + 4}, held ? Color::TextTertiary : Color::TextSecondary);
        x += ui::tokens::size::icon + 6;
        const std::wstring& name = isGroup ? group.name : item->name;
        if (!isGroup && !m_needle.empty()) {
            const auto at = wl::text::lower(name).find(m_needle);
            if (at != std::wstring::npos) {
                const float x0 = x + canvas.text().measure(std::wstring_view(name).substr(0, at), TypeStyle::Body);
                const float x1 = x + canvas.text().measure(std::wstring_view(name).substr(0, at + m_needle.size()), TypeStyle::Body);
                canvas.fillRect({x0, rect.y + 4, x1 - x0, rect.height - 8}, Color::AccentSubtle);
            }
        }
        canvas.drawText(name, {x, rect.y, rect.right() - x, rect.height},
                        isGroup || cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        held ? Color::TextSecondary : Color::TextPrimary);
        break;
    }
    case kRisk:
        if (isGroup) {
            const std::wstring text = filtering
                                          ? m_strings.format(Str::ComponentsMatchesOf, {{L"m", std::to_wstring(r.matches)},
                                                                                        {L"n", std::to_wstring(group.items.size())}})
                                          : m_strings.format(Str::ComponentsItemsN, {{L"n", std::to_wstring(group.items.size())}});
            canvas.drawText(text, rect, TypeStyle::Caption, Color::TextTertiary);
        } else {
            paintRisk(canvas, rect, item->risk, m_strings);
        }
        break;
    case kSize: {
        const std::uint64_t size = isGroup ? group.size : item->size;
        canvas.drawText(size ? formatBytes(size, m_language) : std::wstring(L"—"), rect, TypeStyle::Mono,
                        size ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
        break;
    }
    default: break;
    }
}

void ComponentsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(240);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_category, m_risk, m_selectedOnly}) {
        const ui::SizeF size = w->measure({});
        // Long group names would make the category box huge: the value ellipsizes instead.
        const float width = w == m_category ? std::min(size.width, 200.0f) : size.width;
        w->setBounds({x, y, width, kToolbar});
        x += width + kGap;
    }
    y += kToolbar + 12;
    if (m_riskBar->visible()) {
        m_riskBar->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + 8;
    }
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
}

void ComponentsPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const float left = m_selectedOnly->bounds().right() + kGap;
    std::wstring text;
    if (!m_needle.empty()) {
        std::size_t n = 0;
        for (const auto& r : m_rows) {
            n += r.item >= 0 ? 1 : 0;
        }
        text = m_strings.format(Str::ComponentsResults, {{L"n", std::to_wstring(n)}}) + L" · " +
               m_strings.get(Str::ComponentsEscClears);
    } else {
        text = m_strings.get(Str::ComponentsEstGain) + L" " + formatBytes(m_controller.queuedBytes(), m_language) +
               L" · " + m_strings.format(Str::ComponentsQueuedN, {{L"n", std::to_wstring(m_controller.queuedCount())}});
    }
    canvas.drawText(text, {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
}

} // namespace wl::app
