#include "app/pages/ServicesPage.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cwctype>

namespace wl::app {

using core::ServiceEntry;
using core::StartType;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
constexpr float kTableGap = 12.0f;
constexpr float kChoiceH = 20.0f;

enum Column : int { kService, kName, kDefault, kNew, kRisk };

constexpr StartType kChoices[] = {StartType::Auto, StartType::AutoDelayed, StartType::Manual, StartType::Disabled};

std::wstring lowered(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

// Boot/System services are listed under "Otomatik" in the filter (they start with Windows).
bool matchesFilter(StartType start, int filter) {
    if (filter == 0) {
        return true;
    }
    const StartType want = kChoices[filter - 1];
    if (want == StartType::Auto) {
        return start == StartType::Auto || start == StartType::Boot || start == StartType::System;
    }
    return start == want;
}
} // namespace

Str ServicesPage::startName(StartType start) {
    switch (start) {
    case StartType::Boot: return Str::ServicesStartBoot;
    case StartType::System: return Str::ServicesStartSystem;
    case StartType::Auto: return Str::ServicesStartAuto;
    case StartType::AutoDelayed: return Str::ServicesStartAutoDelayed;
    case StartType::Manual: return Str::ServicesStartManual;
    case StartType::Disabled: return Str::ServicesStartDisabled;
    }
    return Str::ServicesStartManual;
}

ServicesPage::ServicesPage(AppState& state, ServiceController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::ServicesSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = lowered(text);
        refilter();
    };
    std::vector<std::wstring> filters{strings.get(Str::CommonAll)};
    for (const auto start : kChoices) {
        filters.push_back(strings.get(startName(start)));
    }
    m_startBox = &add<ui::Dropdown>(strings.get(Str::ServicesStartFilter), std::move(filters), 0);
    m_startBox->onChange = [this](int index) {
        m_startFilter = index;
        refilter();
    };
    m_changed = &add<ui::Toggle>(strings.get(Str::CommonOnlyChanged), false);
    m_changed->onChange = [this](bool on) {
        m_onlyChanged = on;
        refilter();
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::ServicesService), 0},
        {strings.get(Str::CommonName), 180},
        {strings.get(Str::ServicesDefault), 150},
        {strings.get(Str::ServicesNewStart), 184},
        {strings.get(Str::RiskColumn), 80},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kNew) {
            openChoice(row);
        }
    };
    m_table->onActivate = [this](int row) { openChoice(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::ServicesGear, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ServicesTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Services) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_onlyChanged || m_startFilter != 0) {
                refilter();
            } else {
                m_table->refresh();
                invalidate();
            }
        }
    });
    m_controller.load();
    refresh();
}

ServicesPage::~ServicesPage() {
    m_state.unsubscribe(m_subscription);
}

void ServicesPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

void ServicesPage::reveal(const std::wstring& name) {
    m_search->setText({});
    m_needle.clear();
    m_startBox->setSelected(0);
    m_startFilter = 0;
    m_changed->setOn(false, /*animated=*/false);
    m_onlyChanged = false;
    refilter();
    for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
        if (const auto* item = itemAt(row); item && item->name == name) {
            m_table->setSelected(row);
            if (host()) {
                host()->setFocus(m_table, /*visible=*/false);
            }
            return;
        }
    }
}

bool ServicesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

const ServiceEntry* ServicesPage::itemAt(int row) const {
    const auto& list = m_state.serviceList();
    if (!list || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    return &list->items[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
}

void ServicesPage::showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                             std::function<void()> onAction) {
    m_empty->setContent(icon, m_strings.get(title), std::move(body));
    if (action) {
        m_empty->setAction(m_strings.get(*action)).onInvoke = std::move(onAction);
    } else {
        m_empty->hideAction();
    }
    m_empty->setVisible(true);
}

void ServicesPage::refresh() {
    const auto& list = m_state.serviceList();
    const bool ready = list && list->status == AppState::ServiceList::Status::Ready;
    if (!m_state.mounted()) {
        showEmpty(ui::icons::Icon::ServicesGear, Str::ServicesNoMountTitle, m_strings.get(Str::ServicesNoMountBody),
                  Str::FeaturesGoImages, m_intents.goImages);
    } else if (!list || list->status == AppState::ServiceList::Status::Loading) {
        m_controller.load();
        showEmpty(ui::icons::Icon::Spinner, Str::ServicesLoadingTitle, m_strings.get(Str::ServicesLoadingBody),
                  std::nullopt, {});
    } else if (list->status == AppState::ServiceList::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::ServicesFailedTitle,
                  list->error.message + L" — " + list->error.context, Str::CommonRetry,
                  [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_startBox, m_changed, m_table}) {
        w->setVisible(ready);
    }
    refilter();
    layout();
    invalidate();
}

void ServicesPage::refilter() {
    m_rows.clear();
    const auto& list = m_state.serviceList();
    if (list && list->status == AppState::ServiceList::Status::Ready) {
        for (std::size_t i = 0; i < list->items.size(); ++i) {
            const auto& s = list->items[i];
            if (m_onlyChanged && !m_controller.changed(s)) {
                continue;
            }
            if (!matchesFilter(m_controller.target(s), m_startFilter)) {
                continue;
            }
            if (!m_needle.empty() && lowered(s.displayName).find(m_needle) == std::wstring::npos &&
                lowered(s.name).find(m_needle) == std::wstring::npos) {
                continue;
            }
            m_rows.push_back(static_cast<int>(i));
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

RectF ServicesPage::choiceRect(RectF cell) const {
    return {cell.x, cell.y + (cell.height - kChoiceH) / 2, std::min(cell.width - 8, 176.0f), kChoiceH};
}

void ServicesPage::openChoice(int row) {
    const auto* item = itemAt(row);
    if (!item || !host()) {
        return;
    }
    std::vector<std::wstring> names;
    int selected = -1;
    const StartType current = m_controller.target(*item);
    for (std::size_t i = 0; i < std::size(kChoices); ++i) {
        names.push_back(m_strings.get(startName(kChoices[i])));
        if (kChoices[i] == current) {
            selected = static_cast<int>(i);
        }
    }
    const std::wstring name = item->name;
    auto popup = std::make_unique<ui::MenuPopup>(
        choiceRect(m_table->cellRect(row, kNew)), std::move(names), std::max(selected, 0),
        [this, name](int index) {
            const auto& list = m_state.serviceList();
            if (!list) {
                return;
            }
            const auto it = std::ranges::find(list->items, name, &ServiceEntry::name);
            if (it != list->items.end()) {
                choose(*it, kChoices[static_cast<std::size_t>(index)]);
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false);
}

void ServicesPage::choose(const ServiceEntry& service, StartType start) {
    m_controller.set(service, start);
    if (start == StartType::Disabled && m_intents.warn) {
        const auto dependents = m_controller.activeDependents(service);
        if (!dependents.empty()) {
            std::wstring list;
            for (std::size_t i = 0; i < dependents.size() && i < 6; ++i) {
                list += (i ? L", " : L"") + dependents[i];
            }
            if (dependents.size() > 6) {
                list += L" …";
            }
            m_intents.warn(m_strings.format(Str::ServicesDependentsWarn, {{L"name", service.displayName},
                                                                           {L"n", std::to_wstring(dependents.size())}}),
                           list);
        }
    }
}

void ServicesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    const bool changed = m_controller.changed(*item);
    switch (column) {
    case kService: {
        canvas.drawIcon(ui::icons::Icon::ServicesGear, {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(item->displayName, {x, rect.y, rect.right() - x, rect.height},
                        cell.selected || changed ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kName:
        canvas.drawText(item->name, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    case kDefault:
        canvas.drawText(m_strings.get(startName(item->start)), rect, TypeStyle::Body, Color::TextPrimary);
        break;
    case kNew: {
        const RectF box = choiceRect(rect);
        const Color border = changed ? Color::AccentBase : cell.hoveredCell ? Color::TextTertiary : Color::LineStrong;
        canvas.fillRoundRect(box, ui::tokens::radius::r2, Color::BgInput);
        canvas.strokeRoundRect(box, ui::tokens::radius::r2, border);
        const float chevronX = box.right() - 4 - ui::tokens::size::icon;
        canvas.drawText(m_strings.get(startName(m_controller.target(*item))),
                        {box.x + 6, box.y, chevronX - box.x - 8, box.height}, TypeStyle::Body, Color::TextPrimary);
        canvas.drawIcon(ui::icons::Icon::ChevronDown, {chevronX, box.y + (box.height - ui::tokens::size::icon) / 2},
                        Color::TextTertiary);
        break;
    }
    case kRisk: {
        const auto risk = m_controller.risk(*item);
        const Color ink = risk == core::ops::Risk::Low    ? Color::StatusSuccess
                          : risk == core::ops::Risk::High ? Color::StatusError
                                                          : Color::StatusWarning;
        canvas.fillRect({rect.x, rect.y + (rect.height - 6) / 2, 6, 6}, ink);
        const Str text = risk == core::ops::Risk::Low    ? Str::RiskLow
                         : risk == core::ops::Risk::High ? Str::RiskHigh
                                                         : Str::RiskMedium;
        canvas.drawText(m_strings.get(text), {rect.x + 12, rect.y, rect.width - 12, rect.height}, TypeStyle::Caption,
                        Color::TextSecondary);
        break;
    }
    default: break;
    }
}

void ServicesPage::layout() {
    const RectF b = bounds();
    const float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(240);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_startBox, m_changed}) {
        const ui::SizeF size = w->measure({});
        w->setBounds({x, y, size.width, kToolbar});
        x += size.width + kGap;
    }
    const float top = y + kToolbar + kTableGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    m_empty->setBounds(b);
}

void ServicesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const float left = m_changed->bounds().right() + kGap;
    const auto& list = m_state.serviceList();
    std::wstring summary = m_strings.format(Str::CommonChangesQueued, {{L"n", std::to_wstring(m_controller.queuedCount())}});
    if (list) {
        summary = m_strings.format(Str::CommonItems, {{L"n", std::to_wstring(list->items.size())}}) + L" · " + summary;
    }
    canvas.drawText(summary, {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
    if (m_rows.empty()) {
        canvas.drawText(m_strings.get(Str::ServicesNoResults),
                        {b.x, m_table->bounds().y + ui::TableView::kHeader + 12, b.width, 20}, TypeStyle::Body,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
}

} // namespace wl::app
