#include "app/pages/RegistryPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "core/image/RegistryInput.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 0.0f;
constexpr float kKeyMin = 200.0f; // the key column's least (see the columns)
constexpr float kRemoveColumn = ui::tokens::size::icon + 2 * ui::TableView::kCellPad; // D-139: the ✕ of a row
// The scope's chips: a small control's height less a spacing step, text 6 in (D-136: from the tokens).
constexpr float kChipH = ui::tokens::size::controlSmall - ui::tokens::spacing::s1;
constexpr float kChipPad = ui::tokens::spacing::s3;
constexpr float kChipGap = ui::tokens::spacing::s3;
constexpr float kNameGap = ui::tokens::spacing::s4; // check → name, name → its note

enum Column : int { kEntry, kKey, kValue, kScope, kRemove };
constexpr int kListLines = 40; // D-139: the values of an imported .reg the card lists (the rest: "+n daha")
} // namespace

RegistryPage::RegistryPage(AppState& state, RegistryController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_intents(std::move(intents)) {
    // The key (the longest text) takes what is left: with the entry as the flexible column the fixed
    // ones overran the card at 1200 DIP and the scope's tag and its ✕ were cut off at the edge.
    // D-136 (13 px text): the entry keeps more room; at a narrow window the entry, the value and the
    // scope give way down to their least so the key keeps kKeyMin; what is cut shows whole on hover.
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {.title = strings.get(Str::RegistryEntry), .width = 230, .minWidth = 170},
        {strings.get(Str::RegistryKey), 0},
        {.title = strings.get(Str::RegistryValue), .width = 200, .minWidth = 150},
        {.title = strings.get(Str::RegistryScope), .width = 242, .minWidth = 222},
        {L"", kRemoveColumn},
    });
    m_table->cellTooltip = [this](int, int column) { return column == kRemove ? m_strings.get(Str::RegistryRemoveTip) : std::wstring(); };
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->setFlexMin(kKeyMin);
    m_table->cellText = [this](int row, int column) { return cellText(row, column); };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kEntry) {
            const RectF cell = m_table->cellRect(row, kEntry);
            if (p.x < cell.x + ui::Checkbox::kBox + kNameGap) {
                toggleEntry(row);
                return;
            }
        }
        if (column == kRemove) { // the row's ✕ = remove the entry
            m_controller.remove(static_cast<std::size_t>(row));
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (row < 0) {
            return false;
        }
        if (key.virtualKey == VK_SPACE) {
            toggleEntry(row);
            return true;
        }
        if (key.virtualKey == VK_DELETE) {
            m_controller.remove(static_cast<std::size_t>(row));
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Registry, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::RegistryTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Queue || change == AppState::Change::Registry) {
            refresh();
        }
    });
    refresh();
}

RegistryPage::~RegistryPage() {
    m_state.unsubscribe(m_subscription);
}

void RegistryPage::activate(int row) {
    const auto& entries = m_state.regImports();
    if (row < 0 || row >= static_cast<int>(entries.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(row);
    if (entries[index].typed && m_intents.edit) {
        m_intents.edit(index);
    } else {
        toggleEntry(row);
    }
}

void RegistryPage::toggleEntry(int row) {
    m_controller.toggle(static_cast<std::size_t>(row));
    // The rows are the entries in their order (refresh() ran in the queue's notification): the
    // user's tick draws in.
    if (row >= 0 && static_cast<std::size_t>(row) < m_rowCache.size() && m_rowCache[static_cast<std::size_t>(row)].checked) {
        m_table->revealCheck(row);
    }
}

void RegistryPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    if (!mounted) {
        m_empty->setContent(ui::icons::Icon::Registry, m_strings.get(Str::RegistryNoMountTitle),
                            m_strings.get(Str::RegistryNoMountBody));
        m_empty->setAction(m_strings.get(Str::CommonGoImages)).onInvoke = m_intents.goImages;
    }
    m_empty->setVisible(!mounted);
    m_table->setVisible(mounted);
    m_summaryLine->setVisible(mounted);
    // Every change of the entries or of the queue comes through here (Mount / Queue / Registry).
    const auto& entries = m_state.regImports();
    m_rowCache.clear();
    m_rowCache.reserve(entries.size());
    m_checkedRows = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        RowCache row;
        row.checked = m_controller.checked(i);
        m_checkedRows += row.checked ? 1 : 0;
        for (const auto& w : entry.writes) {
            (core::isUserKey(w.key) ? row.user : row.system) = true;
        }
        if (!entry.writes.empty()) {
            row.key = entry.writes.front().key;
            const auto keys =
                std::ranges::count_if(entry.writes, [&](const core::RegistryWrite& w) { return w.key != row.key; });
            if (keys > 0) {
                row.key += std::format(L"  +{}", keys);
            }
        }
        row.name = cellText(static_cast<int>(i), kEntry).text;
        row.value = entry.typed && !entry.writes.empty()
                        ? valueText(entry.writes.front())
                        : m_strings.format(Str::RegistryValuesN, {{L"n", std::to_wstring(entry.writes.size())}});
        row.scope = row.user && row.system ? m_strings.get(Str::RegistryScopeSystem) + L" + " + m_strings.get(Str::RegistryScopeUser)
                    : row.user             ? m_strings.get(Str::RegistryScopeUser)
                                           : m_strings.get(Str::RegistryScopeSystem);
        m_rowCache.push_back(std::move(row));
    }
    // D-139: "5 kayıt · 4 uygulanacak" (it said "4 / 5 açık" above the table's right end).
    std::vector<SummaryPart> parts;
    if (!m_rowCache.empty()) {
        parts.push_back({m_strings.format(Str::RegistrySummary, {{L"n", formatCount(m_rowCache.size(), m_language)}})});
    }
    if (m_checkedRows > 0) {
        parts.push_back({m_strings.format(Str::RegistrySummaryOn, {{L"n", std::to_wstring(m_checkedRows)}}), SummaryPart::Tone::Change});
    }
    m_summaryLine->set(std::move(parts));
    const int count = static_cast<int>(entries.size());
    if (m_table->selected() >= count) {
        m_table->clearSelection();
    }
    m_table->setRowCount(count);
    m_table->refresh();
    layout();
    invalidate();
    detailChanged();
}

std::wstring RegistryPage::valueText(const core::RegistryWrite& write) const {
    const auto input = core::registryWriteInput(write);
    switch (input.type) {
    case core::RegValueType::DeleteValue: return m_strings.get(Str::RegistryTypeDeleteValue);
    case core::RegValueType::DeleteKey: return m_strings.get(Str::RegistryTypeDeleteKey);
    case core::RegValueType::String: return L"REG_SZ  " + input.data;
    case core::RegValueType::ExpandString: return L"REG_EXPAND_SZ  " + input.data;
    case core::RegValueType::MultiString: return L"REG_MULTI_SZ  " + input.data;
    case core::RegValueType::Dword: return L"REG_DWORD  " + input.data;
    case core::RegValueType::Qword: return L"REG_QWORD  " + input.data;
    case core::RegValueType::Binary: return L"REG_BINARY  " + input.data;
    }
    return {};
}

ui::TableCellText RegistryPage::cellText(int row, int column) const {
    const auto& entries = m_state.regImports();
    if (row < 0 || row >= static_cast<int>(entries.size()) || (column != kEntry && static_cast<std::size_t>(row) >= m_rowCache.size())) {
        return {};
    }
    const auto& entry = entries[static_cast<std::size_t>(row)];
    switch (column) {
    case kEntry: {
        if (!entry.typed) {
            return {entry.file.filename().wstring(), TypeStyle::Body, ui::Checkbox::kBox + kNameGap};
        }
        const auto& w = entry.writes.front();
        return {w.kind == core::RegistryWrite::Kind::DeleteKey ? m_strings.get(Str::RegistryWholeKey)
                : w.name.empty()                                ? m_strings.get(Str::RegistryValueNameDefault)
                                                                : w.name,
                TypeStyle::Body, ui::Checkbox::kBox + kNameGap};
    }
    case kKey: return {m_rowCache[static_cast<std::size_t>(row)].key, TypeStyle::Mono};
    case kValue:
        return entry.typed && !entry.writes.empty() ? ui::TableCellText{m_rowCache[static_cast<std::size_t>(row)].value, TypeStyle::Mono}
                                                    : ui::TableCellText{};
    default: return {};
    }
}

void RegistryPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto& entries = m_state.regImports();
    if (row < 0 || row >= static_cast<int>(entries.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(row);
    if (index >= m_rowCache.size()) {
        return; // refresh() has not seen this entry yet; it runs in the same notification
    }
    const auto& entry = entries[index];
    const RowCache& cached = m_rowCache[index];
    switch (column) {
    case kEntry: {
        const auto state = cached.checked ? ui::CheckState::On : ui::CheckState::Off;
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2}, state,
                               cell.hoveredCell, cell.checkReveal);
        // D-139: only the name — an imported file's "n değer" is the value column's (it said it twice).
        const float x = rect.x + ui::Checkbox::kBox + kNameGap;
        canvas.drawText(cached.name, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kKey: {
        if (entry.writes.empty()) {
            break;
        }
        canvas.drawText(cached.key, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    }
    case kValue:
        canvas.drawText(cached.value, rect, entry.typed ? TypeStyle::Mono : TypeStyle::Caption,
                        entry.typed ? Color::TextPrimary : Color::TextTertiary);
        break;
    case kScope: {
        const float w = std::ceil(canvas.text().measure(cached.scope, TypeStyle::Caption)) + 2 * kChipPad;
        const RectF badge{rect.x, rect.y + std::round((rect.height - kChipH) / 2), std::min(w, rect.width), kChipH};
        canvas.strokeRoundRect(badge, ui::tokens::radius::r2, Color::LineStrong);
        canvas.drawText(cached.scope, {badge.x + kChipPad, badge.y, std::max(badge.width - 2 * kChipPad, 0.0f), badge.height},
                        TypeStyle::Caption, Color::TextSecondary);
        if (entry.afterSetup) {
            // Re-applied after setup (D-026): imports always, typed values when chosen.
            const std::wstring& later = m_strings.get(Str::RegistryFirstLogon);
            const float lw = std::ceil(canvas.text().measure(later, TypeStyle::Caption)) + 2 * kChipPad;
            const RectF tag{badge.right() + kChipGap, badge.y, std::max(std::min(lw, rect.right() - badge.right() - kChipGap), 0.0f), kChipH};
            canvas.fillRoundRect(tag, ui::tokens::radius::r2, Color::AccentSubtle);
            canvas.drawText(later, {tag.x + kChipPad, tag.y, std::max(tag.width - 2 * kChipPad, 0.0f), tag.height}, TypeStyle::Caption,
                            Color::AccentText);
        }
        break;
    }
    case kRemove:
        canvas.drawIcon(ui::icons::Icon::Close, {rect.x, rect.y + std::round((rect.height - ui::tokens::size::icon) / 2)},
                        cell.hoveredCell ? Color::TextPrimary : Color::TextTertiary);
        break;
    default: break;
    }
}

void RegistryPage::layout() {
    // D-139: the header row — the section's title at the left (the page has no filter), the summary at
    // the right; the table under it.
    const RectF b = bounds();
    const float y = b.y + kTop;
    const float titleW = 220.0f;
    m_summaryLine->setBounds({b.x + titleW, y, std::max(b.width - titleW, 0.0f), kHeaderRow});
    const float top = y + kHeaderRow + kHeaderGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kValue, kScope}, kKeyMin);
    m_empty->setBounds(b);
}

void RegistryPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const int total = static_cast<int>(m_rowCache.size());
    // The header row's left: the section's title (the forms' one style), on the row's middle.
    canvas.drawText(m_strings.get(Str::RegistryCustomTitle), {b.x, b.y + kTop, 220.0f, kHeaderRow}, TypeStyle::Section,
                    Color::TextPrimary);
    if (total == 0) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::RegistryNoEntries));
        canvas.drawText(m_strings.get(Str::RegistryTweaksMoved),
                        {b.x, m_table->bounds().y + ui::TableView::kHeader + 36, b.width, 20}, TypeStyle::Caption,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
}

} // namespace wl::app

namespace wl::app {

DetailModel RegistryPage::detail() const {
    DetailModel model;
    const auto& entries = m_state.regImports();
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(entries.size()) || static_cast<std::size_t>(row) >= m_rowCache.size()) {
        return model;
    }
    const auto& entry = entries[static_cast<std::size_t>(row)];
    const RowCache& cached = m_rowCache[static_cast<std::size_t>(row)];
    const bool deletes = !entry.writes.empty() && std::ranges::all_of(entry.writes, [](const core::RegistryWrite& w) {
        return w.kind == core::RegistryWrite::Kind::DeleteValue || w.kind == core::RegistryWrite::Kind::DeleteKey;
    });
    model.icon = entry.typed ? ui::icons::Icon::Registry : ui::icons::Icon::RegFile;
    model.title = cached.name;
    model.subtitle = cached.scope;
    if (!entry.writes.empty()) {
        model.identity = entry.writes.front().key;
    }
    if (cached.checked) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(deletes ? Str::RegistryWillDelete : Str::RegistryWillWrite), DetailTone::Change};
    } else {
        model.state = {ui::icons::Icon::Close, m_strings.get(Str::RegistryOff), DetailTone::Quiet};
    }
    if (entry.typed && !entry.writes.empty()) {
        const auto input = core::registryWriteInput(entry.writes.front());
        const std::wstring& value = cached.value;
        const auto gap = value.find(L"  ");
        model.facts.push_back({m_strings.get(Str::RegistryValueType), gap == std::wstring::npos ? value : value.substr(0, gap), DetailTone::Normal, true});
        if (!input.data.empty()) {
            model.facts.push_back({m_strings.get(Str::RegistryValueData), input.data, DetailTone::Normal, true});
        }
    } else {
        if (entry.skipped > 0) {
            model.notes.push_back({ui::icons::Icon::WarningTriangle,
                                   m_strings.format(Str::RegistrySkippedN, {{L"n", std::to_wstring(entry.skipped)}}), DetailTone::Warning});
        }
        model.facts.push_back({m_strings.get(Str::RegistryFile), entry.file.filename().wstring()});
    }
    model.facts.push_back({m_strings.get(Str::RegistryScope), cached.scope});
    model.facts.push_back({m_strings.get(Str::RegistryAfterSetupFact),
                           m_strings.get(entry.afterSetup ? Str::RegistryAfterSetupYes : Str::RegistryAfterSetupNo)});
    if (!entry.typed && !entry.writes.empty()) {
        // What the file writes: the first lines (a .reg can hold thousands), the rest counted.
        DetailList list{m_strings.format(Str::RegistryValuesN, {{L"n", formatCount(entry.writes.size(), m_language)}}), {}, true};
        for (std::size_t i = 0; i < entry.writes.size() && i < static_cast<std::size_t>(kListLines); ++i) {
            const auto& w = entry.writes[i];
            const std::wstring name = w.kind == core::RegistryWrite::Kind::DeleteKey ? m_strings.get(Str::RegistryWholeKey)
                                      : w.name.empty()                                ? std::wstring(L"@")
                                                                                      : w.name;
            list.lines.push_back(name + L" = " + valueText(w));
        }
        if (entry.writes.size() > static_cast<std::size_t>(kListLines)) {
            list.lines.push_back(m_strings.format(Str::DetailMore, {{L"n", formatCount(entry.writes.size() - kListLines, m_language)}}));
        }
        model.lists.push_back(std::move(list));
    }
    if (entry.typed) {
        model.actions.push_back({kActionEdit, m_strings.get(Str::RegistryEdit)});
    }
    model.actions.push_back({kActionToggle, m_strings.get(cached.checked ? Str::RegistryTurnOff : Str::RegistryTurnOn)});
    model.actions.push_back({kActionRemove, m_strings.get(Str::RegistryRemove)});
    return model;
}

void RegistryPage::detailAction(int id) {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_state.regImports().size())) {
        return;
    }
    switch (id) {
    case kActionEdit:
        if (m_intents.edit) {
            m_intents.edit(static_cast<std::size_t>(row)); // Enter / double click on a typed value
        }
        break;
    case kActionToggle: toggleEntry(row); break; // Space
    case kActionRemove: m_controller.remove(static_cast<std::size_t>(row)); break; // Delete / the row's ✕
    default: break;
    }
}

} // namespace wl::app
