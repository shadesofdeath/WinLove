#include "app/pages/FeaturesPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widget/Host.h"

namespace wl::app {

using core::OptionalFeature;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Status = FeatureController::Status;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
constexpr float kTableGap = 12.0f;

enum Column : int { kName, kType, kState, kTarget, kSize };
} // namespace

FeaturesPage::FeaturesPage(AppState& state, FeatureController& controller, const Localization& strings,
                           Language language, std::function<void()> goToImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_goToImages(std::move(goToImages)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::FeaturesSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        refilter();
    };
    m_stateBox = &add<ui::Dropdown>(strings.get(Str::FeaturesState),
                                    std::vector<std::wstring>{strings.get(Str::CommonAll), strings.get(Str::FeaturesOn),
                                                              strings.get(Str::FeaturesOff), strings.get(Str::FeaturesQueued)},
                                    0);
    m_stateBox->onChange = [this](int index) {
        m_filter = static_cast<Filter>(index);
        refilter();
    };
    m_changed = &add<ui::Toggle>(strings.get(Str::CommonOnlyChanged), false);
    m_changed->onChange = [this](bool on) {
        m_onlyChanged = on;
        refilter();
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::FeaturesFeature), 0},
        {strings.get(Str::FeaturesType), 140},
        {strings.get(Str::FeaturesState), 152},
        {strings.get(Str::FeaturesTarget), 72},
        {strings.get(Str::FeaturesSize), 80, ui::TextAlign::Trailing},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kTarget) {
            if (const auto* item = itemAt(row)) {
                m_controller.toggle(*item);
            }
        }
    };
    m_table->onActivate = [this](int row) {
        if (const auto* item = itemAt(row)) {
            m_controller.toggle(*item);
        }
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::PuzzleFeatures, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::FeaturesTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Features) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_onlyChanged || m_filter == Filter::Queued) {
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

FeaturesPage::~FeaturesPage() {
    m_state.unsubscribe(m_subscription);
}

void FeaturesPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

Str FeaturesPage::statusName(Status status) {
    switch (status) {
    case Status::Enabled: return Str::FeaturesEnabled;
    case Status::Installed: return Str::FeaturesInstalled;
    case Status::Disabled: return Str::FeaturesDisabled;
    case Status::Removed: return Str::FeaturesRemoved;
    case Status::Pending: return Str::FeaturesPending;
    case Status::WillEnable: return Str::FeaturesWillEnable;
    case Status::WillDisable: return Str::FeaturesWillDisable;
    case Status::WillRemove: return Str::FeaturesWillRemove;
    case Status::Permanent: return Str::FeaturesPermanent;
    }
    return Str::FeaturesDisabled;
}

void FeaturesPage::reveal(const std::wstring& name) {
    m_search->setText({});
    m_needle.clear();
    m_stateBox->setSelected(0);
    m_filter = Filter::All;
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

bool FeaturesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

const OptionalFeature* FeaturesPage::itemAt(int row) const {
    const auto& features = m_state.optionalFeatures();
    if (!features || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    return &features->items[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)])];
}

void FeaturesPage::showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                             std::function<void()> onAction) {
    // Changed in place, never destroyed here: this may run inside the action button's own click.
    m_empty->setContent(icon, m_strings.get(title), std::move(body));
    if (action) {
        m_empty->setAction(m_strings.get(*action)).onInvoke = std::move(onAction);
    } else {
        m_empty->hideAction();
    }
    m_empty->setVisible(true);
}

void FeaturesPage::refresh() {
    const auto& features = m_state.optionalFeatures();
    const bool ready = features && features->status == AppState::OptionalFeatures::Status::Ready;
    if (!m_state.mounted()) {
        showEmpty(ui::icons::Icon::PuzzleFeatures, Str::FeaturesNoMountTitle, m_strings.get(Str::FeaturesNoMountBody),
                  Str::CommonGoImages, m_goToImages);
    } else if (!features || features->status == AppState::OptionalFeatures::Status::Loading) {
        m_controller.load();
        showEmpty(ui::icons::Icon::Spinner, Str::FeaturesLoadingTitle, m_strings.get(Str::FeaturesLoadingBody),
                  std::nullopt, {});
    } else if (features->status == AppState::OptionalFeatures::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::FeaturesFailedTitle,
                  features->error.message + L" — " + features->error.context, Str::CommonRetry,
                  [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_stateBox, m_changed, m_table}) {
        w->setVisible(ready);
    }
    refilter();
    layout();
    invalidate();
}

void FeaturesPage::refilter() {
    m_rows.clear();
    const auto& features = m_state.optionalFeatures();
    if (features && features->status == AppState::OptionalFeatures::Status::Ready) {
        for (std::size_t i = 0; i < features->items.size(); ++i) {
            const auto& item = features->items[i];
            const bool isQueued = m_controller.queued(item) != nullptr;
            if (m_onlyChanged && !isQueued) {
                continue;
            }
            const bool on = m_controller.targetOn(item);
            if ((m_filter == Filter::On && !on) || (m_filter == Filter::Off && on) ||
                (m_filter == Filter::Queued && !isQueued)) {
                continue;
            }
            if (!m_needle.empty() && wl::text::fold(item.displayName).find(m_needle) == std::wstring::npos &&
                wl::text::fold(item.name).find(m_needle) == std::wstring::npos) {
                continue;
            }
            m_rows.push_back(static_cast<int>(i));
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

void FeaturesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    switch (column) {
    case kName: {
        canvas.drawIcon(ui::icons::Icon::PuzzleFeatures, {rect.x, rect.y + 4}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(item->displayName, {x, rect.y, rect.right() - x, rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kType:
        canvas.drawText(m_strings.get(item->kind == OptionalFeature::Kind::Feature ? Str::FeaturesKindFeature
                                                                                   : Str::FeaturesKindCapability),
                        rect, TypeStyle::Body, Color::TextPrimary);
        break;
    case kState: {
        ui::icons::Icon icon = ui::icons::Icon::InfoCircle;
        Color ink = Color::StatusInfo;
        const Status status = m_controller.status(*item);
        const Str text = statusName(status);
        switch (status) {
        case Status::Enabled: icon = ui::icons::Icon::SuccessCircle; ink = Color::StatusSuccess; break;
        case Status::Installed: icon = ui::icons::Icon::SuccessCircle; ink = Color::StatusSuccess; break;
        case Status::Disabled: break;
        case Status::Removed: ink = Color::TextTertiary; break;
        case Status::Pending: icon = ui::icons::Icon::QueueClock; ink = Color::TextSecondary; break;
        case Status::WillEnable: icon = ui::icons::Icon::QueueClock; ink = Color::AccentBase; break;
        case Status::WillDisable: icon = ui::icons::Icon::QueueClock; ink = Color::AccentBase; break;
        case Status::WillRemove: icon = ui::icons::Icon::QueueClock; ink = Color::AccentBase; break;
        case Status::Permanent: icon = ui::icons::Icon::Lock; ink = Color::TextTertiary; break;
        }
        canvas.drawIcon(icon, {rect.x, rect.y + 4}, ink);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(m_strings.get(text), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Caption,
                        Color::TextSecondary);
        break;
    }
    case kTarget: {
        const RectF track{rect.x, rect.y + (rect.height - ui::tokens::size::toggleH) / 2, ui::tokens::size::toggleW,
                          ui::tokens::size::toggleH};
        if (!m_controller.canToggle(*item)) {
            canvas.pushOpacity(ui::tokens::opacity::disabled);
            ui::Toggle::paintSwitch(canvas, track, 0.0f, false);
            canvas.popOpacity();
        } else {
            ui::Toggle::paintSwitch(canvas, track, m_controller.targetOn(*item) ? 1.0f : 0.0f, cell.hoveredCell);
        }
        break;
    }
    case kSize:
        canvas.drawText(item->size > 0 ? formatBytes(item->size, m_language) : std::wstring(L"—"), rect,
                        TypeStyle::Mono, item->size > 0 ? Color::TextPrimary : Color::TextTertiary,
                        ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void FeaturesPage::layout() {
    const RectF b = bounds();
    const float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(240);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_stateBox, m_changed}) {
        const ui::SizeF size = w->measure({});
        w->setBounds({x, y, size.width, kToolbar});
        x += size.width + kGap;
    }
    const float top = y + kToolbar + kTableGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    m_empty->setBounds(b);
}

void FeaturesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const float left = m_changed->bounds().right() + kGap;
    canvas.drawText(m_strings.format(Str::FeaturesQueuedCount, {{L"n", std::to_wstring(m_controller.queuedCount())}}),
                    {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::FeaturesNoResults));
    }
}

} // namespace wl::app
