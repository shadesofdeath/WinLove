#include "app/pages/AppsPage.h"

#include "app/Format.h"
#include "app/pages/FetchStrip.h"

#include "app/pages/PageBits.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using core::ops::OpKind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTabsTop = 0.0f;
constexpr float kTabsHeight = ui::tokens::size::control;
constexpr float kContentTop = kTabsTop + kTabsHeight + 11.0f;
constexpr float kDrop = 56.0f;
constexpr float kToolbar = ui::tokens::size::control;
enum AppColumn : int { kApp, kPublisher, kVersion, kArch, kDeps };
enum AssocColumn : int { kKind, kApplication, kProgId };
} // namespace

AppsPage::AppsPage(AppState& state, AppsController& controller, const Localization& strings, Language language,
                   std::function<void()> addPackages, std::function<void()> goImages, std::function<void()> stopStore)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_store = &add<FetchStrip>(strings, [this]() -> std::optional<FetchStrip::Status> {
        const auto& fetch = m_state.storeFetch();
        if (!fetch || fetch->stage == AppState::StoreFetch::Stage::Searching) {
            return std::nullopt;
        }
        if (fetch->stage == AppState::StoreFetch::Stage::Resolving || fetch->totalBytes == 0) {
            return FetchStrip::Status{m_strings.format(Str::AppsStoreResolving, {{L"name", fetch->title}}), -1};
        }
        return FetchStrip::Status{m_strings.format(Str::AppsStoreDownloading, {{L"name", fetch->title},
                                                                               {L"done", formatBytes(fetch->doneBytes, m_language)},
                                                                               {L"total", formatBytes(fetch->totalBytes, m_language)}}),
                                  static_cast<double>(fetch->doneBytes) / static_cast<double>(fetch->totalBytes)};
    });
    m_store->onStop = std::move(stopStore);
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::AppsTabAdd), strings.get(Str::AppsTabDefaults)}, 0);
    m_tabs->onChange = [this](int) { refresh(); };
    m_summaryLine = &add<SummaryLine>();
    m_drop = &add<ui::DropZone>(strings.get(Str::AppsDrop), strings.get(Str::AppsDrop), strings.get(Str::AppsDropInvalid),
                                strings.get(Str::AppsDrop));
    m_drop->setCompact(true);
    m_drop->onInvoke = std::move(addPackages);
    m_appTable = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::AppsApp), 0},
        // Narrow enough for the app's name to keep its room at the smallest window (the fixed ones
        // and the name's least ran past the card at 1100 DIP); D-136: they give way to it below that.
        {.title = strings.get(Str::AppsPublisher), .width = 220, .minWidth = 150},
        {.title = strings.get(Str::DriversVersion), .width = 140, .minWidth = 110},
        {.title = strings.get(Str::ImagesArch), .width = 130, .minWidth = 90},
        {.title = strings.get(Str::AppsDeps), .width = 150, .minWidth = 110},
    });
    m_appTable->setFlexMin(kNameColumnMin);
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
    m_appTable->onSelect = [this](int) {
        invalidate();
        detailChanged();
    };
    m_appTable->cellText = [this](int row, int column) -> ui::TableCellText {
        if (column != kApp || row < 0 || row >= static_cast<int>(m_apps.size())) {
            return {};
        }
        const auto& app = m_apps[static_cast<std::size_t>(row)];
        return {app.displayName.empty() ? app.name : app.displayName, TypeStyle::Body, ui::tokens::size::icon + 6};
    };

    std::vector<std::wstring> browsers{strings.get(Str::AppsBrowserUnchanged)};
    for (const auto& b : AppsController::browsers()) {
        browsers.push_back(strings.get(b.label));
    }
    m_browser = &add<ui::Dropdown>(strings.get(Str::AppsBrowser), std::move(browsers), 0);
    m_browser->onChange = [this](int index) { m_controller.setBrowser(index - 1); };
    m_assocTable = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {.title = strings.get(Str::AppsKind), .width = 184, .minWidth = 140},
        {strings.get(Str::AppsApplication), 0},
        {.title = strings.get(Str::AppsProgId), .width = 368, .minWidth = 240},
    });
    m_assocTable->setFlexMin(kNameColumnMin);
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
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::AppsTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::StoreFetch) {
            const bool storeRunning = m_state.mounted() && !defaultsTab() && m_state.storeFetch() &&
                                      m_state.storeFetch()->stage != AppState::StoreFetch::Stage::Searching;
            if (storeRunning == m_store->visible()) {
                // Download progress (audit PG-10): the strip reads the state itself; the two lists
                // come from the queue, which did not change.
                invalidate();
                return;
            }
        }
        if (change == AppState::Change::Mount || change == AppState::Change::Queue || change == AppState::Change::StoreFetch) {
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
    const bool storeRunning = m_state.mounted() && !defaultsTab() && m_state.storeFetch() &&
                              m_state.storeFetch()->stage != AppState::StoreFetch::Stage::Searching;
    if (storeRunning && !m_store->visible()) {
        m_store->setVisible(true);
        m_store->start();
    }
    m_store->setVisible(storeRunning);
    const bool mounted = m_state.mounted().has_value();
    const bool defaults = defaultsTab();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_drop->setVisible(mounted && !defaults && !storeRunning); // the Store strip takes its place
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
    // D-139: the tab row's summary — the apps queued and what they lack (the defaults tab: its rows).
    std::vector<SummaryPart> parts;
    if (mounted && !defaults) {
        std::size_t missing = 0;
        for (const auto& app : m_apps) {
            missing += app.missing.size();
        }
        if (!m_apps.empty()) {
            parts.push_back({m_strings.format(Str::AppsSummary, {{L"n", formatCount(m_apps.size(), m_language)}}), SummaryPart::Tone::Change});
        }
        if (missing > 0) {
            parts.push_back({m_strings.format(Str::AppsSummaryMissing, {{L"n", formatCount(missing, m_language)}}), SummaryPart::Tone::Warning});
        }
    } else if (mounted && !m_assoc.empty()) {
        parts.push_back({m_strings.format(Str::CommonItems, {{L"n", formatCount(m_assoc.size(), m_language)}}), SummaryPart::Tone::Change});
    }
    m_summaryLine->set(std::move(parts));
    m_summaryLine->setVisible(mounted);
    layout();
    invalidate();
    detailChanged();
}

void AppsPage::paintAppCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_apps.size())) {
        return;
    }
    const auto& app = m_apps[static_cast<std::size_t>(row)];
    switch (column) {
    case kApp: {
        // D-139: only the name; the package's file is the detail card's (and the line under the table's).
        canvas.drawIcon(ui::icons::Icon::AppxPackage, {rect.x, rect.y + (rect.height - 16) / 2}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(app.displayName.empty() ? app.name : app.displayName, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
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
        canvas.drawIcon(missing ? ui::icons::Icon::WarningTriangle : ui::icons::Icon::SuccessCircle, {rect.x, rect.y + (rect.height - 16) / 2},
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
    // D-139: the tab row is the page's header row — the tabs at the left, the summary at the right.
    const float tabsW = std::min(m_tabs->measure({}).width, b.width);
    m_tabs->setBounds({b.x, b.y + kTabsTop, tabsW, kTabsHeight});
    m_summaryLine->setBounds({b.x + tabsW + kHeaderGap, b.y + kTabsTop, std::max(b.width - tabsW - kHeaderGap, 0.0f), kTabsHeight});
    float y = b.y + kContentTop;
    m_drop->setBounds({b.x, y, b.width, kDrop});
    m_store->setBounds({b.x, y, b.width, kDrop}); // over the drop zone while a Store download runs
    const float line = detailShown() ? 0.0f : kDetailLine; // the line under the table: the card's when it is there
    m_appTable->setBounds({b.x, y + kDrop + 16, b.width, std::max(b.bottom() - (y + kDrop + 16) - line, 0.0f)});
    fitColumns(*m_appTable, b.width, detailShown(), {kPublisher, kArch, kVersion});
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
            paintTableEmpty(canvas, t, m_strings.get(Str::AppsAssocEmpty));
        }
        paintDetail(canvas, {b.x, t.bottom(), b.width, kDetailLine}, L"", m_strings.get(Str::AppsAssocHint));
        return;
    }
    const RectF t = m_appTable->bounds();
    if (m_apps.empty()) {
        paintTableEmpty(canvas, t, m_strings.get(Str::AppsEmpty));
    }
    if (detailShown()) {
        return; // the selected app is the detail card's
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

namespace wl::app {

DetailModel AppsPage::detail() const {
    DetailModel model;
    const int row = m_appTable->selected();
    if (defaultsTab() || !m_appTable->visible() || row < 0 || row >= static_cast<int>(m_apps.size())) {
        return model;
    }
    const auto& app = m_apps[static_cast<std::size_t>(row)];
    model.icon = ui::icons::Icon::AppxPackage;
    model.title = app.displayName.empty() ? app.name : app.displayName;
    model.subtitle = app.publisher;
    model.identity = app.package.filename().wstring();
    model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::DetailWillAdd), DetailTone::Change};
    model.facts.push_back({m_strings.get(Str::DriversVersion), app.version, DetailTone::Normal, true});
    std::wstring archs;
    for (const auto& a : app.architectures) {
        archs += (archs.empty() ? L"" : L", ") + a;
    }
    if (!archs.empty()) {
        model.facts.push_back({m_strings.get(Str::ImagesArch), archs, DetailTone::Normal, true});
    }
    const bool missing = !app.missing.empty();
    model.facts.push_back({m_strings.get(Str::AppsDeps),
                           missing ? m_strings.format(Str::AppsDepsMissing, {{L"n", std::to_wstring(app.missing.size())}})
                           : app.dependencies.empty() ? m_strings.get(Str::AppsDepsNone)
                                                      : m_strings.format(Str::AppsDepsFound, {{L"n", std::to_wstring(app.dependencies.size())}}),
                           missing ? DetailTone::Warning : DetailTone::Normal});
    model.facts.push_back({m_strings.get(Str::AppsLicenseFact), app.license.empty() ? m_strings.get(Str::AppsLicenseNone) : app.license.filename().wstring()});
    if (app.framework) {
        model.facts.push_back({m_strings.get(Str::CommonType), m_strings.get(Str::AppsFramework)});
    }
    if (missing) {
        model.lists.push_back({m_strings.get(Str::AppsMissingTitle), app.missing, true});
    }
    if (!app.dependencies.empty()) {
        DetailList list{m_strings.get(Str::AppsDependsTitle), {}, true};
        for (const auto& dep : app.dependencies) {
            list.lines.push_back(dep.filename().wstring());
        }
        model.lists.push_back(std::move(list));
    }
    model.actions.push_back({kActionRemove, m_strings.get(Str::UpdatesDequeue)});
    return model;
}

void AppsPage::detailAction(int id) {
    const int row = m_appTable->selected();
    if (id == kActionRemove && row >= 0 && row < static_cast<int>(m_apps.size())) {
        const std::wstring package = m_apps[static_cast<std::size_t>(row)].package.wstring(); // a copy: the queue notifies
        m_state.unqueue(OpKind::AddAppx, package); // the row's Delete
    }
}

} // namespace wl::app
