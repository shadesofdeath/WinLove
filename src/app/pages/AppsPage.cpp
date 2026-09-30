#include "app/pages/AppsPage.h"

#include "app/pages/PageBits.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTabsTop = 11.0f;
constexpr float kTabsHeight = ui::tokens::size::control + 1;
constexpr float kContentTop = kTabsTop + kTabsHeight + 11.0f;
constexpr float kDrop = 56.0f;
constexpr float kToolbar = 24.0f;
enum AppColumn : int { kApp, kPublisher, kVersion, kArch, kDeps };
enum AssocColumn : int { kKind, kApplication, kProgId };
} // namespace

AppsPage::AppsPage(AppState& state, AppsController& controller, const Localization& strings, Language language,
                   std::function<void()> addPackages, std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::AppsTabAdd), strings.get(Str::AppsTabDefaults)}, 0);
    m_tabs->onChange = [this](int) { refresh(); };
    m_drop = &add<ui::DropZone>(strings.get(Str::AppsDrop), strings.get(Str::AppsDrop), strings.get(Str::AppsDropInvalid),
                                strings.get(Str::AppsDrop));
    m_drop->setCompact(true);
    m_drop->onInvoke = std::move(addPackages);
    m_appTable = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::AppsApp), 0},
        {strings.get(Str::AppsPublisher), 220},
        {strings.get(Str::DriversVersion), 130},
        {strings.get(Str::ImagesArch), 130},
        {strings.get(Str::AppsDeps), 150},
    });
    m_appTable->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintAppCell(c, row, column, rect, cell);
    };
    m_appTable->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_appTable->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_apps.size())) {
            m_state.unqueue(OpKind::AddAppx, m_apps[static_cast<std::size_t>(row)].package.wstring());
            return true;
        }
        return false;
    };
    m_appTable->onSelect = [this](int) { invalidate(); };

    std::vector<std::wstring> browsers{strings.get(Str::AppsBrowserUnchanged)};
    for (const auto& b : AppsController::browsers()) {
        browsers.push_back(strings.get(b.label));
    }
    m_browser = &add<ui::Dropdown>(strings.get(Str::AppsBrowser), std::move(browsers), 0);
    m_browser->onChange = [this](int index) { m_controller.setBrowser(index - 1); };
    m_assocTable = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::AppsKind), 160},
        {strings.get(Str::AppsApplication), 0},
        {strings.get(Str::AppsProgId), 320},
    });
    m_assocTable->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintAssocCell(c, row, column, rect);
    };
    m_assocTable->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_assocTable->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_assoc.size())) {
            m_controller.removeAssociation(m_assoc[static_cast<std::size_t>(row)].identifier);
            return true;
        }
        return false;
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::AppxPackage, strings.get(Str::AppsNoMountTitle), strings.get(Str::AppsNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::AppsTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Queue) {
            refresh();
        }
    });
    refresh();
}

AppsPage::~AppsPage() {
    m_state.unsubscribe(m_subscription);
}

void AppsPage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

void AppsPage::showDefaultsTab() {
    m_tabs->setSelected(1);
    refresh();
}

void AppsPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    const bool defaults = defaultsTab();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_drop->setVisible(mounted && !defaults);
    m_appTable->setVisible(mounted && !defaults);
    m_browser->setVisible(mounted && defaults);
    m_assocTable->setVisible(mounted && defaults);
    m_apps = m_controller.queuedApps();
    m_assoc = m_controller.associations();
    m_appTable->setRowCount(static_cast<int>(m_apps.size()));
    m_assocTable->setRowCount(static_cast<int>(m_assoc.size()));
    m_browser->setSelected(m_controller.browser() + 1);
    m_appTable->refresh();
    m_assocTable->refresh();
    layout();
    invalidate();
}

void AppsPage::paintAppCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_apps.size())) {
        return;
    }
    const auto& app = m_apps[static_cast<std::size_t>(row)];
    switch (column) {
    case kApp: {
        canvas.drawIcon(ui::icons::Icon::AppxPackage, {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        const std::wstring name = app.displayName.empty() ? app.name : app.displayName;
        const auto style = cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body;
        const float nameW = std::min(rect.right() - x, std::ceil(canvas.text().measure(name, style)));
        canvas.drawText(name, {x, rect.y, nameW, rect.height}, style, Color::TextPrimary);
        const float fx = x + nameW + 8;
        canvas.drawText(app.package.filename().wstring(), {fx, rect.y, std::max(rect.right() - fx, 0.0f), rect.height},
                        TypeStyle::Caption, Color::TextTertiary);
        break;
    }
    case kPublisher: canvas.drawText(app.publisher, rect, TypeStyle::Caption, Color::TextSecondary); break;
    case kVersion: canvas.drawText(app.version, rect, TypeStyle::Mono, Color::TextPrimary); break;
    case kArch: {
        std::wstring archs;
        for (const auto& a : app.architectures) {
            archs += (archs.empty() ? L"" : L" ") + a;
        }
        canvas.drawText(archs, rect, TypeStyle::Mono, Color::TextSecondary);
        break;
    }
    case kDeps: {
        const bool missing = !app.missing.empty();
        const std::wstring text = missing ? m_strings.format(Str::AppsDepsMissing, {{L"n", std::to_wstring(app.missing.size())}})
                                          : app.dependencies.empty() ? m_strings.get(Str::AppsDepsNone)
                                                                     : m_strings.format(Str::AppsDepsFound, {{L"n", std::to_wstring(app.dependencies.size())}});
        canvas.drawIcon(missing ? ui::icons::Icon::WarningTriangle : ui::icons::Icon::SuccessCircle, {rect.x, rect.y + 4},
                        missing ? Color::StatusWarning : Color::StatusSuccess);
        canvas.drawText(text, {rect.x + 22, rect.y, rect.width - 22, rect.height}, TypeStyle::Caption,
                        missing ? Color::StatusWarning : Color::TextSecondary);
        break;
    }
    default: break;
    }
}

void AppsPage::paintAssocCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_assoc.size())) {
        return;
    }
    const auto& a = m_assoc[static_cast<std::size_t>(row)];
    switch (column) {
    case kKind: canvas.drawText(a.identifier, rect, TypeStyle::Mono, Color::TextPrimary); break;
    case kApplication:
        canvas.drawText(a.applicationName.empty() ? L"—" : a.applicationName, rect, TypeStyle::Body, Color::TextPrimary);
        break;
    case kProgId: canvas.drawText(a.progId, rect, TypeStyle::Mono, Color::TextSecondary); break;
    default: break;
    }
}

void AppsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y + kTabsTop, b.width, kTabsHeight});
    float y = b.y + kContentTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    m_appTable->setBounds({b.x, y + kDrop + 16, b.width, std::max(b.bottom() - (y + kDrop + 16) - kDetailLine, 0.0f)});
    const float bw = std::min(m_browser->measure({}).width, 320.0f);
    m_browser->setBounds({b.x, y, bw, kToolbar});
    m_assocTable->setBounds({b.x, y + kToolbar + 12, b.width, std::max(b.bottom() - (y + kToolbar + 12) - kDetailLine, 0.0f)});
}

void AppsPage::paint(ui::Canvas& canvas) {
    if (!m_state.mounted()) {
        return;
    }
    const RectF b = bounds();
    if (defaultsTab()) {
        const RectF t = m_assocTable->bounds();
        if (m_assoc.empty()) {
            canvas.drawText(m_strings.get(Str::AppsAssocEmpty), {t.x, t.y + ui::TableView::kHeader + 12, t.width, 20},
                            TypeStyle::Body, Color::TextTertiary, ui::TextAlign::Center);
        }
        paintDetail(canvas, {b.x, t.bottom(), b.width, kDetailLine}, L"", m_strings.get(Str::AppsAssocHint));
        return;
    }
    const RectF t = m_appTable->bounds();
    if (m_apps.empty()) {
        canvas.drawText(m_strings.get(Str::AppsEmpty), {t.x, t.y + ui::TableView::kHeader + 12, t.width, 20}, TypeStyle::Body,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
    const int row = m_appTable->selected();
    if (row >= 0 && row < static_cast<int>(m_apps.size())) {
        const auto& app = m_apps[static_cast<std::size_t>(row)];
        std::wstring text;
        for (const auto& m : app.missing) {
            text += (text.empty() ? m_strings.get(Str::AppsMissingPrefix) + L" " : L", ") + m;
        }
        if (text.empty()) {
            text = app.license.empty() ? m_strings.get(Str::AppsNoLicense)
                                       : m_strings.format(Str::AppsLicense, {{L"file", app.license.filename().wstring()}});
        }
        paintDetail(canvas, {b.x, t.bottom(), b.width, kDetailLine}, app.name, text);
    } else {
        paintDetail(canvas, {b.x, t.bottom(), b.width, kDetailLine}, L"", m_strings.get(Str::AppsHint));
    }
}

} // namespace wl::app
