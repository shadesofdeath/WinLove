#include "app/pages/UpdatesPage.h"

#include "app/Format.h"

#include <format>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kDrop = 56.0f;
enum Column : int { kHandle, kOrder, kPackage, kKind, kKb, kSize, kState };
} // namespace

UpdatesPage::UpdatesPage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_drop = &add<ui::DropZone>(strings.get(Str::UpdatesDrop), strings.get(Str::UpdatesDrop),
                                strings.get(Str::SourceDropUnsupported), strings.get(Str::UpdatesDrop));
    m_drop->setCompact(true);
    m_drop->onInvoke = [this] {
        if (m_intents.addPackages) {
            m_intents.addPackages();
        }
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 24},
        {strings.get(Str::UpdatesOrder), 48},
        {strings.get(Str::UpdatesPackage), 0},
        {strings.get(Str::UpdatesType), 80},
        {strings.get(Str::UpdatesKb), 110},
        {strings.get(Str::CommonSize), 90, ui::TextAlign::Trailing},
        {strings.get(Str::CommonStatus), 190},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_rows.size())) {
            m_state.unqueue(OpKind::AddPackage, m_rows[static_cast<std::size_t>(row)].target);
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::UpdateDownload, strings.get(Str::UpdatesNoMountTitle),
                                   strings.get(Str::UpdatesNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = [this] {
        if (m_intents.goImages) {
            m_intents.goImages();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::UpdatesTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Queue || change == AppState::Change::Mount) {
            refresh();
        }
    });
    refresh();
}

UpdatesPage::~UpdatesPage() {
    m_state.unsubscribe(m_subscription);
}

std::size_t UpdatesPage::queuePackages(AppState& state, const std::vector<std::filesystem::path>& files) {
    std::size_t added = 0;
    for (const auto& file : files) {
        if (!core::isUpdateFile(file) || state.changes().find(OpKind::AddPackage, file.wstring())) {
            continue;
        }
        const auto info = core::analyzeUpdate(file);
        core::ops::Operation op{OpKind::AddPackage, file.wstring(), core::updateKindKey(info.kind)};
        op.risk = core::ops::Risk::Low;
        op.sizeDelta = static_cast<std::int64_t>(info.size); // the image grows
        state.queue(std::move(op));
        ++added;
    }
    return added;
}

void UpdatesPage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

const core::UpdateInfo& UpdatesPage::infoFor(const std::filesystem::path& path) {
    auto it = m_info.find(path.wstring());
    if (it == m_info.end()) {
        it = m_info.emplace(path.wstring(), core::analyzeUpdate(path)).first;
    }
    return it->second;
}

UpdatesPage::Compat UpdatesPage::compatibility(const core::UpdateInfo& info) const {
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (!mounted || !source) {
        return Compat::Unknown;
    }
    for (const auto& image : source->install.images) {
        if (image.index != mounted->index) {
            continue;
        }
        if (info.targetWindows != 0 && info.targetWindows != core::windowsGeneration(image.build)) {
            return Compat::WrongWindows;
        }
        const std::wstring arch = core::architectureName(image.architecture);
        if (!info.architecture.empty() && info.architecture != arch) {
            return Compat::WrongArch;
        }
        return info.targetWindows == 0 ? Compat::Unknown : Compat::Ok;
    }
    return Compat::Unknown;
}

void UpdatesPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_drop->setVisible(mounted);
    m_table->setVisible(mounted);
    m_rows.clear();
    for (const auto& step : core::ops::plan(m_state.changes()).steps) {
        if (step.operation.kind == OpKind::AddPackage) {
            m_rows.push_back(step.operation);
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    layout();
    invalidate();
}

void UpdatesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& op = m_rows[static_cast<std::size_t>(row)];
    const auto& info = infoFor(op.target);
    switch (column) {
    case kHandle: canvas.drawIcon(ui::icons::Icon::DragHandle, {rect.x - 4, rect.y + 4}, Color::TextTertiary); break;
    case kOrder: canvas.drawText(std::to_wstring(row + 1), rect, TypeStyle::Body, Color::TextPrimary); break;
    case kPackage: {
        canvas.drawIcon(info.path.extension() == L".cab" ? ui::icons::Icon::CabFile : ui::icons::Icon::MsuFile,
                        {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(info.path.filename().wstring(), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    case kKind: {
        static constexpr Str kKinds[] = {Str::UpdatesKindSsu, Str::UpdatesKindLcu, Str::UpdatesKindDotnet, Str::UpdatesKindOther};
        canvas.drawText(m_strings.get(kKinds[static_cast<int>(info.kind)]), rect, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kKb:
        canvas.drawText(info.kb.empty() ? std::wstring(L"—") : info.kb, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    case kSize:
        canvas.drawText(info.size ? formatBytes(info.size, m_language) : std::wstring(L"—"), rect, TypeStyle::Mono,
                        info.size ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
        break;
    case kState: {
        const Compat compat = compatibility(info);
        ui::icons::Icon icon = ui::icons::Icon::SuccessCircle;
        Color ink = Color::StatusSuccess;
        std::wstring text = m_strings.get(Str::UpdatesCompatible);
        if (compat == Compat::WrongWindows) {
            icon = ui::icons::Icon::ErrorOctagon;
            ink = Color::StatusError;
            text = m_strings.format(Str::UpdatesIncompatible, {{L"ver", std::format(L"Windows {}", info.targetWindows)}});
        } else if (compat == Compat::WrongArch) {
            icon = ui::icons::Icon::ErrorOctagon;
            ink = Color::StatusError;
            text = m_strings.format(Str::UpdatesArchMismatch, {{L"arch", info.architecture}});
        } else if (compat == Compat::Unknown) {
            icon = ui::icons::Icon::InfoCircle;
            ink = Color::TextTertiary;
            text = m_strings.get(Str::UpdatesUnknown);
        }
        canvas.drawIcon(icon, {rect.x, rect.y + 4}, ink);
        canvas.drawText(text, {rect.x + 22, rect.y, rect.width - 22, rect.height}, TypeStyle::Caption,
                        compat == Compat::Ok ? Color::TextSecondary : ink);
        break;
    }
    default: break;
    }
}

void UpdatesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    y += kDrop + 16;
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
}

void UpdatesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF t = m_table->bounds();
    if (m_rows.empty()) {
        canvas.drawText(m_strings.get(Str::UpdatesEmptyList), {t.x, t.y + ui::TableView::kHeader + 12, t.width, 20},
                        TypeStyle::Body, Color::TextTertiary, ui::TextAlign::Center);
    }
}

} // namespace wl::app
