#include "app/pages/icons/IconFilesView.h"

#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/ScrollBar.h"
#include "ui/widgets/SearchBox.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::PointF;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kNoteH = 32.0f;
constexpr float kListW = 272.0f;
constexpr float kGap = 16.0f;
constexpr float kRow = 28.0f;
constexpr float kHeaderH = 32.0f;
constexpr float kCellW = 84.0f;
constexpr float kCellH = 88.0f;
constexpr float kIcon = 48.0f;

std::wstring groupRef(const std::wstring& fileName, const core::ResourceKey& key) {
    return key.named() ? fileName + L"," + key.name : fileName + L",-" + std::to_wstring(key.id);
}
} // namespace

// ---- the files ---------------------------------------------------------------------------------
class IconFilesView::FileList : public ui::Widget {
public:
    FileList(IconPatchController& controller, const Localization& strings) : m_controller(controller), m_strings(strings) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, strings.get(Str::IconsTabFiles));
        m_scroll = &add<ui::ScrollBar>();
        m_scroll->onScroll = [this](float offset) {
            m_offset = offset;
            invalidate();
        };
    }
    std::function<void(const std::wstring& relative)> onSelect;

    void setFilter(const std::wstring& filter) {
        m_filter = text::lower(filter);
        rebuild();
    }
    void setSelected(const std::wstring& relative) {
        m_selected = relative;
        reveal();
        invalidate();
    }
    void rebuild() {
        m_rows.clear();
        const auto& files = m_controller.files();
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (m_filter.empty() || text::lower(files[i].name).find(m_filter) != std::wstring::npos) {
                m_rows.push_back(i);
            }
        }
        layout();
        invalidate();
    }
    [[nodiscard]] std::size_t visibleCount() const { return m_rows.size(); }

    void layout() override {
        const RectF b = bounds();
        const float content = static_cast<float>(m_rows.size()) * kRow;
        m_offset = std::clamp(m_offset, 0.0f, std::max(content - b.height, 0.0f));
        m_scroll->setBounds({b.right() - ui::ScrollBar::kWidth, b.y, ui::ScrollBar::kWidth, b.height});
        m_scroll->setRange(content, b.height);
        m_scroll->setOffset(m_offset);
        m_scroll->setVisible(m_scroll->needed());
    }
    bool onWheel(PointF, float lines) override {
        if (!m_scroll->needed()) {
            return false;
        }
        m_offset -= lines * kRow * 3;
        layout();
        invalidate();
        return true;
    }
    void onPointerMove(PointF p) override {
        const int row = rowAt(p);
        if (row != m_hover) {
            m_hover = row;
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
            invalidate();
        }
    }
    void onPointerDown(PointF p) override {
        if (const int row = rowAt(p); row >= 0) {
            choose(row);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        const int count = static_cast<int>(m_rows.size());
        if (count == 0) {
            return false;
        }
        const int now = currentRow();
        const int page = std::max(1, static_cast<int>(bounds().height / kRow) - 1);
        switch (key.virtualKey) {
        case VK_UP: choose(std::max(now - 1, 0)); return true;
        case VK_DOWN: choose(std::min(now + 1, count - 1)); return true;
        case VK_PRIOR: choose(std::max(now - page, 0)); return true;
        case VK_NEXT: choose(std::min(now + page, count - 1)); return true;
        case VK_HOME: choose(0); return true;
        case VK_END: choose(count - 1); return true;
        default: return false;
        }
    }
    [[nodiscard]] RectF focusRect() const override {
        const int row = currentRow();
        return row >= 0 ? rowRect(row) : bounds();
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.strokeRoundRect(b, ui::tokens::radius::r2, Color::LineSubtle);
        canvas.pushClip(b);
        const auto& files = m_controller.files();
        for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
            const RectF r = rowRect(row);
            if (r.bottom() < b.y || r.y > b.bottom()) {
                continue;
            }
            const auto& file = files[m_rows[static_cast<std::size_t>(row)]];
            const bool selected = file.relative == m_selected;
            if (selected || row == m_hover) {
                canvas.fillRect({r.x + 1, r.y, r.width - 2, r.height}, selected ? Color::BgRaised : Color::BgPanel);
            }
            if (selected) {
                canvas.fillRect({r.x + 1, r.y + 4, 2, r.height - 8}, Color::AccentBase);
            }
            // Badge first (right), then the name in what is left.
            std::wstring badge;
            Color badgeInk = Color::TextTertiary;
            const int changed = m_controller.changedIn(file.relative);
            if (m_controller.restoreQueued(file.relative)) {
                badge = m_strings.get(Str::IconsRestoreBadge);
                badgeInk = Color::StatusWarning;
            } else if (changed > 0) {
                badge = m_strings.format(Str::IconsChangedN, {{L"n", std::to_wstring(changed)}});
                badgeInk = Color::AccentBase;
            } else if (m_controller.patchedInImage(file.relative)) {
                badge = m_strings.get(Str::IconsPatchedBadge);
            }
            float right = r.right() - 10 - (m_scroll->visible() ? ui::ScrollBar::kWidth : 0.0f);
            if (!badge.empty()) {
                const float w = std::ceil(canvas.text().measure(badge, TypeStyle::Caption));
                canvas.drawText(badge, {right - w, r.y, w, r.height}, TypeStyle::Caption, badgeInk);
                right -= w + 8;
            }
            canvas.drawText(file.name, {r.x + 12, r.y, std::max(right - r.x - 12, 0.0f), r.height},
                            selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        }
        canvas.popClip();
    }

private:
    [[nodiscard]] RectF rowRect(int row) const {
        const RectF b = bounds();
        return {b.x, b.y + static_cast<float>(row) * kRow - m_offset, b.width, kRow};
    }
    [[nodiscard]] int rowAt(PointF p) const {
        if (!bounds().contains(p)) {
            return -1;
        }
        const int row = static_cast<int>((p.y - bounds().y + m_offset) / kRow);
        return row >= 0 && row < static_cast<int>(m_rows.size()) ? row : -1;
    }
    [[nodiscard]] int currentRow() const {
        const auto& files = m_controller.files();
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
            if (files[m_rows[static_cast<std::size_t>(i)]].relative == m_selected) {
                return i;
            }
        }
        return -1;
    }
    void choose(int row) {
        const auto& files = m_controller.files();
        m_selected = files[m_rows[static_cast<std::size_t>(row)]].relative;
        reveal();
        invalidate();
        if (onSelect) {
            onSelect(m_selected);
        }
    }
    void reveal() {
        const int row = currentRow();
        if (row < 0) {
            return;
        }
        const float top = static_cast<float>(row) * kRow;
        const float view = bounds().height;
        if (top < m_offset) {
            m_offset = top;
        } else if (top + kRow > m_offset + view) {
            m_offset = top + kRow - view;
        }
        layout();
    }

    IconPatchController& m_controller;
    const Localization& m_strings;
    ui::ScrollBar* m_scroll = nullptr;
    std::wstring m_filter;
    std::vector<std::size_t> m_rows;
    std::wstring m_selected;
    float m_offset = 0;
    int m_hover = -1;
};

// ---- the icons of one file ---------------------------------------------------------------------
class IconFilesView::IconGrid : public ui::Widget {
public:
    IconGrid(IconPatchController& controller, const Localization& strings) : m_controller(controller), m_strings(strings) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, strings.get(Str::IconsTitle));
        m_scroll = &add<ui::ScrollBar>();
        m_scroll->onScroll = [this](float offset) {
            m_offset = offset;
            invalidate();
        };
    }
    std::function<void()> onPick;
    std::function<void()> onRevert;
    std::function<void()> onSelectionChanged;

    void setFile(const std::wstring& relative, const std::wstring& name) {
        if (relative != m_file) {
            m_offset = 0;
            m_selected = 0;
        }
        m_file = relative;
        m_name = name;
        layout();
        invalidate();
    }
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    [[nodiscard]] const std::vector<IconPatchController::Group>* groups() const {
        return m_file.empty() ? nullptr : m_controller.groups(m_file);
    }

    void layout() override {
        const RectF b = bounds();
        const float content = rows() * kCellH;
        m_offset = std::clamp(m_offset, 0.0f, std::max(content - b.height, 0.0f));
        m_scroll->setBounds({b.right() - ui::ScrollBar::kWidth, b.y, ui::ScrollBar::kWidth, b.height});
        m_scroll->setRange(content, b.height);
        m_scroll->setOffset(m_offset);
        m_scroll->setVisible(m_scroll->needed());
    }
    bool onWheel(PointF, float lines) override {
        if (!m_scroll->needed()) {
            return false;
        }
        m_offset -= lines * kCellH;
        layout();
        invalidate();
        return true;
    }
    void onPointerMove(PointF p) override {
        const int cell = cellAt(p);
        if (cell != m_hover) {
            m_hover = cell;
            setTooltip(cell >= 0 ? tooltipFor(cell) : std::wstring());
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
            invalidate();
        }
    }
    void onPointerDown(PointF p) override {
        if (const int cell = cellAt(p); cell >= 0) {
            select(cell);
        }
    }
    void onDoubleClick() override {
        if (m_hover >= 0 && onPick) {
            onPick();
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        const int count = groupCount();
        if (count == 0) {
            return false;
        }
        const int cols = columns();
        switch (key.virtualKey) {
        case VK_LEFT: select(std::max(m_selected - 1, 0)); return true;
        case VK_RIGHT: select(std::min(m_selected + 1, count - 1)); return true;
        case VK_UP: select(std::max(m_selected - cols, 0)); return true;
        case VK_DOWN: select(std::min(m_selected + cols, count - 1)); return true;
        case VK_HOME: select(0); return true;
        case VK_END: select(count - 1); return true;
        case VK_RETURN:
        case VK_SPACE:
            if (onPick) {
                onPick();
            }
            return true;
        case VK_DELETE:
        case VK_BACK:
            if (onRevert) {
                onRevert();
            }
            return true;
        default: return false;
        }
    }
    [[nodiscard]] RectF focusRect() const override {
        return m_selected >= 0 && m_selected < groupCount() ? cellRect(m_selected) : bounds();
    }
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r3; }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        const auto* list = groups();
        if (m_file.empty() || !list || list->empty()) {
            const Str message = m_file.empty() ? Str::IconsPickFile : !list ? Str::IconsLoading : Str::IconsNoIcons;
            canvas.drawText(m_strings.get(message), {b.x, b.y + 24, b.width, 20}, TypeStyle::Body, Color::TextTertiary,
                            ui::TextAlign::Center);
            return;
        }
        const bool restoring = m_controller.restoreQueued(m_file);
        const auto image = m_controller.pathInImage(m_file).wstring();
        const auto backup = m_controller.pathInImage(core::iconBackupPath(m_file)).wstring();
        canvas.pushClip(b);
        for (int i = 0; i < static_cast<int>(list->size()); ++i) {
            const RectF c = cellRect(i);
            if (c.bottom() < b.y || c.y > b.bottom()) {
                continue;
            }
            const auto& g = (*list)[static_cast<std::size_t>(i)];
            const auto replacement = m_controller.replacement(m_file, g.key);
            const bool selected = i == m_selected;
            if (selected || i == m_hover) {
                canvas.fillRoundRect(c, ui::tokens::radius::r3, selected ? Color::BgRaised : Color::BgPanel);
            }
            if (replacement || selected) {
                canvas.strokeRoundRect(c, ui::tokens::radius::r3, replacement ? Color::AccentBase : Color::LineStrong);
            }
            const RectF icon{c.x + (c.width - kIcon) / 2, c.y + 10, kIcon, kIcon};
            bool drawn = false;
            if (replacement) {
                drawn = canvas.drawFileIcon(replacement->wstring(), 0, icon);
            } else if (restoring) {
                drawn = canvas.drawFileIcon(backup, g.index, icon);
            } else {
                drawn = canvas.drawFileIcon(image, g.index, icon);
            }
            if (!drawn) {
                canvas.strokeRoundRect(icon, ui::tokens::radius::r2, replacement ? Color::StatusError : Color::LineSubtle);
            }
            canvas.drawText(g.key.named() ? g.key.name : L"#" + std::to_wstring(g.key.id), {c.x + 4, c.y + 64, c.width - 8, 16},
                            TypeStyle::Caption, replacement ? Color::AccentBase : Color::TextSecondary, ui::TextAlign::Center);
        }
        canvas.popClip();
    }

private:
    [[nodiscard]] int groupCount() const {
        const auto* list = groups();
        return list ? static_cast<int>(list->size()) : 0;
    }
    [[nodiscard]] int columns() const {
        return std::max(1, static_cast<int>((bounds().width - ui::ScrollBar::kWidth) / kCellW));
    }
    [[nodiscard]] float rows() const {
        const int cols = columns();
        return static_cast<float>((groupCount() + cols - 1) / cols);
    }
    [[nodiscard]] RectF cellRect(int i) const {
        const RectF b = bounds();
        const int cols = columns();
        return {b.x + static_cast<float>(i % cols) * kCellW, b.y + static_cast<float>(i / cols) * kCellH - m_offset, kCellW - 4,
                kCellH - 4};
    }
    [[nodiscard]] int cellAt(PointF p) const {
        if (!bounds().contains(p)) {
            return -1;
        }
        const int cols = columns();
        const int col = static_cast<int>((p.x - bounds().x) / kCellW);
        const int row = static_cast<int>((p.y - bounds().y + m_offset) / kCellH);
        const int i = row * cols + col;
        return col < cols && i >= 0 && i < groupCount() && cellRect(i).contains(p) ? i : -1;
    }
    void select(int i) {
        m_selected = i;
        const RectF c = cellRect(i);
        const RectF b = bounds();
        if (c.y < b.y) {
            m_offset -= b.y - c.y;
        } else if (c.bottom() > b.bottom()) {
            m_offset += c.bottom() - b.bottom();
        }
        layout();
        invalidate();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
    }
    [[nodiscard]] std::wstring tooltipFor(int i) const {
        const auto* list = groups();
        if (!list || i >= static_cast<int>(list->size())) {
            return {};
        }
        const auto& g = (*list)[static_cast<std::size_t>(i)];
        const std::wstring ref = groupRef(m_name, g.key);
        if (const auto r = m_controller.replacement(m_file, g.key)) {
            return m_strings.format(Str::IconsTooltipNew, {{L"ref", ref}, {L"file", r->filename().wstring()}});
        }
        return m_strings.format(Str::IconsTooltip,
                                {{L"ref", ref}, {L"n", std::to_wstring(g.images)}, {L"px", std::to_wstring(g.largest)}});
    }

    IconPatchController& m_controller;
    const Localization& m_strings;
    ui::ScrollBar* m_scroll = nullptr;
    std::wstring m_file;
    std::wstring m_name;
    float m_offset = 0;
    int m_selected = 0;
    int m_hover = -1;
};

// ---- the view ----------------------------------------------------------------------------------
IconFilesView::IconFilesView(AppState& state, IconPatchController& controller, const Localization& strings, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::IconsSearchFiles));
    m_search->setAccessible(ui::AccessRole::Edit, strings.get(Str::IconsSearchFiles));
    m_search->onChange = [this](const std::wstring& text) { m_list->setFilter(text); };
    m_list = &add<FileList>(controller, strings);
    m_list->onSelect = [this](const std::wstring& relative) {
        m_file = relative;
        refresh();
    };
    m_grid = &add<IconGrid>(controller, strings);
    m_grid->onPick = [this] { pick(); };
    m_grid->onRevert = [this] { revertSelected(); };
    m_grid->onSelectionChanged = [this] { updateButtons(); };
    m_replace = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::IconsReplace), ui::icons::Icon::OpenFolder);
    m_replace->onInvoke = [this] { pick(); };
    m_revert = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::IconsRevert));
    m_revert->onInvoke = [this] { revertSelected(); };
    m_save = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::IconsSaveOriginal), ui::icons::Icon::Save);
    m_save->onInvoke = [this] { saveSelected(); };
    m_restore = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::IconsRestoreFile));
    m_restore->onInvoke = [this] {
        if (!m_file.empty()) {
            m_controller.setRestore(m_file, !m_controller.restoreQueued(m_file));
        }
    };
    m_controller.onLoaded = [this] { refresh(); };
    m_list->rebuild();
    if (!m_controller.files().empty()) {
        selectFile(L"imageres.dll");
    }
}

IconFilesView::~IconFilesView() {
    m_controller.onLoaded = nullptr;
}

void IconFilesView::selectFile(std::wstring_view name) {
    const auto& files = m_controller.files();
    const auto* file = IconPatchController::fileNamed(files, name);
    if (!file && !files.empty()) {
        file = &files.front();
    }
    if (file) {
        m_file = file->relative;
        m_list->setSelected(m_file);
    }
    refresh();
}

const IconPatchController::Group* IconFilesView::selectedGroup() const {
    const auto* list = m_grid->groups();
    const int i = m_grid->selected();
    return list && i >= 0 && i < static_cast<int>(list->size()) ? &(*list)[static_cast<std::size_t>(i)] : nullptr;
}

void IconFilesView::refresh() {
    std::wstring name;
    for (const auto& f : m_controller.files()) {
        if (f.relative == m_file) {
            name = f.name;
        }
    }
    if (name.empty()) {
        m_file.clear();
    }
    m_grid->setFile(m_file, name);
    m_list->rebuild();
    m_list->setSelected(m_file);
    updateButtons();
    layout();
    invalidate();
}

void IconFilesView::updateButtons() {
    const auto* group = selectedGroup();
    const bool restoring = !m_file.empty() && m_controller.restoreQueued(m_file);
    m_replace->setEnabled(group != nullptr && !m_state.queueLocked());
    m_revert->setEnabled(group != nullptr && m_controller.replacement(m_file, group->key).has_value());
    m_save->setEnabled(group != nullptr);
    m_restore->setVisible(!m_file.empty() && (restoring || m_controller.patchedInImage(m_file)));
    m_restore->setText(m_strings.get(restoring ? Str::IconsRestoreCancel : Str::IconsRestoreFile));
    layout();
}

void IconFilesView::pick() {
    const auto* group = selectedGroup();
    if (!group || !m_intents.pickSource) {
        return;
    }
    const auto key = group->key;
    if (const auto source = m_intents.pickSource()) {
        if (auto ok = m_controller.replace(m_file, key, *source); !ok && m_intents.toast) {
            m_intents.toast(Str::IconsNotSource, source->filename().wstring(), true);
        }
    }
}

void IconFilesView::revertSelected() {
    if (const auto* group = selectedGroup()) {
        m_controller.revert(m_file, group->key);
    }
}

void IconFilesView::saveSelected() {
    const auto* group = selectedGroup();
    if (!group || !m_intents.saveIco) {
        return;
    }
    std::wstring stem;
    for (const auto& f : m_controller.files()) {
        if (f.relative == m_file) {
            stem = f.name;
        }
    }
    const auto key = group->key;
    const std::wstring suggested = stem + L"_" + (key.named() ? key.name : std::to_wstring(key.id)) + L".ico";
    if (const auto out = m_intents.saveIco(suggested)) {
        const auto ok = m_controller.exportOriginal(m_file, key, *out);
        if (m_intents.toast) {
            m_intents.toast(ok ? Str::IconsSavedIco : Str::IconsSaveFailed, ok ? out->wstring() : describe(ok.error()), !ok);
        }
    }
}

void IconFilesView::layout() {
    const RectF b = bounds();
    const float top = b.y + kNoteH + 8;
    m_search->setBounds({b.x, top, kListW, ui::tokens::size::control});
    m_list->setBounds({b.x, top + ui::tokens::size::control + 8, kListW, std::max(b.bottom() - top - ui::tokens::size::control - 8, 0.0f)});
    const float x = b.x + kListW + kGap;
    const float right = b.right();
    // Buttons right-aligned on the header row.
    float bx = right;
    for (ui::Button* button : {m_restore, m_save, m_revert, m_replace}) {
        if (!button->visible()) {
            continue;
        }
        const float w = button->measure({400, ui::tokens::size::control}).width;
        bx -= w;
        button->setBounds({bx, top, w, ui::tokens::size::control});
        bx -= 6;
    }
    m_headerRight = bx;
    m_grid->setBounds({x, top + kHeaderH + 8, std::max(right - x, 0.0f), std::max(b.bottom() - top - kHeaderH - 8, 0.0f)});
}

void IconFilesView::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.drawTextWrapped(m_strings.get(Str::IconsPatchNote), {b.x, b.y, b.width, kNoteH}, TypeStyle::Caption, Color::TextTertiary);
    const float top = b.y + kNoteH + 8;
    const float x = b.x + kListW + kGap;
    if (m_file.empty()) {
        if (m_controller.files().empty()) {
            canvas.drawText(m_strings.get(Str::IconsNoFiles), {x, top, b.right() - x, 20}, TypeStyle::Body, Color::TextTertiary);
        }
        return;
    }
    std::wstring name;
    for (const auto& f : m_controller.files()) {
        if (f.relative == m_file) {
            name = f.name;
        }
    }
    const float nameW = std::ceil(canvas.text().measure(name, TypeStyle::BodyStrong));
    canvas.drawText(name, {x, top, nameW, ui::tokens::size::control}, TypeStyle::BodyStrong, Color::TextPrimary);
    std::wstring detail = m_file;
    if (const auto* list = m_controller.groups(m_file)) {
        detail = m_strings.format(Str::IconsIconsN, {{L"n", std::to_wstring(list->size())}}) + L" · " + m_file;
    }
    const float dx = x + nameW + 10;
    canvas.drawText(detail, {dx, top, std::max(m_headerRight - dx - 8, 0.0f), ui::tokens::size::control}, TypeStyle::Caption,
                    Color::TextTertiary);
}

} // namespace wl::app
