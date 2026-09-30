#include "app/pages/FilesPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kDrop = 56.0f;
enum Column : int { kSource, kTarget, kSize, kRisk };
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
        {strings.get(Str::FilesTarget), 360},
        {strings.get(Str::CommonSize), 96, ui::TextAlign::Trailing},
        {strings.get(Str::CommonRisk), 90},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_rows.size())) {
            m_state.unqueue(OpKind::CopyTree, m_rows[static_cast<std::size_t>(row)].target);
            return true;
        }
        return false;
    };
    m_table->onSelect = [this](int) { invalidate(); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Folder, strings.get(Str::FilesNoMountTitle), strings.get(Str::FilesNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = std::move(goImages);
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
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    layout();
    invalidate();
}

void FilesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& op = m_rows[static_cast<std::size_t>(row)];
    switch (column) {
    case kSource: {
        std::error_code ec;
        const bool folder = std::filesystem::is_directory(op.value, ec);
        canvas.drawIcon(folder ? ui::icons::Icon::Folder : ui::icons::Icon::File, {rect.x, rect.y + 4}, Color::TextSecondary);
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
    case kRisk: paintRisk(canvas, rect, op.risk, m_strings); break;
    default: break;
    }
}

void FilesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    y += kDrop + 16;
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y - kDetailLine, 0.0f)});
}

void FilesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const RectF t = m_table->bounds();
    if (m_rows.empty()) {
        canvas.drawText(m_strings.get(Str::FilesEmpty), {t.x, t.y + ui::TableView::kHeader + 12, t.width, 20}, TypeStyle::Body,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
    std::int64_t total = 0;
    for (const auto& op : m_rows) {
        total += std::max<std::int64_t>(op.sizeDelta, 0);
    }
    paintDetail(canvas, {b.x, t.bottom(), b.width, kDetailLine}, L"",
                m_strings.format(Str::FilesSummary, {{L"n", std::to_wstring(m_rows.size())},
                                                     {L"size", formatBytes(static_cast<std::uint64_t>(total), m_language)}}));
}

} // namespace wl::app
