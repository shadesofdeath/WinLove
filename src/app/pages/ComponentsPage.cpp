#include "app/pages/ComponentsPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <set>
namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Item = ComponentController::Item;

namespace {
// D-110, measured on the reference tool at 175 % (DIP): the filter row 24 high; a row's chevron at its indent,
// the check circle (12 across) centered 17 further, the name 30 in; each level 17 deeper. The size
// bar is ~20 DIP per decade of (1 + 10 × MB) and 20 high; the number sits 4 in on it.
constexpr float kFilterRow = ui::tokens::size::control;
constexpr float kGap = 8.0f;
constexpr float kInfoBar = ui::InfoBar::kHeight;
constexpr float kIndent = kTreeIndent; // D-139: one indent for every tree
constexpr float kChevronW = 14.0f;
constexpr float kCircle = 6.0f; // radius
constexpr float kTextX = 30.0f;
constexpr float kBarPerDecade = 20.3f;
constexpr float kBarHeight = 20.0f;
constexpr double kBarFullScale = 4.6127; // log10(1 + 10 × 4096 MB): the deepest tint is 4 GB
constexpr float kSizeColumn = 150.0f;
constexpr float kFilterWidth = 78.0f;
constexpr float kBarText = ui::TableView::kCellPad; // the number's inset on its bar: under the column's head
constexpr int kMarkSlots = 2;                        // D-139: state, risk

enum Column : int { kName, kMarks, kSize, kFill };

std::optional<Color> tierColor(Tier tier) {
    switch (tier) {
    case Tier::Privacy: return Color::TierPrivacy;
    case Tier::General: return Color::TierGeneral;
    case Tier::Light: return Color::TierLight;
    case Tier::NotRecommended: return Color::TierNotRecommended;
    case Tier::None: break;
    }
    return std::nullopt;
}

Str tierName(Tier tier) {
    switch (tier) {
    case Tier::Privacy: return Str::ComponentsTierPrivacy;
    case Tier::General: return Str::ComponentsTierGeneral;
    case Tier::Light: return Str::ComponentsTierLight;
    default: break;
    }
    return Str::ComponentsTierNotRecommended;
}

std::wstring joinPath(const std::vector<std::wstring>& path, std::size_t n) {
    std::wstring out;
    for (std::size_t i = 0; i < n && i < path.size(); ++i) {
        out += L"/" + path[i];
    }
    return out;
}

bool startsWithPath(const std::vector<std::wstring>& path, const std::vector<std::wstring>& prefix) {
    if (path.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (path[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool isDeviceKind(const Item& item) {
    return item.kind == Item::Kind::Driver;
}

// D-139: the risk the catalog gives (an app or a system component it knows); what the image's
// inventory lists without a catalog entry (a driver, a font …) has no risk of its own to show.
core::ops::Risk shownRisk(const Item& item) {
    return item.entry || item.system ? item.risk : core::ops::Risk::Low;
}

ui::icons::Icon kindIcon(Item::Kind kind) {
    switch (kind) {
    case Item::Kind::System:
    case Item::Kind::SystemApp: return ui::icons::Icon::WindowsLogoGeneric;
    case Item::Kind::Cleanup:
    case Item::Kind::Shrink: return ui::icons::Icon::SizeSaved;
    case Item::Kind::Driver: return ui::icons::Icon::DriverChip;
    case Item::Kind::Language: return ui::icons::Icon::LanguageGlobe;
    case Item::Kind::Keyboard: return ui::icons::Icon::Keyboard;
    case Item::Kind::Font: return ui::icons::Icon::Personalize;
    case Item::Kind::Appx: break;
    }
    return ui::icons::Icon::AppxPackage;
}

} // namespace

ComponentsPage::ComponentsPage(AppState& state, ComponentController& controller, const Localization& strings,
                               Language language, std::function<void()> goToImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_goToImages(std::move(goToImages)) {
    m_filterBox = &add<ui::Dropdown>(L"",
                                     std::vector<std::wstring>{
                                         strings.get(Str::ComponentsFilterAll), strings.get(Str::ComponentsFilterKept),
                                         strings.get(Str::ComponentsFilterRemoved), strings.get(Str::ComponentsFilterUserApps),
                                         strings.get(Str::ComponentsFilterSystem), strings.get(Str::ComponentsFilterDrivers),
                                         strings.get(Str::ComponentsFilterPrivacy), strings.get(Str::ComponentsFilterGeneral),
                                         strings.get(Str::ComponentsFilterLight)},
                                     0);
    m_filterBox->onChange = [this](int index) {
        m_filter = static_cast<Filter>(std::clamp(index, 0, static_cast<int>(Filter::Light)));
        rebuildRows();
    };
    m_search = &add<ui::SearchBox>(strings.get(Str::ComponentsFilterHint));
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        rebuildRows();
    };
    m_summary = &add<SummaryLine>();
    m_riskBar = &add<ui::InfoBar>(ui::InfoKind::Warning, L"", L"", strings.get(Str::CommonClose));
    m_riskBar->setVisible(false);
    m_riskBar->onClose = [this] {
        m_riskBar->setVisible(false);
        layout();
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::CommonName), m_nameWidth},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::ComponentsSizeColumn), kSizeColumn},
        {L"", 0},
    });
    m_table->setRowHeight(ui::tokens::size::tableRow);
    // D-139: the column heads are the table's own (the header row has the summary at its right).
    m_table->cellTooltip = [this](int row, int column) { return column == kMarks ? marksTip(row) : std::wstring(); };
    // A category's / node's name cut beside its "12/40" shows whole on hover (an item says itself: tooltipFor).
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kName || row < 0 || static_cast<std::size_t>(row) >= m_rows.size()) {
            return {};
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        if (r.kind != Row::Kind::Category && r.kind != Row::Kind::Node) {
            return {};
        }
        const float inset = static_cast<float>(r.depth) * kIndent + kTextX + kTreeCountRoom + 8;
        return {r.kind == Row::Kind::Category ? m_groups[static_cast<std::size_t>(r.group)].name : r.text,
                r.kind == Row::Kind::Category ? kGroupStyle : TypeStyle::Body, inset};
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) { click(row, column, p); };
    m_table->onActivate = [this](int row) { toggleByUser(row); };
    m_table->tooltipFor = [this](int row) { return tooltipOf(row); };
    m_table->onSelect = [this](int) { selectionChanged(); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::ComponentsRemove, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ComponentsTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Components) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_filter == Filter::Kept || m_filter == Filter::Removed) {
                rebuildRows();
            }
            updateRiskBar();
            updateChecks();
            updateSummary();
            m_table->refresh();
            invalidate();
            selectionChanged();
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

void ComponentsPage::setFilter(Filter filter) {
    m_filter = filter;
    m_filterBox->setSelected(static_cast<int>(filter));
    rebuildRows();
}

bool ComponentsPage::expanded(const std::wstring& key) const {
    return m_expanded.contains(key);
}

void ComponentsPage::reveal(const std::wstring& packageName) {
    m_search->setText({});
    m_needle.clear();
    m_filter = Filter::All;
    m_filterBox->setSelected(0);
    for (const auto& group : m_groups) {
        for (const auto& item : group.items) {
            if (item.packageName == packageName) {
                m_expanded.insert(L"c:" + std::to_wstring(group.catalogIndex));
                for (std::size_t n = 1; n <= item.path.size(); ++n) {
                    m_expanded.insert(L"n:" + std::to_wstring(group.catalogIndex) + joinPath(item.path, n));
                }
                for (const auto& up : m_controller.dependencies(item)) { // D-113: the rows it hangs under
                    m_expanded.insert(L"i:" + up.packageName);
                }
            }
        }
    }
    rebuildRows();
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        const Row& r = m_rows[row];
        if (r.kind == Row::Kind::Item &&
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

void ComponentsPage::setAllExpanded(bool open) {
    // Categories and nodes; a component's own lines (devices) stay as they are.
    if (!open) {
        m_expanded.clear();
        rebuildRows();
        return;
    }
    for (const auto& group : m_groups) {
        m_expanded.insert(L"c:" + std::to_wstring(group.catalogIndex));
        for (const auto& item : group.items) {
            for (std::size_t n = 1; n <= item.path.size(); ++n) {
                m_expanded.insert(L"n:" + std::to_wstring(group.catalogIndex) + joinPath(item.path, n));
            }
            if (!item.parentKey.empty()) {
                m_expanded.insert(L"i:" + item.parentKey); // D-113: the dependency tree too
            }
        }
    }
    rebuildRows();
}

bool ComponentsPage::allExpanded() const {
    return std::ranges::all_of(m_groups, [this](const auto& g) { return expanded(L"c:" + std::to_wstring(g.catalogIndex)); });
}

std::optional<Item> ComponentsPage::selectedItem() const {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return std::nullopt;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind != Row::Kind::Item) {
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
        // D-139: a reading list is a quiet line under the page's icon, not a spinner that never turns.
        m_empty->setContent(ui::icons::Icon::ComponentsRemove, m_strings.get(Str::ComponentsReading),
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
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_filterBox, m_search, m_summary, m_table}) {
        w->setVisible(ready);
    }
    m_keepKey = selectedKey();
    m_keepKnown = true;
    m_groups = m_controller.groups();
    m_itemFlagsAt = kNoRevision; // new items; also how the shell says "the guards changed" (D-082)
    rebuildRows();
    updateRiskBar();
    updateSummary();
    layout();
    invalidate();
    selectionChanged(); // the card: the list came or went, the guards changed
}

void ComponentsPage::selectionChanged() {
    if (onSelectionChanged) {
        onSelectionChanged();
    }
    detailChanged();
}

void ComponentsPage::updateSummary() {
    // "1.312 bileşen · 9,80 GB · 13 kaldırılacak · 1,05 GB": every component of the image and what
    // the queue takes of it (the flags are asked once per state change: itemQueued).
    std::size_t all = 0;
    std::size_t queued = 0;
    std::uint64_t bytes = 0;
    std::uint64_t freed = 0;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        for (std::size_t i = 0; i < m_groups[g].items.size(); ++i) {
            const auto size = m_groups[g].items[i].size;
            ++all;
            bytes += size;
            if (itemQueued(g, i)) {
                ++queued;
                freed += size;
            }
        }
    }
    std::vector<SummaryPart> parts;
    if (all > 0) {
        parts.push_back({m_strings.format(Str::ComponentsSummary, {{L"n", formatCount(all, m_language)}, {L"size", formatBytes(bytes, m_language)}})});
    }
    if (queued > 0) {
        parts.push_back({freed > 0 ? m_strings.format(Str::ComponentsSummaryRemove,
                                                      {{L"n", formatCount(queued, m_language)}, {L"size", formatBytes(freed, m_language)}})
                                   : m_strings.format(Str::ComponentsSummaryRemoveN, {{L"n", formatCount(queued, m_language)}}),
                         SummaryPart::Tone::Change});
    }
    m_summary->set(std::move(parts));
}

bool ComponentsPage::filtering() const {
    return !m_needle.empty() || m_filter != Filter::All;
}

bool ComponentsPage::itemVisible(const Item& item) const {
    switch (m_filter) {
    case Filter::All: break;
    case Filter::Kept:
        if (m_controller.queued(item)) {
            return false;
        }
        break;
    case Filter::Removed:
        if (!m_controller.queued(item)) {
            return false;
        }
        break;
    case Filter::UserApps:
        if (item.kind != Item::Kind::Appx) {
            return false;
        }
        break;
    case Filter::SystemApps:
        if (item.kind != Item::Kind::SystemApp) {
            return false;
        }
        break;
    case Filter::Drivers:
        if (item.kind != Item::Kind::Driver) {
            return false;
        }
        break;
    case Filter::Privacy:
        if (!templateTakes(Tier::Privacy, item.tier)) {
            return false;
        }
        break;
    case Filter::General:
        if (!templateTakes(Tier::General, item.tier)) {
            return false;
        }
        break;
    case Filter::Light:
        if (!templateTakes(Tier::Light, item.tier)) {
            return false;
        }
        break;
    }
    if (!m_needle.empty() && wl::text::fold(item.name).find(m_needle) == std::wstring::npos &&
        wl::text::fold(item.identity).find(m_needle) == std::wstring::npos) {
        return false;
    }
    return true;
}

// The rows under `prefix` of one category: its nodes and its components, by name (the reference tool sorts them
// together), each node followed by what is under it when it is open.
void ComponentsPage::emit(int group, const std::vector<int>& items, std::vector<std::wstring>& prefix, int depth, bool open) {
    const auto& g = m_groups[static_cast<std::size_t>(group)];
    struct Entry {
        std::wstring label;
        int item = -1; // -1: a node
    };
    std::vector<Entry> entries;
    const std::set<std::wstring>& shown = m_emitShown; // D-113: a component under one that is shown goes with that one
    for (const int i : items) {
        const Item& item = g.items[static_cast<std::size_t>(i)];
        if (!startsWithPath(item.path, prefix)) {
            continue;
        }
        if (!item.parentKey.empty() && shown.contains(item.parentKey)) {
            continue;
        }
        if (item.path.size() == prefix.size()) {
            entries.push_back({item.name, i});
        } else {
            const std::wstring& node = item.path[prefix.size()];
            if (std::ranges::none_of(entries, [&](const Entry& e) { return e.item < 0 && e.label == node; })) {
                entries.push_back({node, -1});
            }
        }
    }
    std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) { return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0; });
    const std::wstring base = L"n:" + std::to_wstring(g.catalogIndex) + joinPath(prefix, prefix.size());
    for (const auto& e : entries) {
        if (e.item < 0) {
            const std::wstring key = base + L"/" + e.label;
            m_rows.push_back({Row::Kind::Node, group, -1, depth, key, e.label});
            if (open || expanded(key)) {
                prefix.push_back(e.label);
                emit(group, items, prefix, depth + 1, open);
                prefix.pop_back();
            }
            continue;
        }
        emitItem(group, items, e.item, depth, open);
    }
}

void ComponentsPage::emitItem(int group, const std::vector<int>& items, int index, int depth, bool open) {
    const auto& g = m_groups[static_cast<std::size_t>(group)];
    const Item& item = g.items[static_cast<std::size_t>(index)];
    std::vector<int> dependents;
    if (const auto under = m_emitDependents.find(item.packageName); under != m_emitDependents.end()) {
        dependents = under->second;
    }
    std::ranges::stable_sort(dependents, [&](int a, int b) {
        return _wcsicmp(g.items[static_cast<std::size_t>(a)].name.c_str(), g.items[static_cast<std::size_t>(b)].name.c_str()) < 0;
    });
    const std::wstring key = L"i:" + item.packageName;
    const bool rows = !item.children.empty() || !dependents.empty();
    m_rows.push_back({Row::Kind::Item, group, index, depth, rows ? key : std::wstring(), {}});
    if (!rows || !(expanded(key) || (open && !dependents.empty()))) {
        return;
    }
    for (const int i : dependents) {
        emitItem(group, items, i, depth + 1, open);
    }
    for (const auto& child : item.children) {
        m_rows.push_back({Row::Kind::Child, group, index, depth + 1, {}, child});
    }
}

void ComponentsPage::rebuildRows() {
    // Rows move when filters or the queue change: remember WHAT was selected and find it again.
    // (D-139: after refresh() took new groups the old rows index the old ones — it read the key first;
    // an unmount with a component selected read past the new, shorter lists.)
    const std::wstring selectedKey = m_keepKnown ? m_keepKey : this->selectedKey();
    m_keepKnown = false;
    m_keepKey.clear();
    m_rows.clear();
    const bool filter = filtering();
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        const auto& group = m_groups[g];
        std::vector<int> visible;
        for (std::size_t i = 0; i < group.items.size(); ++i) {
            if (itemVisible(group.items[i])) {
                visible.push_back(static_cast<int>(i));
            }
        }
        if (filter && visible.empty()) {
            continue; // only matching branches
        }
        const std::wstring key = L"c:" + std::to_wstring(group.catalogIndex);
        m_rows.push_back({Row::Kind::Category, static_cast<int>(g), -1, 0, key, {}});
        if (filter || expanded(key)) {
            m_emitShown.clear();
            m_emitDependents.clear();
            for (const int i : visible) {
                const Item& item = group.items[static_cast<std::size_t>(i)];
                m_emitShown.insert(item.packageName);
                m_emitDependents[item.parentKey].push_back(i);
            }
            std::vector<std::wstring> prefix;
            emit(static_cast<int>(g), visible, prefix, 1, filter);
        }
    }
    m_emitShown.clear();
    m_emitDependents.clear();
    for (Row& r : m_rows) {
        if (r.kind != Row::Kind::Item) {
            continue;
        }
        const Item& item = m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)];
        if (!m_needle.empty()) {
            r.matchAt = wl::text::fold(item.name).find(m_needle);
        }
        if (item.size > 0) {
            r.sizeText = std::format(L"{:.2f}", static_cast<double>(item.size) / (1024.0 * 1024.0));
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    updateChecks();
    int found = -1;
    for (std::size_t i = 0; i < m_rows.size() && !selectedKey.empty(); ++i) {
        const Row& r = m_rows[i];
        const std::wstring key = r.kind == Row::Kind::Item
                                     ? L"i:" + m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)].packageName
                                     : r.key;
        if (key == selectedKey) {
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
    selectionChanged();
}

std::wstring ComponentsPage::selectedKey() const {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind != Row::Kind::Item) {
        return r.key;
    }
    const auto g = static_cast<std::size_t>(r.group);
    const auto i = static_cast<std::size_t>(r.item);
    return g < m_groups.size() && i < m_groups[g].items.size() ? L"i:" + m_groups[g].items[i].packageName : std::wstring();
}

void ComponentsPage::syncItemFlags() const {
    const std::uint64_t revision = m_state.revision();
    if (m_itemFlagsAt == revision && m_itemFlags.size() == m_groups.size()) {
        return;
    }
    m_itemFlagsAt = revision;
    m_itemFlags.resize(m_groups.size());
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        m_itemFlags[g].assign(m_groups[g].items.size(), ItemFlags{});
    }
}

bool ComponentsPage::itemQueued(std::size_t group, std::size_t item) const {
    syncItemFlags();
    ItemFlags& flags = m_itemFlags[group][item];
    if (flags.queued < 0) {
        flags.queued = m_controller.queued(m_groups[group].items[item]) ? 1 : 0;
    }
    return flags.queued != 0;
}

bool ComponentsPage::itemHeld(std::size_t group, std::size_t item) const {
    syncItemFlags();
    ItemFlags& flags = m_itemFlags[group][item];
    if (flags.held < 0) {
        flags.held = m_controller.block(m_groups[group].items[item]).empty() ? 0 : 1;
    }
    return flags.held != 0;
}

void ComponentsPage::updateChecks() {
    m_checks.assign(m_rows.size(), {});
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const auto group = static_cast<std::size_t>(r.group);
        const auto& g = m_groups[group];
        if (r.kind == Row::Kind::Child) {
            continue;
        }
        if (r.kind == Row::Kind::Item) {
            const auto at = static_cast<std::size_t>(r.item);
            m_checks[i] = {static_cast<std::uint8_t>(itemQueued(group, at) ? TreeCheck::Off : TreeCheck::On),
                           itemHeld(group, at)};
            continue;
        }
        std::size_t queued = 0;
        std::size_t open = 0;
        std::size_t all = 0;
        std::uint64_t bytes = 0;
        for (const int index : itemsUnder(r)) {
            const auto at = static_cast<std::size_t>(index);
            const bool q = itemQueued(group, at);
            queued += q ? 1 : 0;
            open += q || (g.items[at].lockReason.empty() && !itemHeld(group, at)) ? 1 : 0;
            ++all;
            bytes += g.items[at].size;
        }
        const TreeCheck state = queued == 0 ? TreeCheck::On : queued >= open ? TreeCheck::Off : TreeCheck::Mixed;
        m_checks[i] = {static_cast<std::uint8_t>(state), open == 0, treeCount(m_strings, all - queued, all),
                       bytes > 0 ? std::format(L"{:.2f}", static_cast<double>(bytes) / (1024.0 * 1024.0)) : std::wstring()};
    }
}

std::vector<int> ComponentsPage::itemsUnder(const Row& row) const {
    const auto& g = m_groups[static_cast<std::size_t>(row.group)];
    std::vector<int> out;
    if (row.kind == Row::Kind::Category) {
        for (std::size_t i = 0; i < g.items.size(); ++i) {
            out.push_back(static_cast<int>(i));
        }
        return out;
    }
    // A node's key: "n:<category>/<a>/<b>" — the components whose path starts with a, b.
    std::vector<std::wstring> prefix;
    const std::wstring head = L"n:" + std::to_wstring(g.catalogIndex);
    std::wstring_view rest = std::wstring_view(row.key).substr(head.size());
    while (!rest.empty()) {
        rest.remove_prefix(1); // '/'
        const auto slash = rest.find(L'/');
        prefix.emplace_back(rest.substr(0, slash));
        rest = slash == std::wstring_view::npos ? std::wstring_view() : rest.substr(slash);
    }
    for (std::size_t i = 0; i < g.items.size(); ++i) {
        if (startsWithPath(g.items[i].path, prefix)) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

void ComponentsPage::updateRiskBar() {
    // Deep removal (D-060) cannot be undone: its own warning while anything of it is queued.
    std::size_t deep = 0;
    for (const auto& g : m_groups) {
        for (const auto& item : g.items) {
            deep += (item.deep || item.kind == Item::Kind::Driver) && m_controller.queued(item) ? 1 : 0;
        }
    }
    const bool show = deep > 0;
    if (show) {
        m_riskBar->set(ui::InfoKind::Error, m_strings.format(Str::ComponentsDeepTitle, {{L"n", std::to_wstring(deep)}}),
                       m_strings.get(Str::ComponentsDeepWarning));
    }
    if (show != m_riskBar->visible()) {
        m_riskBar->setVisible(show);
        layout();
    }
}

std::wstring ComponentsPage::tooltipOf(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind != Row::Kind::Item) {
        return {};
    }
    const Item& item = m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)];
    std::wstring tip = item.name;
    if (!item.notes.empty()) {
        tip += L"\n" + item.notes;
    }
    if (!item.detail.empty()) {
        tip += L"\n" + item.detail;
    }
    if (!item.children.empty() && isDeviceKind(item)) {
        tip += L"\n" + m_strings.format(Str::ComponentsDevicesLine, {{L"n", std::to_wstring(item.children.size())}});
    }
    if (item.size > 0) {
        tip += L"\n" + m_strings.format(Str::ComponentsSizeLine, {{L"size", formatBytes(item.size, m_language)}});
    }
    if (item.tier != Tier::None) {
        tip += L"\n" + m_strings.format(Str::ComponentsTierLine, {{L"name", m_strings.get(tierName(item.tier))}});
    }
    if (!item.lockReason.empty()) {
        tip += L"\n" + item.lockReason;
    } else if (!m_controller.block(item).empty()) {
        tip += L"\n" + m_strings.get(Str::ComponentsLockedLine);
        if (explainBlock) {
            tip += L" • " + explainBlock(item);
        }
    }
    return tip;
}

void ComponentsPage::toggleRow(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    switch (r.kind) {
    case Row::Kind::Item: m_controller.toggle(group.items[static_cast<std::size_t>(r.item)]); break;
    case Row::Kind::Category:
    case Row::Kind::Node: {
        // Every open component of it queued → back; else every open one goes.
        // The answers updateChecks() already has (audit PG-02: three guard questions per component here).
        const auto g = static_cast<std::size_t>(r.group);
        const std::vector<int> under = itemsUnder(r);
        std::vector<Item> items;
        items.reserve(under.size());
        bool allTaken = true;
        bool anyQueued = false;
        for (const int index : under) {
            const auto at = static_cast<std::size_t>(index);
            items.push_back(group.items[at]);
            const bool q = itemQueued(g, at);
            anyQueued = anyQueued || q;
            allTaken = allTaken && (q || itemHeld(g, at));
        }
        const bool allRemoved = allTaken && anyQueued;
        m_controller.setRemoved(items, !allRemoved);
        break;
    }
    case Row::Kind::Child: break;
    }
}

void ComponentsPage::toggleByUser(int row) {
    toggleRow(row);
    // The row the user ticked draws its tick in; what a category's box ticks with it, an undo or a
    // template stay instant. Under "Tutulanlar" / "Kaldırılanlar" the rows were just rebuilt: the
    // index may name another row now.
    if (m_filter == Filter::Kept || m_filter == Filter::Removed || row < 0 || static_cast<std::size_t>(row) >= m_checks.size()) {
        return;
    }
    if (static_cast<TreeCheck>(m_checks[static_cast<std::size_t>(row)].state) != TreeCheck::Off) {
        m_table->revealCheck(row);
    }
}

std::wstring ComponentsPage::rowCountText(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < m_checks.size() ? m_checks[static_cast<std::size_t>(row)].count : std::wstring();
}

std::wstring ComponentsPage::rowSizeText(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= m_rows.size()) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind == Row::Kind::Item) {
        return r.sizeText;
    }
    return static_cast<std::size_t>(row) < m_checks.size() ? m_checks[static_cast<std::size_t>(row)].sizeText : std::wstring();
}

float ComponentsPage::sizeNumberX() const {
    return m_table->cellRect(0, kSize).x + kBarText;
}

void ComponentsPage::click(int row, int column, ui::PointF p) {
    if (column != kName || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind == Row::Kind::Child) {
        return;
    }
    const RectF cell = m_table->cellRect(row, column);
    const float x0 = cell.x + ui::TableView::kCellPad + static_cast<float>(r.depth) * kIndent;
    if (!r.key.empty() && p.x < x0 + kChevronW) {
        if (m_expanded.contains(r.key)) {
            m_expanded.erase(r.key);
        } else {
            m_expanded.insert(r.key);
        }
        rebuildRows();
        return;
    }
    const float cx = x0 + kChevronW + kCircle;
    if (p.x >= cx - kCircle - 4 && p.x < cx + kCircle + 4) {
        toggleByUser(row);
    }
}

void ComponentsPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto& group = m_groups[static_cast<std::size_t>(r.group)];
    const Item* item = r.item >= 0 ? &group.items[static_cast<std::size_t>(r.item)] : nullptr;
    const float cy = rect.y + rect.height / 2;
    switch (column) {
    case kName: {
        float x = rect.x + static_cast<float>(r.depth) * kIndent;
        if (r.kind == Row::Kind::Child) {
            // A driver's device: a plain line under it, nothing to tick.
            const float tx = x + kTextX - kChevronW;
            canvas.drawText(r.text, {tx, rect.y, std::max(rect.right() - tx, 0.0f), rect.height}, TypeStyle::Body, Color::TextTertiary);
            break;
        }
        if (!r.key.empty()) {
            const bool open = (r.kind != Row::Kind::Item && filtering()) || expanded(r.key);
            const float s = 12.0f;
            canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {x, cy - s / 2}, Color::TextTertiary,
                            ui::IconVariant::Regular16, s);
        }
        x += kChevronW;
        // The reference tool: a tick means kept — the reverse of the queue (cached: updateChecks).
        static const CheckState kNone{};
        const CheckState& check = static_cast<std::size_t>(row) < m_checks.size() ? m_checks[static_cast<std::size_t>(row)] : kNone;
        const TreeCheck kept = static_cast<TreeCheck>(check.state);
        const bool held = check.held;
        const std::wstring& name = r.kind == Row::Kind::Item ? item->name : r.kind == Row::Kind::Category ? group.name : r.text;
        paintTreeCheck(canvas, {x + kCircle, cy}, kept, item ? tierColor(item->tier) : std::nullopt, held, cell);
        x = rect.x + static_cast<float>(r.depth) * kIndent + kTextX;
        if (!m_needle.empty() && item) {
            const auto at = r.matchAt;
            if (at != std::wstring::npos) {
                const float x0 = x + canvas.text().measure(std::wstring_view(name).substr(0, at), TypeStyle::Body);
                const float x1 = x + canvas.text().measure(std::wstring_view(name).substr(0, at + m_needle.size()), TypeStyle::Body);
                canvas.fillRoundRect({x0, cy - 9, x1 - x0, 18}, 3.0f, Color::AccentSubtle);
            }
        }
        const Color ink = held && item                                     ? Color::TextTertiary
                          : item && item->tier == Tier::NotRecommended     ? Color::TextSecondary
                                                                           : Color::TextPrimary;
        // A category / node row: "kept/all" at the cell's right (cached: updateChecks), the name before it;
        // D-139: a category reads as the head of what is under it.
        const float right = item ? rect.right() : rect.right() - (check.count.empty() ? 0.0f : kTreeCountRoom + 8);
        canvas.drawText(name, {x, rect.y, std::max(right - x, 0.0f), rect.height},
                        r.kind == Row::Kind::Category ? kGroupStyle : TypeStyle::Body, ink);
        if (!item) {
            paintTreeCount(canvas, rect, check.count);
        }
        break;
    }
    case kMarks:
        if (r.kind == Row::Kind::Item) {
            paintMarks(canvas, rect, marksOf(row), kMarkSlots);
        }
        break;
    case kSize: {
        if (r.kind == Row::Kind::Category || r.kind == Row::Kind::Node) {
            // What its components take together, quietly, where the components' numbers are.
            if (static_cast<std::size_t>(row) < m_checks.size()) {
                const float left = rect.x - ui::TableView::kCellPad + kBarText;
                canvas.drawText(m_checks[static_cast<std::size_t>(row)].sizeText, {left, rect.y, rect.right() - left, rect.height},
                                TypeStyle::Body, Color::TextTertiary);
            }
            break;
        }
        if (r.kind != Row::Kind::Item || item->size == 0) {
            break;
        }
        // The reference tool's bar: ~20 per decade of (1 + 10 × MB), 20 high, the number on it ("87.93"). The
        // user liked it and asked for the same, a little nicer (a separate gauge was not, 2026-11-09):
        // rounded ends, and the accent's soft tint in place of grey, a touch deeper the bigger the row.
        // D-137: from the size bar's own quiet tone towards the accent's readable tone — the selection
        // fill (accent.subtle, a real tint since D-137) is the chosen row's, not a size's.
        const double mb = static_cast<double>(item->size) / (1024.0 * 1024.0);
        const double decades = std::log10(1.0 + 10.0 * mb);
        const float left = rect.x - ui::TableView::kCellPad;
        const float w = std::min(static_cast<float>(kBarPerDecade * decades), rect.right() - left);
        const RectF bar{left, rect.y + std::round((rect.height - kBarHeight) / 2), std::max(w, 4.0f), kBarHeight};
        const float depth = static_cast<float>(std::clamp(decades / kBarFullScale, 0.0, 1.0)) * 0.3f;
        canvas.fillRoundRect(bar, 4.0f, ui::Ink(Color::SizeBar, Color::AccentText, depth));
        canvas.drawText(r.sizeText, {left + kBarText, rect.y, rect.right() - left - kBarText, rect.height}, TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    default: break;
    }
}

void ComponentsPage::layout() {
    // D-139: the header row — "Tümü ▾", the search box (no wider than the name column), the summary at
    // the right; then the table with its own column heads.
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_nameWidth = std::clamp(std::round(b.width * 0.42f), 260.0f, 420.0f);
    m_table->setColumnWidth(kName, m_nameWidth);
    float y = b.y;
    m_filterBox->setBounds({b.x, y, kFilterWidth, kFilterRow}); // The reference tool's "Tümü ▾" is narrow; its list is not
    const float searchX = m_filterBox->bounds().right() + kGap;
    const float searchW = std::max(m_nameWidth - ui::TableView::kCellPad - (searchX - b.x), 220.0f);
    m_search->setWidth(searchW);
    m_search->setBounds({searchX, y, searchW, kFilterRow});
    const float summaryX = m_search->bounds().right() + kHeaderGap;
    m_summary->setBounds({summaryX, y, std::max(b.right() - summaryX, 0.0f), kFilterRow});
    y += kFilterRow + kHeaderGap;
    if (m_riskBar->visible()) {
        m_riskBar->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + 6;
    }
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
    m_sizeHead = columnHead(*m_table, kSize, y, ui::TableView::kHeader);
}

void ComponentsPage::paint(ui::Canvas&) {}

// ---- D-139: marks and the detail card ----------------------------------------------------------

RowMarks ComponentsPage::marksOf(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= m_rows.size()) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.kind != Row::Kind::Item) {
        return {};
    }
    const auto g = static_cast<std::size_t>(r.group);
    const auto i = static_cast<std::size_t>(r.item);
    const Item& item = m_groups[g].items[i];
    RowMarks marks;
    marks.risk = shownRisk(item);
    marks.mark = itemQueued(g, i)            ? RowMark::Queued
                 : !item.lockReason.empty() ? RowMark::Locked
                 : itemHeld(g, i)           ? RowMark::Held
                                            : RowMark::None;
    return marks;
}

std::wstring ComponentsPage::marksTip(int row) const {
    const RowMarks marks = marksOf(row);
    if (marks.mark == RowMark::None && marks.risk == core::ops::Risk::Low) {
        return {};
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const Item& item = m_groups[static_cast<std::size_t>(r.group)].items[static_cast<std::size_t>(r.item)];
    std::wstring reason = item.lockReason;
    if (marks.mark == RowMark::Held && explainBlock) {
        reason = explainBlock(item);
    }
    return marksTooltip(m_strings, marks, m_strings.get(Str::ComponentsWillRemove), reason);
}

DetailModel ComponentsPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || static_cast<std::size_t>(row) >= m_rows.size()) {
        return model;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const auto g = static_cast<std::size_t>(r.group);
    const auto& group = m_groups[g];
    if (r.kind == Row::Kind::Category || r.kind == Row::Kind::Node) {
        // A category / node: what is under it, and its box's action.
        model.icon = ui::icons::Icon::Folder;
        model.title = r.kind == Row::Kind::Category ? group.name : r.text;
        if (r.kind == Row::Kind::Node) {
            model.subtitle = group.name;
        }
        std::size_t all = 0, kept = 0, queued = 0;
        std::uint64_t bytes = 0, freed = 0;
        bool anyOpen = false;
        for (const int index : itemsUnder(r)) {
            const auto at = static_cast<std::size_t>(index);
            const bool q = itemQueued(g, at);
            ++all;
            kept += q ? 0 : 1;
            queued += q ? 1 : 0;
            bytes += group.items[at].size;
            freed += q ? group.items[at].size : 0;
            anyOpen = anyOpen || q || (group.items[at].lockReason.empty() && !itemHeld(g, at));
        }
        model.facts.push_back({m_strings.get(Str::DetailInImage),
                               m_strings.format(Str::ComponentsGroupKept, {{L"on", formatCount(kept, m_language)}, {L"n", formatCount(all, m_language)}})});
        if (bytes > 0) {
            model.facts.push_back({m_strings.get(Str::CommonSize), formatBytes(bytes, m_language), DetailTone::Normal, true});
        }
        if (queued > 0) {
            model.state = {ui::icons::Icon::QueueClock,
                           freed > 0 ? m_strings.format(Str::ComponentsSummaryRemove,
                                                        {{L"n", formatCount(queued, m_language)}, {L"size", formatBytes(freed, m_language)}})
                                     : m_strings.format(Str::ComponentsSummaryRemoveN, {{L"n", formatCount(queued, m_language)}}),
                           DetailTone::Change};
        }
        const TreeCheck check = static_cast<std::size_t>(row) < m_checks.size() ? static_cast<TreeCheck>(m_checks[static_cast<std::size_t>(row)].state)
                                                                                : TreeCheck::On;
        model.actions.push_back({kActionToggle, m_strings.get(check == TreeCheck::Off ? Str::ComponentsGroupKeep : Str::ComponentsGroupRemove),
                                 anyOpen});
        return model;
    }
    if (r.kind != Row::Kind::Item) {
        return model; // a driver's device line: nothing more than its row
    }
    const auto i = static_cast<std::size_t>(r.item);
    const Item& item = group.items[i];
    const bool queued = itemQueued(g, i);
    const bool locked = !item.lockReason.empty();
    const bool held = !locked && itemHeld(g, i);
    model.icon = kindIcon(item.kind);
    model.title = item.name;
    model.subtitle = group.name;
    for (const auto& node : item.path) {
        model.subtitle += L" › " + node;
    }
    model.identity = item.identity;
    if (queued) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::ComponentsWillRemove), DetailTone::Change};
    } else if (locked) {
        model.state = {ui::icons::Icon::Lock, m_strings.get(Str::ComponentsCannotRemove), DetailTone::Quiet};
    } else if (held) {
        model.state = {ui::icons::Icon::ShieldCheck, m_strings.get(Str::MarksHeld), DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::ComponentsStays), DetailTone::Quiet};
    }
    model.about = item.notes;
    if (!item.detail.empty()) {
        model.about += (model.about.empty() ? L"" : L"\n") + item.detail;
    }
    model.facts.push_back({m_strings.get(Str::CommonSize), item.size ? formatBytes(item.size, m_language) : std::wstring(L"—"),
                           DetailTone::Normal, true});
    if (item.tier != Tier::None) {
        model.facts.push_back({m_strings.get(Str::ComponentsTemplateAction), m_strings.get(tierName(item.tier))});
    }
    if (item.entry || item.system) {
        const auto risk = item.risk;
        model.facts.push_back({m_strings.get(Str::RiskColumn), m_strings.get(riskText(risk)),
                               risk == core::ops::Risk::Low ? DetailTone::Success : risk == core::ops::Risk::High ? DetailTone::Error : DetailTone::Warning,
                               false, true});
    }
    model.facts.push_back({m_strings.get(Str::DetailAtApply), m_strings.get(queued ? Str::ComponentsFactRemoved : Str::ComponentsFactKept),
                           queued ? DetailTone::Change : DetailTone::Normal});
    model.facts.push_back({m_strings.get(Str::ComponentsReversible), m_strings.get(Str::CommonNo)});
    if (held && explainBlock) {
        model.notes.push_back({ui::icons::Icon::ShieldCheck, explainBlock(item) + L"\n" + m_strings.get(Str::CompatLockedHint)});
    }
    if (locked) {
        model.notes.push_back({ui::icons::Icon::Lock, item.lockReason});
    }
    if (item.deep || item.kind == Item::Kind::Driver) {
        model.notes.push_back({ui::icons::Icon::WarningTriangle, m_strings.get(Str::ComponentsDeepWarning), DetailTone::Warning});
    }
    if (gapsOf) {
        if (const auto lines = gapsOf(item); !lines.empty()) {
            std::wstring text = m_strings.get(Str::ComponentsGapTitle);
            for (const auto& line : lines) {
                text += L"\n" + line;
            }
            model.notes.push_back({ui::icons::Icon::Check, std::move(text)});
        }
    }
    if (const auto ups = m_controller.dependencies(item); !ups.empty()) {
        DetailList list{m_strings.get(Str::DetailDependsOn)};
        for (const auto& up : ups) {
            list.lines.push_back(up.name);
        }
        model.lists.push_back(std::move(list));
    }
    if (!item.children.empty()) {
        model.lists.push_back({m_strings.get(Str::DetailDevices) + L" · " + formatCount(item.children.size(), m_language), item.children});
    }
    if (!item.contents.empty()) {
        model.lists.push_back({m_strings.get(Str::ComponentsContents) + L" · " +
                                   m_strings.format(item.kind == Item::Kind::Appx ? Str::ComponentsPackages : Str::ComponentsItemsN,
                                                    {{L"n", std::to_wstring(item.contents.size())}}),
                               item.contents, true});
    }
    model.actions.push_back({kActionToggle, m_strings.get(queued ? Str::ComponentsRemoveFromQueue : Str::ComponentsAddToQueue),
                             queued || (!locked && !held)});
    return model;
}

void ComponentsPage::detailAction(int id) {
    if (id == kActionToggle) {
        toggleByUser(m_table->selected()); // the row's own Space / double click
    }
}

} // namespace wl::app
