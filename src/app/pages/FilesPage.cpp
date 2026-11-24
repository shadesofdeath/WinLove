#include "app/pages/FilesPage.h"
#include "app/pages/PageHeader.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 0.0f;
constexpr float kDrop = 56.0f;
enum Column : int { kSource, kMarks, kTarget, kSize }; // 2c: the risk's dot in kMarks (was a Risk word column last)
} // namespace

FilesPage::FilesPage(AppState& state, FilesController& controller, const Localization& strings, Language language,
                     std::function<void()> addFiles, std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_drop = &add<ui::DropZone>(strings.get(Str::FilesDrop), strings.get(Str::FilesDrop), strings.get(Str::FilesDrop),
                                strings.get(Str::FilesDrop));
    m_drop->setCompact(true);
    m_drop->onInvoke = std::move(addFiles);
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::FilesSource), 0},
        {L"", marksColumnWidth(1)},
        {.title = strings.get(Str::FilesTarget), .width = 414, .minWidth = 240},
        {strings.get(Str::CommonSize), 110, ui::TextAlign::Trailing, false, 90},
    });
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        if (column != kMarks || row < 0 || row >= static_cast<int>(m_rows.size()) ||
            m_rows[static_cast<std::size_t>(row)].risk == core::ops::Risk::Low) {
            return {};
        }
        return marksTooltip(m_strings, RowMarks{RowMark::None, m_rows[static_cast<std::size_t>(row)].risk}, {}, {},
                            m_strings.get(Str::FilesRiskNote));
    };
    // D-136: the source path keeps its room at a narrow window; paths cut there show whole on hover.
    m_table->setFlexMin(kNameColumnMin);
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return {};
        }
        const auto& op = m_rows[static_cast<std::size_t>(row)];
        return column == kSource   ? ui::TableCellText{op.value, TypeStyle::Body, ui::tokens::size::icon + 6}
               : column == kTarget ? ui::TableCellText{FilesController::displayPath(op.target), TypeStyle::Mono}
                                   : ui::TableCellText{};
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_rows.size())) {
            removeRow(row);
            return true;
        }
        return false;
    };
    m_table->onSelect = [this](int) {
        invalidate();
        detailChanged();
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Folder, strings.get(Str::FilesNoMountTitle), strings.get(Str::FilesNoMountBody));
    m_summaryLine = &add<SummaryLine>();
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::FilesTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Queue) {
            refresh();
        }
    });
    refresh();
}

FilesPage::~FilesPage() {
    m_state.unsubscribe(m_subscription);
}

void FilesPage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

void FilesPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_drop->setVisible(mounted);
    m_table->setVisible(mounted);
    m_rows = m_controller.queued();
    std::map<std::wstring, bool> folders;
    m_rowIsFolder.clear();
    m_rowIsFolder.reserve(m_rows.size());
    std::int64_t total = 0;
    for (const auto& op : m_rows) {
        bool folder = false;
        if (const auto known = m_folders.find(op.value); known != m_folders.end()) {
            folder = known->second;
        } else {
            std::error_code ec;
            folder = std::filesystem::is_directory(op.value, ec);
        }
        folders[op.value] = folder;
        m_rowIsFolder.push_back(folder);
        total += std::max<std::int64_t>(op.sizeDelta, 0);
    }
    m_folders = std::move(folders); // sources that left the queue are forgotten
    // D-139: what Uygula copies, at the header row's right (it was the line under the table).
    std::vector<SummaryPart> parts;
    if (!m_rows.empty()) {
        parts.push_back({m_strings.format(Str::FilesSummary, {{L"n", std::to_wstring(m_rows.size())},
                                                              {L"size", formatBytes(static_cast<std::uint64_t>(total), m_language)}}),
                         SummaryPart::Tone::Change});
    }
    m_summaryLine->set(std::move(parts));
    m_summaryLine->setVisible(m_state.mounted().has_value());
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    layout();
    invalidate();
    detailChanged();
}

void FilesPage::removeRow(int row) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        m_state.unqueue(OpKind::CopyTree, m_rows[static_cast<std::size_t>(row)].target);
    }
}

void FilesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& op = m_rows[static_cast<std::size_t>(row)];
    switch (column) {
    case kSource: {
        const bool folder = static_cast<std::size_t>(row) < m_rowIsFolder.size() && m_rowIsFolder[static_cast<std::size_t>(row)];
        canvas.drawIcon(folder ? ui::icons::Icon::Folder : ui::icons::Icon::File, {rect.x, rect.y + (rect.height - 16) / 2}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(op.value, {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kTarget:
        canvas.drawText(FilesController::displayPath(op.target), rect, TypeStyle::Mono, Color::TextSecondary);
        break;
    case kSize:
        canvas.drawText(formatBytes(static_cast<std::uint64_t>(std::max<std::int64_t>(op.sizeDelta, 0)), m_language), rect,
                        TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    case kMarks: paintRiskMark(canvas, rect, op.risk); break;
    default: break;
    }
}

void FilesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    y += kDrop + kHeaderGap;
    m_titleY = y;
    m_summaryLine->setBounds({b.x + 220.0f, y, std::max(b.width - 220.0f, 0.0f), kHeaderRow});
    y += kHeaderRow + kHeaderGap;
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kSize}); // beside the card the size is the card's
}

void FilesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const RectF t = m_table->bounds();
    canvas.drawText(m_strings.get(Str::FilesTitle), {b.x, m_titleY, 220.0f, kHeaderRow}, TypeStyle::Section, Color::TextPrimary);
    if (m_rows.empty()) {
        paintTableEmpty(canvas, t, m_strings.get(Str::FilesEmpty));
    }
}

DetailModel FilesPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return model;
    }
    const auto& op = m_rows[static_cast<std::size_t>(row)];
    const bool folder = static_cast<std::size_t>(row) < m_rowIsFolder.size() && m_rowIsFolder[static_cast<std::size_t>(row)];
    const std::filesystem::path source(op.value);
    const std::wstring where = FilesController::displayPath(op.target);
    const std::wstring size = formatBytes(static_cast<std::uint64_t>(std::max<std::int64_t>(op.sizeDelta, 0)), m_language);
    const std::wstring kind = m_strings.get(folder ? Str::FilesKindFolder : Str::FilesKindFile);
    model.icon = folder ? ui::icons::Icon::Folder : ui::icons::Icon::File;
    model.title = source.filename().empty() ? op.value : source.filename().wstring();
    model.subtitle = kind + L" · " + size;
    // The list is the queue: every row is copied at Uygula.
    model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::FilesStateQueued), DetailTone::Change};
    // The whole source path (the table cuts it; a one-line identity would too), wrapped, and where it lands.
    model.about = m_strings.format(Str::FilesCardAbout, {{L"source", op.value}, {L"path", where}});
    model.facts.push_back({m_strings.get(Str::FilesTarget), where, DetailTone::Normal, true});
    model.facts.push_back({m_strings.get(Str::CommonSize), size});
    model.facts.push_back({m_strings.get(Str::CommonType), kind});
    model.facts.push_back({m_strings.get(Str::CommonRisk), m_strings.get(riskText(op.risk)),
                           op.risk == core::ops::Risk::Low    ? DetailTone::Success
                           : op.risk == core::ops::Risk::High ? DetailTone::Error
                                                              : DetailTone::Warning,
                           false, true});
    if (op.risk != core::ops::Risk::Low) {
        model.notes.push_back({ui::icons::Icon::WarningTriangle, m_strings.get(Str::FilesRiskNote), DetailTone::Warning});
    }
    model.actions.push_back({kActionRemove, m_strings.get(Str::FilesActionRemove)});
    return model;
}

void FilesPage::detailAction(int id) {
    if (id == kActionRemove) {
        removeRow(m_table->selected()); // the row's own Delete
    }
}

} // namespace wl::app
