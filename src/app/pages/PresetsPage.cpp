#include "app/pages/PresetsPage.h"

#include "app/pages/PageBits.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Mark = PresetController::DiffRow::Mark;

namespace {

constexpr float kTop = 11.0f;
constexpr float kListWidth = 360.0f;
constexpr float kPanelGap = 24.0f;
constexpr float kControl = ui::tokens::size::control;
constexpr float kButtons = 36.0f;     // the button row under the list
constexpr float kCompareTop = 32.0f;  // section title band → A / B row
constexpr float kChoiceWidth = 200.0f;
constexpr float kDiffTop = 72.0f;     // A / B row → diff header

enum ListColumn : int { kName, kCount };
enum DiffColumn : int { kMark, kCategory, kItem, kA, kB };

Color markColor(Mark mark) {
    switch (mark) {
    case Mark::Added: return Color::StatusSuccess;
    case Mark::Removed: return Color::StatusError;
    case Mark::Changed: return Color::StatusWarning;
    default: return Color::TextTertiary;
    }
}

const wchar_t* markSign(Mark mark) {
    switch (mark) {
    case Mark::Added: return L"+";
    case Mark::Removed: return L"-";
    case Mark::Changed: return L"~";
    default: return L"";
    }
}

} // namespace

PresetsPage::PresetsPage(AppState& state, PresetController& controller, const Localization& strings, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_intents(std::move(intents)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_list = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {s(Str::PresetsPreset), 0},
        {s(Str::PresetsChanges), 96, ui::TextAlign::Trailing},
    });
    m_list->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintListCell(c, row, column, rect, cell);
    };
    m_list->onSelect = [this](int row) {
        // The selected preset is what the comparison starts from.
        if (row >= 0) {
            m_a->setSelected(row);
        }
        updateDiff();
    };
    auto selected = [this]() -> std::optional<std::size_t> {
        const int row = m_list->selected();
        return row >= 0 && row < static_cast<int>(m_controller.presets().size()) ? std::optional<std::size_t>(row)
                                                                                 : std::nullopt;
    };
    m_list->onActivate = [this](int row) {
        if (row >= 0 && m_intents.load) {
            m_intents.load(static_cast<std::size_t>(row));
        }
    };
    m_list->onKey = [this, selected](const ui::KeyEvent& key) {
        if (key.virtualKey == VK_DELETE) {
            if (const auto index = selected(); index && m_intents.remove) {
                m_intents.remove(*index);
            }
            return true;
        }
        return false;
    };
    auto action = [selected](std::function<void(std::size_t)>& intent) {
        return [selected, &intent] {
            if (const auto index = selected(); index && intent) {
                intent(*index);
            }
        };
    };
    m_load = &add<ui::Button>(ui::ButtonKind::Primary, s(Str::PresetsLoad));
    m_load->onInvoke = action(m_intents.load);
    m_export = &add<ui::Button>(ui::ButtonKind::Secondary, s(Str::CommonExport));
    m_export->onInvoke = action(m_intents.exportPreset);
    m_delete = &add<ui::Button>(ui::ButtonKind::Secondary, s(Str::CommonDelete));
    m_delete->onInvoke = action(m_intents.remove);

    m_a = &add<ui::Dropdown>(L"A", std::vector<std::wstring>{}, 0);
    m_b = &add<ui::Dropdown>(L"B", std::vector<std::wstring>{}, 0);
    m_a->onChange = [this](int) { updateDiff(); };
    m_b->onChange = [this](int) { updateDiff(); };
    m_swap = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::Compare);
    m_swap->setTooltip(L"A ⇄ B");
    m_swap->onInvoke = [this] {
        const int a = m_a->selected();
        m_a->setSelected(m_b->selected());
        m_b->setSelected(a);
        updateDiff();
    };
    m_same = &add<ui::Toggle>(s(Str::PresetsShowSame), false);
    m_same->onChange = [this](bool) { updateDiff(); };

    m_diff = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 20},
        {s(Str::CommonCategory), 132},
        {s(Str::PresetsItem), 0},
        {L"A", 140},
        {L"B", 148},
    });
    m_diff->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintDiffCell(c, row, column, rect);
    };
    setAccessible(ui::AccessRole::Group, s(Str::PresetsTitle));

    // "Geçerli kuyruk" is one of the choices: the diff follows the queue and the answer file.
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Queue || change == AppState::Change::Unattend || change == AppState::Change::Mount) {
            updateDiff();
        }
    });
    reloadList();
}

PresetsPage::~PresetsPage() {
    m_state.unsubscribe(m_subscription);
}

Preset PresetsPage::choice(int index) const {
    const auto& presets = m_controller.presets();
    if (index >= 0 && index < static_cast<int>(presets.size())) {
        return presets[static_cast<std::size_t>(index)];
    }
    return m_controller.current();
}

void PresetsPage::reloadList() {
    const auto& presets = m_controller.presets();
    const int count = static_cast<int>(presets.size());
    m_counts.clear();
    std::vector<std::wstring> names;
    for (const auto& preset : presets) {
        m_counts.push_back(m_controller.items(preset).size());
        names.push_back(preset.name);
    }
    names.push_back(m_strings.get(Str::PresetsCurrentQueue));
    // Keep A / B where they were when they still exist; B starts on the current queue.
    const int a = std::clamp(m_a->selected(), 0, count);
    m_a->setItems(names, a);
    m_b->setItems(std::move(names), count);
    m_list->setRowCount(count);
    if (count > 0 && m_list->selected() < 0) {
        m_list->setSelected(0);
    }
    updateDiff();
}

void PresetsPage::updateDiff() {
    m_rows = m_controller.diff(choice(m_a->selected()), choice(m_b->selected()), m_same->isOn());
    m_summary = PresetController::summarize(m_rows);
    m_diff->setRowCount(static_cast<int>(m_rows.size()));
    m_diff->refresh();
    const bool any = m_list->selected() >= 0 && m_list->selected() < static_cast<int>(m_controller.presets().size());
    for (ui::Button* button : {m_load, m_export, m_delete}) {
        button->setEnabled(any);
    }
    m_list->refresh();
    invalidate();
}

void PresetsPage::paintListCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto& presets = m_controller.presets();
    if (row < 0 || row >= static_cast<int>(presets.size())) {
        return;
    }
    if (column == kName) {
        canvas.drawIcon(ui::icons::Icon::PresetBookmark, {rect.x, rect.y + (rect.height - ui::tokens::size::icon) / 2},
                        Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(presets[static_cast<std::size_t>(row)].name, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
    } else {
        canvas.drawText(std::to_wstring(m_counts[static_cast<std::size_t>(row)]), rect, TypeStyle::Mono, Color::TextSecondary,
                        ui::TextAlign::Trailing);
    }
}

void PresetsPage::paintDiffCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& diff = m_rows[static_cast<std::size_t>(row)];
    auto value = [&](const std::wstring& text) {
        if (text.empty()) {
            canvas.drawText(m_strings.get(Str::CommonNone), rect, TypeStyle::Body, Color::TextTertiary);
        } else {
            canvas.drawText(text, rect, TypeStyle::Body, Color::TextPrimary);
        }
    };
    switch (column) {
    case kMark:
        if (diff.mark != Mark::Same) {
            // 2px bar at the row's left edge + the sign, both in the status color.
            const float left = rect.x - ui::TableView::kCellPad;
            canvas.fillRect({left, rect.y + 4, 2, rect.height - 8}, markColor(diff.mark));
            canvas.drawText(markSign(diff.mark), {left + 10, rect.y, 12, rect.height}, TypeStyle::Mono, markColor(diff.mark));
        }
        break;
    case kCategory:
        canvas.drawText(m_controller.categoryName(diff.category), rect, TypeStyle::Body, Color::TextSecondary);
        break;
    case kItem: canvas.drawText(diff.label, rect, TypeStyle::Body, Color::TextPrimary); break;
    case kA: value(diff.a); break;
    case kB: value(diff.b); break;
    default: break;
    }
}

void PresetsPage::layout() {
    const RectF b = bounds();
    const float top = b.y + kTop;
    const float listWidth = std::min(kListWidth, std::max(b.width * 0.4f, 0.0f));
    m_list->setBounds({b.x, top, listWidth, std::max(b.bottom() - top - kButtons, 0.0f)});
    float x = b.x;
    const float buttonY = b.bottom() - kControl - 4;
    for (ui::Button* button : {m_load, m_export, m_delete}) {
        const float width = button->measure({}).width;
        button->setBounds({x, buttonY, width, kControl});
        x += width + 8;
    }

    const float panelX = b.x + listWidth + kPanelGap;
    const float rowY = top + kCompareTop;
    m_a->setBounds({panelX, rowY, kChoiceWidth, kControl});
    m_swap->setBounds({panelX + kChoiceWidth + 8, rowY, 28, kControl});
    m_b->setBounds({panelX + kChoiceWidth + 44, rowY, kChoiceWidth, kControl});
    m_same->setBounds({panelX + 2 * kChoiceWidth + 60, rowY, m_same->measure({}).width, kControl});
    const float diffY = top + kDiffTop;
    m_diff->setBounds({panelX, diffY, std::max(b.right() - panelX, 0.0f), std::max(b.bottom() - diffY, 0.0f)});
}

void PresetsPage::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float top = b.y + kTop;
    const float panelX = m_diff->bounds().x;
    // "KARŞILAŞTIR" band, level with the list header.
    canvas.drawText(m_strings.get(Str::PresetsCompare), {panelX, top, b.right() - panelX, ui::TableView::kHeader},
                    TypeStyle::Section, Color::TextTertiary);
    canvas.hairlineH(panelX, top + ui::TableView::kHeader - 1, b.right() - panelX, Color::LineSubtle);
    const std::wstring summary =
        m_strings.format(Str::PresetsDiffSummary, {{L"a", std::to_wstring(m_summary.added)},
                                                   {L"r", std::to_wstring(m_summary.removed)},
                                                   {L"c", std::to_wstring(m_summary.changed)}});
    const float summaryX = m_same->bounds().right() + 12;
    canvas.drawText(summary, {summaryX, top + kCompareTop, std::max(b.right() - summaryX, 0.0f), kControl}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
    if (m_controller.presets().empty()) {
        const RectF list = m_list->bounds();
        canvas.drawTextWrapped(m_strings.get(Str::PresetsEmpty),
                               {list.x + 8, list.y + ui::TableView::kHeader + 12, list.width - 16, 48}, TypeStyle::Body,
                               Color::TextTertiary, ui::TextAlign::Center);
    }
    if (m_rows.empty()) {
        const RectF diff = m_diff->bounds();
        paintTableEmpty(canvas, diff, m_strings.get(Str::PresetsNoDiff));
    }
}

} // namespace wl::app
