#include "app/pages/RegistryPage.h"

#include "app/pages/PageBits.h"
#include "core/image/RegistryInput.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kSectionH = 20.0f;
constexpr float kSectionGap = 4.0f;

enum Column : int { kEntry, kKey, kValue, kScope };
} // namespace

RegistryPage::RegistryPage(AppState& state, RegistryController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_intents(std::move(intents)) {
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::RegistryEntry), 0},
        {strings.get(Str::RegistryKey), 340},
        {strings.get(Str::RegistryValue), 220},
        {strings.get(Str::RegistryScope), 250},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kEntry) {
            const RectF cell = m_table->cellRect(row, kEntry);
            if (p.x < cell.x + ui::Checkbox::kBox + 8) {
                m_controller.toggle(static_cast<std::size_t>(row));
                return;
            }
        }
        if (column == kScope) {
            const RectF cell = m_table->cellRect(row, kScope);
            if (p.x > cell.right() - ui::tokens::size::icon - 4) { // trailing ✕ = remove the entry
                m_controller.remove(static_cast<std::size_t>(row));
            }
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (row < 0) {
            return false;
        }
        if (key.virtualKey == VK_SPACE) {
            m_controller.toggle(static_cast<std::size_t>(row));
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
        m_controller.toggle(index);
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
    const int count = static_cast<int>(m_state.regImports().size());
    if (m_table->selected() >= count) {
        m_table->clearSelection();
    }
    m_table->setRowCount(count);
    m_table->refresh();
    layout();
    invalidate();
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

void RegistryPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto& entries = m_state.regImports();
    if (row < 0 || row >= static_cast<int>(entries.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(row);
    const auto& entry = entries[index];
    switch (column) {
    case kEntry: {
        const auto state = m_controller.checked(index) ? ui::CheckState::On : ui::CheckState::Off;
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2}, state,
                               cell.hoveredCell);
        std::wstring name;
        std::wstring sub;
        if (entry.typed) {
            const auto& w = entry.writes.front();
            name = w.kind == core::RegistryWrite::Kind::DeleteKey ? m_strings.get(Str::RegistryWholeKey)
                   : w.name.empty()                                ? m_strings.get(Str::RegistryValueNameDefault)
                                                                   : w.name;
        } else {
            name = entry.file.filename().wstring();
            sub = m_strings.format(Str::RegistryValuesN, {{L"n", std::to_wstring(entry.writes.size())}});
            if (entry.skipped > 0) {
                sub += L" · " + m_strings.format(Str::RegistrySkippedN, {{L"n", std::to_wstring(entry.skipped)}});
            }
        }
        const float x = rect.x + ui::Checkbox::kBox + 8;
        const float nameW = sub.empty() ? rect.right() - x
                                        : std::min(rect.right() - x, std::ceil(canvas.text().measure(name, TypeStyle::Body)));
        canvas.drawText(name, {x, rect.y, nameW, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        if (!sub.empty()) {
            const float sx = x + nameW + 8;
            canvas.drawText(sub, {sx, rect.y, std::max(rect.right() - sx, 0.0f), rect.height}, TypeStyle::Caption,
                            Color::TextTertiary);
        }
        break;
    }
    case kKey: {
        if (entry.writes.empty()) {
            break;
        }
        std::wstring key = entry.writes.front().key;
        const auto keys = std::ranges::count_if(entry.writes, [&](const core::RegistryWrite& w) { return w.key != key; });
        if (keys > 0) {
            key += std::format(L"  +{}", keys);
        }
        canvas.drawText(key, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    }
    case kValue: {
        const std::wstring text = entry.typed && !entry.writes.empty()
                                      ? valueText(entry.writes.front())
                                      : m_strings.format(Str::RegistryValuesN, {{L"n", std::to_wstring(entry.writes.size())}});
        canvas.drawText(text, rect, entry.typed ? TypeStyle::Mono : TypeStyle::Caption,
                        entry.typed ? Color::TextPrimary : Color::TextTertiary);
        break;
    }
    case kScope: {
        bool user = false;
        bool system = false;
        for (const auto& w : entry.writes) {
            (core::isUserKey(w.key) ? user : system) = true;
        }
        const std::wstring text = user && system ? m_strings.get(Str::RegistryScopeSystem) + L" + " +
                                                       m_strings.get(Str::RegistryScopeUser)
                                  : user ? m_strings.get(Str::RegistryScopeUser)
                                         : m_strings.get(Str::RegistryScopeSystem);
        const float w = std::ceil(canvas.text().measure(text, TypeStyle::Caption)) + 12;
        const RectF badge{rect.x, rect.y + (rect.height - 16) / 2, w, 16};
        canvas.strokeRoundRect(badge, ui::tokens::radius::r2, Color::LineStrong);
        canvas.drawText(text, {badge.x + 6, badge.y, badge.width - 12, badge.height}, TypeStyle::Caption,
                        Color::TextSecondary);
        if (entry.afterSetup) {
            // Re-applied after setup (D-026): imports always, typed values when chosen.
            const std::wstring later = m_strings.get(Str::RegistryFirstLogon);
            const float lw = std::ceil(canvas.text().measure(later, TypeStyle::Caption)) + 12;
            const RectF tag{badge.right() + 6, badge.y, lw, 16};
            canvas.fillRoundRect(tag, ui::tokens::radius::r2, Color::AccentSubtle);
            canvas.drawText(later, {tag.x + 6, tag.y, tag.width - 12, tag.height}, TypeStyle::Caption, Color::AccentBase);
        }
        canvas.drawIcon(ui::icons::Icon::Close, {rect.right() - ui::tokens::size::icon, rect.y + 4},
                        cell.hoveredCell ? Color::TextPrimary : Color::TextTertiary);
        break;
    }
    default: break;
    }
}

void RegistryPage::layout() {
    const RectF b = bounds();
    const float top = b.y + kTop + kSectionH + kSectionGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    m_empty->setBounds(b);
}

void RegistryPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const auto [on, total] = m_controller.selection();
    canvas.drawText(m_strings.get(Str::RegistryCustomTitle), {b.x, b.y + kTop, b.width, kSectionH}, TypeStyle::Section,
                    Color::TextSecondary);
    canvas.drawText(m_strings.format(Str::RegistrySelectedOf, {{L"s", std::to_wstring(on)}, {L"n", std::to_wstring(total)}}),
                    {b.x, b.y + kTop, b.width, kSectionH}, TypeStyle::Mono, Color::TextTertiary, ui::TextAlign::Trailing);
    if (total == 0) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::RegistryNoEntries));
        canvas.drawText(m_strings.get(Str::RegistryTweaksMoved),
                        {b.x, m_table->bounds().y + ui::TableView::kHeader + 36, b.width, 20}, TypeStyle::Caption,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
}

} // namespace wl::app
