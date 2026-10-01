#include "app/pages/HostsPage.h"

#include "app/pages/PageBits.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kSummary = 24.0f;
enum Column : int { kList, kEntries, kRisk, kState };
} // namespace

HostsPage::HostsPage(AppState& state, HostsController& controller, const Localization& strings, Language language,
                     std::function<void()> goImages, std::function<void()> importFile)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_importFile(std::move(importFile)) {
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::HostsList), 0},
        {strings.get(Str::HostsEntries), 110, ui::TextAlign::Trailing},
        {strings.get(Str::CommonRisk), 90},
        {strings.get(Str::CommonStatus), 170},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kList && p.x < m_table->cellRect(row, kList).x + ui::TableView::kCellPad + ui::Checkbox::kBox + 6) {
            activate(row);
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_table->onSelect = [this](int) { invalidate(); };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        if (key.virtualKey == VK_DELETE && isCustomRow(m_table->selected())) {
            m_controller.clearCustom();
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Network, strings.get(Str::HostsNoMountTitle), strings.get(Str::HostsNoMountBody));
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
    m_table->refresh();
    layout();
    invalidate();
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
        m_controller.toggle(m_controller.lists()[static_cast<std::size_t>(row)]);
    }
}

void HostsPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const bool custom = isCustomRow(row);
    const HostsList* list = custom ? nullptr : &m_controller.lists()[static_cast<std::size_t>(row)];
    const auto customEntries = custom ? m_controller.customEntries() : std::vector<core::HostEntry>{};
    switch (column) {
    case kList: {
        const bool on = custom ? !customEntries.empty() : m_controller.on(*list);
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               on ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell);
        const float x = rect.x + ui::Checkbox::kBox + 8;
        const std::wstring name = custom ? m_strings.get(Str::HostsCustom) : list->name.get(m_language);
        canvas.drawText(name, {x, rect.y, rect.right() - x, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    case kEntries: {
        const std::size_t n = custom ? customEntries.size() : list->entries.size();
        canvas.drawText(std::to_wstring(n), rect, TypeStyle::Mono, n ? Color::TextPrimary : Color::TextTertiary,
                        ui::TextAlign::Trailing);
        break;
    }
    case kRisk:
        if (!custom) {
            paintRisk(canvas, rect, list->risk, m_strings);
        }
        break;
    case kState: {
        const auto* queued = m_state.changes().find(core::ops::OpKind::SetHosts, custom ? HostsController::kCustom : list->id);
        const bool image = custom ? m_controller.customInImage() : m_controller.inImage(*list);
        Str text = Str::HostsStateOff;
        Color ink = Color::TextTertiary;
        if (queued) {
            text = queued->value.empty() ? Str::HostsWillRemove : Str::HostsWillWrite;
            ink = Color::AccentBase;
        } else if (image) {
            text = Str::HostsInImage;
        }
        canvas.drawText(m_strings.get(text), rect, TypeStyle::Caption, ink);
        break;
    }
    default: break;
    }
}

void HostsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    const float top = b.y + kTop + kSummary + 8;
    m_table->setBounds({b.x, top, b.width, std::min(ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(m_table->rowCount()) + 1,
                                                    std::max(b.bottom() - top - kDetailLine, 0.0f))});
}

void HostsPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    canvas.drawText(m_strings.format(Str::HostsSummary, {{L"n", std::to_wstring(m_controller.totalEntries())}}),
                    {b.x, b.y + kTop, b.width, kSummary}, TypeStyle::Body, Color::TextSecondary);
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

} // namespace wl::app
