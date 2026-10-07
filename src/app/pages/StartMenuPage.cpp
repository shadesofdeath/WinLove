#include "app/pages/StartMenuPage.h"

#include "app/pages/TweaksPage.h"
#include "base/Text.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/ScrollBar.h"
#include "ui/widgets/SearchBox.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace wl::app {

using core::StartApp;
using ui::PointF;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kTabs = 24.0f;
constexpr float kModeRow = 28.0f;
constexpr float kNoteH = 34.0f;
constexpr float kListW = 300.0f;
constexpr float kGap = 16.0f;
constexpr float kRow = 28.0f;
constexpr float kCellH = 88.0f;
constexpr int kColumns = 6;

// The small picture of an app: its logo from the image, else a generic glyph.
void drawAppIcon(ui::Canvas& canvas, const StartPinsController& pins, const StartApp& app, RectF rect) {
    if (!app.icon.empty() && canvas.drawFileIcon(pins.pathInImage(app.icon).wstring(), app.iconIndex, rect)) {
        return;
    }
    const auto glyph = app.kind == StartApp::Kind::DesktopId ? ui::icons::Icon::EdgeBrowserGeneric
                       : app.kind == StartApp::Kind::Packaged ? ui::icons::Icon::AppxPackage
                                                             : ui::icons::Icon::File;
    const float size = std::min(rect.width, rect.height);
    canvas.drawIcon(glyph, {rect.x + (rect.width - size) / 2, rect.y + (rect.height - size) / 2}, Color::TextSecondary,
                    ui::IconVariant::Regular16, size);
}
} // namespace

// ---- the image's apps --------------------------------------------------------------------------
class StartMenuPage::AppList : public ui::Widget {
public:
    AppList(StartPinsController& pins, const Localization& strings) : m_pins(&pins), m_strings(strings) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, strings.get(Str::StartmenuApps));
        m_scroll = &add<ui::ScrollBar>();
        m_scroll->onScroll = [this](float offset) {
            m_offset = offset;
            invalidate();
        };
    }
    std::function<void(const StartApp&)> onAdd;

    void setPins(StartPinsController& pins) {
        m_pins = &pins;
        rebuild();
    }
    void setFilter(const std::wstring& filter) {
        m_filter = text::lower(filter);
        rebuild();
    }
    void rebuild() {
        m_rows.clear();
        if (const auto* apps = m_pins->apps()) {
            for (std::size_t i = 0; i < apps->size(); ++i) {
                if (m_filter.empty() || text::lower((*apps)[i].name).find(m_filter) != std::wstring::npos) {
                    m_rows.push_back(i);
                }
            }
        }
        m_selected = std::clamp(m_selected, 0, std::max(static_cast<int>(m_rows.size()) - 1, 0));
        layout();
        invalidate();
    }
    [[nodiscard]] const StartApp* selectedApp() const {
        const auto* apps = m_pins->apps();
        if (!apps || m_selected < 0 || m_selected >= static_cast<int>(m_rows.size())) {
            return nullptr;
        }
        return &(*apps)[m_rows[static_cast<std::size_t>(m_selected)]];
    }

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
            m_selected = row;
            invalidate();
        }
    }
    void onDoubleClick() override { addSelected(); }
    bool onKeyDown(const ui::KeyEvent& key) override {
        const int count = static_cast<int>(m_rows.size());
        if (count == 0) {
            return false;
        }
        auto go = [&](int row) {
            m_selected = std::clamp(row, 0, count - 1);
            reveal();
            invalidate();
            return true;
        };
        const int page = std::max(1, static_cast<int>(bounds().height / kRow) - 1);
        switch (key.virtualKey) {
        case VK_UP: return go(m_selected - 1);
        case VK_DOWN: return go(m_selected + 1);
        case VK_PRIOR: return go(m_selected - page);
        case VK_NEXT: return go(m_selected + page);
        case VK_HOME: return go(0);
        case VK_END: return go(count - 1);
        case VK_RETURN:
        case VK_SPACE: addSelected(); return true;
        default: return false;
        }
    }
    [[nodiscard]] RectF focusRect() const override {
        return m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()) ? rowRect(m_selected) : bounds();
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.strokeRoundRect(b, ui::tokens::radius::r2, Color::LineSubtle);
        const auto* apps = m_pins->apps();
        if (!apps) {
            canvas.drawText(m_strings.get(Str::StartmenuLoadingApps), {b.x, b.y + 12, b.width, 20}, TypeStyle::Caption,
                            Color::TextTertiary, ui::TextAlign::Center);
            return;
        }
        const auto pinned = m_pins->pins();
        canvas.pushClip(b);
        for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
            const RectF r = rowRect(row);
            if (r.bottom() < b.y || r.y > b.bottom()) {
                continue;
            }
            const auto& app = (*apps)[m_rows[static_cast<std::size_t>(row)]];
            const bool selected = row == m_selected;
            if (selected || row == m_hover) {
                canvas.fillRect({r.x + 1, r.y, r.width - 2, r.height}, selected ? Color::BgRaised : Color::BgPanel);
            }
            drawAppIcon(canvas, *m_pins, app, {r.x + 10, r.y + 6, 16, 16});
            const bool isPinned = std::ranges::find(pinned, app) != pinned.end();
            const float right = r.right() - 10 - (m_scroll->visible() ? ui::ScrollBar::kWidth : 0.0f);
            float kw = ui::tokens::size::icon;
            if (isPinned) {
                canvas.drawIcon(ui::icons::Icon::Check, {right - kw, r.y + (r.height - kw) / 2}, Color::AccentBase);
            } else {
                const std::wstring kind = m_strings.get(app.kind == StartApp::Kind::DesktopLink ? Str::StartmenuKindLink : Str::StartmenuKindApp);
                kw = std::ceil(canvas.text().measure(kind, TypeStyle::Caption));
                canvas.drawText(kind, {right - kw, r.y, kw, r.height}, TypeStyle::Caption, Color::TextTertiary);
            }
            canvas.drawText(app.name, {r.x + 34, r.y, std::max(right - kw - 8 - r.x - 34, 0.0f), r.height},
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
    void reveal() {
        const float top = static_cast<float>(m_selected) * kRow;
        if (top < m_offset) {
            m_offset = top;
        } else if (top + kRow > m_offset + bounds().height) {
            m_offset = top + kRow - bounds().height;
        }
        layout();
    }
    void addSelected() {
        if (const auto* app = selectedApp(); app && onAdd) {
            onAdd(*app);
        }
    }

    StartPinsController* m_pins;
    const Localization& m_strings;
    ui::ScrollBar* m_scroll = nullptr;
    std::wstring m_filter;
    std::vector<std::size_t> m_rows;
    float m_offset = 0;
    int m_selected = 0;
    int m_hover = -1;
};

// ---- the Start-like preview, which is the pin list ---------------------------------------------
class StartMenuPage::PinGrid : public ui::Widget {
public:
    PinGrid(StartPinsController& pins, const Localization& strings) : m_pins(&pins), m_strings(strings) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, strings.get(Str::StartmenuPinned));
    }
    std::function<void()> onSelectionChanged;

    void setPins(StartPinsController& pins) {
        m_pins = &pins;
        m_selected = 0;
        invalidate();
    }
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    void setSelected(int index) {
        m_selected = index;
        invalidate();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
    }

    void onPointerMove(PointF p) override {
        const int cell = cellAt(p);
        if (cell != m_hover) {
            m_hover = cell;
            const auto list = m_pins->pins();
            setTooltip(cell >= 0 && cell < static_cast<int>(list.size()) ? list[static_cast<std::size_t>(cell)].id : std::wstring());
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
            setSelected(cell);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        const int count = static_cast<int>(m_pins->pins().size());
        if (count == 0 || m_pins->mode() != StartPinsController::Mode::Custom) {
            return false;
        }
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        auto go = [&](int to) {
            setSelected(std::clamp(to, 0, count - 1));
            return true;
        };
        switch (key.virtualKey) {
        case VK_LEFT:
            if (ctrl) {
                m_pins->move(static_cast<std::size_t>(m_selected), -1);
                return go(m_selected - 1);
            }
            return go(m_selected - 1);
        case VK_RIGHT:
            if (ctrl) {
                m_pins->move(static_cast<std::size_t>(m_selected), +1);
                return go(m_selected + 1);
            }
            return go(m_selected + 1);
        case VK_UP: return go(m_selected - kColumns);
        case VK_DOWN: return go(m_selected + kColumns);
        case VK_DELETE:
        case VK_BACK:
            m_pins->remove(static_cast<std::size_t>(m_selected));
            return go(std::min(m_selected, count - 2));
        default: return false;
        }
    }
    [[nodiscard]] RectF focusRect() const override {
        const int count = static_cast<int>(m_pins->pins().size());
        return m_selected >= 0 && m_selected < count ? cellRect(m_selected) : bounds();
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgPanel);
        canvas.strokeRoundRect(b, ui::tokens::radius::r3, Color::LineSubtle);
        const auto mode = m_pins->mode();
        const auto list = m_pins->pins();
        const bool bar = m_pins->surface() == StartPinsController::Surface::Taskbar;
        canvas.drawText(m_strings.get(bar ? Str::StartmenuTabTaskbar : Str::StartmenuPinned), {b.x + 20, b.y + 14, b.width - 40, 20},
                        TypeStyle::BodyStrong, Color::TextPrimary);
        if (mode == StartPinsController::Mode::Custom && !list.empty()) {
            const std::wstring count = m_strings.format(Str::StartmenuPinsN, {{L"n", std::to_wstring(list.size())}});
            canvas.drawText(count, {b.x + 20, b.y + 14, b.width - 40, 20}, TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);
        }
        if (mode != StartPinsController::Mode::Custom || list.empty()) {
            const Str message = mode == StartPinsController::Mode::Windows
                                    ? (bar ? Str::StartmenuTaskbarPreviewWindows : Str::StartmenuPreviewWindows)
                                : mode == StartPinsController::Mode::Empty ? (bar ? Str::StartmenuTaskbarPreviewEmpty : Str::StartmenuPreviewEmpty)
                                                                           : Str::StartmenuPreviewCustomEmpty;
            if (mode == StartPinsController::Mode::Windows) {
                // A hint of what Windows puts there: greyed placeholders, as Start shows them while it downloads.
                for (int i = 0; i < 12; ++i) {
                    const RectF c = cellRect(i);
                    canvas.fillRoundRect({c.x + (c.width - 32) / 2, c.y + 12, 32, 32}, ui::tokens::radius::r2, Color::BgRaised);
                    canvas.fillRoundRect({c.x + 16, c.y + 54, c.width - 32, 8}, ui::tokens::radius::r1, Color::BgRaised);
                }
            }
            canvas.drawText(m_strings.get(message), {b.x, b.bottom() - 40, b.width, 20}, TypeStyle::Caption, Color::TextTertiary,
                            ui::TextAlign::Center);
            return;
        }
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            const RectF c = cellRect(i);
            const bool selected = i == m_selected;
            if (selected || i == m_hover) {
                canvas.fillRoundRect(c, ui::tokens::radius::r3, selected ? Color::BgRaised : Color::BgPressed);
            }
            if (selected) {
                canvas.strokeRoundRect(c, ui::tokens::radius::r3, Color::AccentBase);
            }
            drawAppIcon(canvas, *m_pins, list[static_cast<std::size_t>(i)], {c.x + (c.width - 32) / 2, c.y + 12, 32, 32});
            canvas.drawText(list[static_cast<std::size_t>(i)].name, {c.x + 6, c.y + 52, c.width - 12, 16}, TypeStyle::Caption,
                            Color::TextPrimary, ui::TextAlign::Center);
        }
    }

private:
    [[nodiscard]] RectF cellRect(int i) const {
        const RectF b = bounds();
        const float w = (b.width - 40) / kColumns;
        return {b.x + 20 + static_cast<float>(i % kColumns) * w, b.y + 48 + static_cast<float>(i / kColumns) * kCellH, w - 4, kCellH - 6};
    }
    [[nodiscard]] int cellAt(PointF p) const {
        const int count = static_cast<int>(m_pins->pins().size());
        for (int i = 0; i < count; ++i) {
            if (cellRect(i).contains(p)) {
                return i;
            }
        }
        return -1;
    }

    StartPinsController* m_pins;
    const Localization& m_strings;
    int m_selected = 0;
    int m_hover = -1;
};

// ---- the page ----------------------------------------------------------------------------------
StartMenuPage::StartMenuPage(AppState& state, StartPinsController& pins, StartPinsController& taskbar, ImageSettingsController& settings,
                             const Localization& strings, Language language, std::function<void()> goImages)
    : m_state(state), m_start(pins), m_taskbar(taskbar), m_pins(&pins), m_strings(strings) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::StartmenuTabPins), strings.get(Str::StartmenuTabTaskbar),
                                                        strings.get(Str::StartmenuTabSettings)},
                              0);
    m_tabs->onChange = [this](int tab) { showTab(tab); };
    m_mode = &add<ui::RadioGroup>(std::vector<std::wstring>{strings.get(Str::StartmenuModeWindows), strings.get(Str::StartmenuModeEmpty),
                                                            strings.get(Str::StartmenuModeCustom)},
                                  0);
    m_mode->onChange = [this](int mode) { m_pins->setMode(static_cast<StartPinsController::Mode>(mode)); };
    m_once = &add<ui::CheckField>(strings.get(Str::StartmenuApplyOnce), true);
    m_once->setTooltip(strings.get(Str::StartmenuApplyOnceHint));
    m_once->onChange = [this](bool on) { m_pins->setApplyOnce(on); };
    m_search = &add<ui::SearchBox>(strings.get(Str::StartmenuSearchApps));
    m_search->onChange = [this](const std::wstring& text) { m_apps->setFilter(text); };
    m_apps = &add<AppList>(pins, strings);
    m_apps->onAdd = [this](const StartApp& app) {
        m_pins->add(app);
        m_grid->setSelected(static_cast<int>(m_pins->pins().size()) - 1);
    };
    m_add = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StartmenuAdd), ui::icons::Icon::Pin);
    m_add->onInvoke = [this] {
        if (const auto* app = m_apps->selectedApp()) {
            m_pins->add(*app);
            m_grid->setSelected(static_cast<int>(m_pins->pins().size()) - 1);
        }
    };
    m_grid = &add<PinGrid>(pins, strings);
    m_grid->onSelectionChanged = [this] { sync(); };
    m_left = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StartmenuMoveLeft), ui::icons::Icon::ArrowLeft);
    m_left->onInvoke = [this] {
        const int i = m_grid->selected();
        m_pins->move(static_cast<std::size_t>(i), -1);
        m_grid->setSelected(std::max(i - 1, 0));
    };
    m_right = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StartmenuMoveRight), ui::icons::Icon::ArrowRight);
    m_right->onInvoke = [this] {
        const int i = m_grid->selected();
        m_pins->move(static_cast<std::size_t>(i), +1);
        m_grid->setSelected(std::min(i + 1, static_cast<int>(m_pins->pins().size()) - 1));
    };
    m_remove = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StartmenuRemove), ui::icons::Icon::Unpin);
    m_remove->onInvoke = [this] {
        const int i = m_grid->selected();
        m_pins->remove(static_cast<std::size_t>(i));
        m_grid->setSelected(std::max(std::min(i, static_cast<int>(m_pins->pins().size()) - 1), 0));
    };
    m_clear = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StartmenuClear));
    m_clear->onInvoke = [this] { m_pins->setPins({}); };
    m_settings = &add<TweaksPage>(state, settings, strings, language, goImages, TweaksPage::PickImage{}, std::string("start"));
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Pin, strings.get(Str::StartmenuNoMountTitle), strings.get(Str::StartmenuNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::StartmenuTitle));
    m_start.onLoaded = [this] { // the taskbar's list is the same one
        m_apps->rebuild();
        invalidate();
    };
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            sync();
        }
    });
    refresh();
}

StartMenuPage::~StartMenuPage() {
    m_start.onLoaded = nullptr;
    m_state.unsubscribe(m_subscription);
}

void StartMenuPage::showTab(int tab) {
    m_tab = std::clamp(tab, kTabPins, kTabSettings);
    m_tabs->setSelected(m_tab);
    StartPinsController& pins = taskbar() ? m_taskbar : m_start;
    if (m_pins != &pins) {
        m_pins = &pins;
        m_apps->setPins(pins);
        m_grid->setPins(pins);
        m_once->setLabel(m_strings.get(taskbar() ? Str::StartmenuTaskbarUnpin : Str::StartmenuApplyOnce));
        m_once->setTooltip(m_strings.get(taskbar() ? Str::StartmenuTaskbarUnpinHint : Str::StartmenuApplyOnceHint));
    }
    refresh();
}

bool StartMenuPage::windows10() const {
    const auto* image = m_state.selectedImage();
    return image && image->build > 0 && image->build < 22000;
}

void StartMenuPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_settings->setVisible(mounted && m_tab == kTabSettings);
    if (mounted) {
        (void)m_pins->apps(); // starts reading the image's apps
    }
    m_apps->rebuild();
    sync();
}

void StartMenuPage::sync() {
    const bool mounted = m_state.mounted().has_value();
    const bool pinsTab = mounted && m_tab != kTabSettings;
    const auto mode = m_pins->mode();
    const bool custom = mode == StartPinsController::Mode::Custom;
    const int count = static_cast<int>(m_pins->pins().size());
    const int selected = m_grid->selected();
    m_mode->setSelected(static_cast<int>(mode));
    // D-083: on Windows 10 the policy would lock Start's tiles too — Windows' own taskbar stays.
    m_mode->setEnabled(!(taskbar() && windows10()) || mode != StartPinsController::Mode::Windows);
    m_once->setChecked(m_pins->applyOnce());
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_mode, m_grid}) {
        w->setVisible(pinsTab);
    }
    m_once->setVisible(pinsTab && mode != StartPinsController::Mode::Windows);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_apps, m_add, m_left, m_right, m_remove, m_clear}) {
        w->setVisible(pinsTab && custom);
    }
    m_left->setEnabled(selected > 0 && selected < count);
    m_right->setEnabled(selected >= 0 && selected < count - 1);
    m_remove->setEnabled(selected >= 0 && selected < count);
    m_clear->setEnabled(count > 0);
    layout();
    invalidate();
}

void StartMenuPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y + kTop, b.width, kTabs});
    const float top = b.y + kTop + kTabs + 12;
    m_settings->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    // Mode row: label (painted) + radios + the check box.
    const float radioX = b.x + 120;
    const float radioW = m_mode->measure({b.width, kModeRow}).width;
    m_mode->setBounds({radioX, top, radioW, kModeRow});
    const float onceW = m_once->measure({b.width, kModeRow}).width;
    m_once->setBounds({radioX + radioW + 32, top, onceW, kModeRow});
    const float content = top + kModeRow + kNoteH + 8;
    const bool custom = m_pins->mode() == StartPinsController::Mode::Custom;
    const float gridX = custom ? b.x + kListW + kGap : b.x;
    // Left: search + add, then the list.
    const float addW = m_add->measure({200, ui::tokens::size::control}).width;
    m_search->setBounds({b.x, content, kListW - addW - 6, ui::tokens::size::control});
    m_add->setBounds({b.x + kListW - addW, content, addW, ui::tokens::size::control});
    m_apps->setBounds({b.x, content + ui::tokens::size::control + 8, kListW,
                       std::max(b.bottom() - content - ui::tokens::size::control - 8, 0.0f)});
    // Right: buttons on top, then the preview.
    float bx = b.right();
    for (ui::Button* button : {m_clear, m_remove, m_right, m_left}) {
        const float w = button->measure({200, ui::tokens::size::control}).width;
        bx -= w;
        button->setBounds({bx, content, w, ui::tokens::size::control});
        bx -= 6;
    }
    const float gridTop = custom ? content + ui::tokens::size::control + 8 : content;
    const float gridH = std::min(b.bottom() - gridTop, 48.0f + 4 * kCellH + 12);
    m_grid->setBounds({gridX, gridTop, std::min(b.right() - gridX, 720.0f), std::max(gridH, 0.0f)});
}

void StartMenuPage::paint(ui::Canvas& canvas) {
    if (!m_state.mounted() || m_tab == kTabSettings) {
        return;
    }
    const RectF b = bounds();
    const float top = b.y + kTop + kTabs + 12;
    canvas.drawText(m_strings.get(Str::StartmenuModeLabel), {b.x, top, 110, kModeRow}, TypeStyle::BodyStrong, Color::TextPrimary);
    const auto mode = m_pins->mode();
    if (taskbar()) {
        if (windows10()) {
            canvas.drawTextWrapped(m_strings.get(Str::StartmenuTaskbarWindows10), {b.x, top + kModeRow + 4, b.width, kNoteH},
                                   TypeStyle::Caption, Color::StatusWarning);
            return;
        }
        const Str note = mode == StartPinsController::Mode::Windows ? Str::StartmenuTaskbarNoteWindows
                         : mode == StartPinsController::Mode::Empty ? Str::StartmenuTaskbarNoteEmpty
                                                                    : Str::StartmenuTaskbarNoteCustom;
        std::wstring text = m_strings.get(note);
        if (mode != StartPinsController::Mode::Windows) {
            text += L" " + m_strings.get(Str::StartmenuTaskbarNoteHow);
        }
        canvas.drawTextWrapped(text, {b.x, top + kModeRow + 4, b.width, kNoteH}, TypeStyle::Caption, Color::TextTertiary);
        return;
    }
    const Str note = mode == StartPinsController::Mode::Windows ? Str::StartmenuNoteWindows
                     : mode == StartPinsController::Mode::Empty ? Str::StartmenuNoteEmpty
                                                                : Str::StartmenuNoteCustom;
    std::wstring text = m_strings.get(note);
    if (mode != StartPinsController::Mode::Windows) {
        text += L" " + m_strings.get(Str::StartmenuNoteEdition);
    }
    // A custom list on a build that does not apply it (and no update queued to change that).
    const auto* image = m_state.selectedImage();
    const bool updateQueued = std::ranges::any_of(m_state.changes().operations(), [](const core::ops::Operation& op) {
        return op.kind == core::ops::OpKind::AddPackage;
    });
    if (mode == StartPinsController::Mode::Custom && image && image->build >= 22000 &&
        !core::startAppliesCustomPins(image->build, image->spBuild) && !updateQueued) {
        canvas.drawTextWrapped(m_strings.format(Str::StartmenuOldBuild,
                                                {{L"build", std::to_wstring(image->build) + L"." + std::to_wstring(image->spBuild)}}),
                               {b.x, top + kModeRow + 4, b.width, kNoteH}, TypeStyle::Caption, Color::StatusWarning);
        return;
    }
    canvas.drawTextWrapped(text, {b.x, top + kModeRow + 4, b.width, kNoteH}, TypeStyle::Caption, Color::TextTertiary);
}

} // namespace wl::app
