#include "app/pages/PostSetupPage.h"

#include "app/Format.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using core::PostSetupPlan;
using core::PostSetupStep;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kTop = 11.0f;
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 12.0f;
constexpr float kWhenWidth = 267.0f;
constexpr float kNote = 32.0f;
constexpr float kHint = 24.0f; // key hint line under the table
constexpr float kTag = 16.0f;

// Screen 12 columns; the content of each starts one cell padding in.
enum Column : int { kHandle, kNumber, kStep, kType, kSource, kWait };

ui::icons::Icon iconOf(PostSetupStep::Type type) {
    switch (type) {
    case PostSetupStep::Type::Winget: return ui::icons::Icon::AppxPackage;
    case PostSetupStep::Type::Copy: return ui::icons::Icon::Copy;
    default: return ui::icons::Icon::LogTerminal;
    }
}

Str typeName(PostSetupStep::Type type) {
    switch (type) {
    case PostSetupStep::Type::Winget: return Str::PostsetupTypesWinget;
    case PostSetupStep::Type::Copy: return Str::PostsetupTypesCopy;
    default: return Str::PostsetupTypesCommand;
    }
}

} // namespace

PostSetupPage::PostSetupPage(AppState& state, PostSetupController& controller, const Localization& strings,
                             Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_when = &add<ui::Dropdown>(s(Str::PostsetupRunAt),
                                std::vector<std::wstring>{s(Str::PostsetupFirstLogon), s(Str::PostsetupSetupComplete)}, 0);
    m_when->onChange = [this](int index) {
        m_controller.setWhen(index == 1 ? PostSetupPlan::When::SetupComplete : PostSetupPlan::When::FirstLogon);
        refresh(); // without steps nothing is queued: the control goes back
    };
    m_continue = &add<ui::Toggle>(s(Str::PostsetupContinueOnError), true);
    m_continue->onChange = [this](bool on) {
        m_controller.setContinueOnError(on);
        refresh();
    };
    m_note = &add<ui::InfoBar>(ui::InfoKind::Info, s(Str::PostsetupWingetLater), L"", s(Str::CommonClose));
    m_note->setVisible(false);
    m_note->onClose = [this] {
        m_note->setVisible(false);
        layout();
    };

    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {L"", 20},
        {L"#", 32},
        {s(Str::PostsetupStep), 0},
        {s(Str::CommonType), 100},
        {s(Str::PostsetupSourceCol), 340},
        {s(Str::PostsetupWait), 88},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kWait && row >= 0) {
            m_controller.toggleWait(static_cast<std::size_t>(row));
        }
    };
    m_table->onActivate = [this](int row) {
        if (row >= 0 && m_intents.editStep) {
            m_intents.editStep(static_cast<std::size_t>(row));
        }
    };
    m_table->onKey = [this](const ui::KeyEvent& key) { return onTableKey(key); };

    m_empty = &add<ui::EmptyState>(ui::icons::Icon::PostSetupRocket, s(Str::PostsetupNoMountTitle),
                                   s(Str::PostsetupNoMountBody));
    m_empty->setAction(s(Str::FeaturesGoImages)).onInvoke = m_intents.goImages;
    setAccessible(ui::AccessRole::Group, s(Str::PostsetupTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Queue) {
            refresh();
        }
    });
    refresh();
}

PostSetupPage::~PostSetupPage() {
    m_state.unsubscribe(m_subscription);
}

bool PostSetupPage::onTableKey(const ui::KeyEvent& key) {
    const int row = m_table->selected();
    if (row < 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(row);
    if (key.virtualKey == VK_DELETE) {
        m_controller.remove(index);
        return true;
    }
    if (key.alt && (key.virtualKey == VK_UP || key.virtualKey == VK_DOWN)) {
        const int delta = key.virtualKey == VK_UP ? -1 : 1;
        if (m_controller.move(index, delta)) {
            m_table->setSelected(row + delta); // the selection travels with the step
        }
        return true;
    }
    return false;
}

void PostSetupPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    const PostSetupPlan& plan = m_controller.plan();
    m_empty->setVisible(!mounted);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_when, m_continue, m_table}) {
        w->setVisible(mounted);
    }
    m_when->setSelected(plan.when == PostSetupPlan::When::SetupComplete ? 1 : 0);
    if (m_continue->isOn() != plan.continueOnError) {
        m_continue->setOn(plan.continueOnError);
    }
    const bool wingetWaits = plan.when == PostSetupPlan::When::SetupComplete &&
                             std::ranges::any_of(plan.steps, [](const PostSetupStep& s) { return s.type == PostSetupStep::Type::Winget; });
    m_note->setVisible(mounted && wingetWaits);
    m_table->setRowCount(static_cast<int>(plan.steps.size()));
    m_table->refresh();
    layout();
    invalidate();
}

void PostSetupPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const PostSetupPlan& plan = m_controller.plan();
    if (row < 0 || row >= static_cast<int>(plan.steps.size())) {
        return;
    }
    const PostSetupStep& step = plan.steps[static_cast<std::size_t>(row)];
    const float iconY = rect.y + (rect.height - ui::tokens::size::icon) / 2;
    switch (column) {
    case kHandle:
        // Screen 12 puts the handle at the row's very left edge.
        canvas.drawIcon(ui::icons::Icon::DragHandle, {rect.x - ui::TableView::kCellPad, iconY}, Color::TextSecondary);
        break;
    case kNumber: canvas.drawText(std::to_wstring(row + 1), rect, TypeStyle::Mono, Color::TextPrimary); break;
    case kStep: {
        canvas.drawIcon(iconOf(step.type), {rect.x, iconY}, Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + 6;
        canvas.drawText(step.name.empty() ? step.source : step.name, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                        cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kType: canvas.drawText(m_strings.get(typeName(step.type)), rect, TypeStyle::Body, Color::TextPrimary); break;
    case kSource: {
        PostSetupPlan one;
        one.steps.push_back(step);
        const bool problem = !core::validatePostSetup(one).empty(); // only a hand-edited preset gets here
        const std::wstring text = step.type == PostSetupStep::Type::Copy
                                      ? std::format(L"{} → {}", step.source, step.destination)
                                      : step.source;
        canvas.drawText(text, rect, TypeStyle::Mono, problem ? Color::StatusError : Color::TextPrimary);
        break;
    }
    case kWait: {
        // A tag: yes / no for commands (click toggles); the others always finish before the next.
        const std::wstring text = step.type != PostSetupStep::Type::Command ? m_strings.get(Str::CommonYes)
                                  : step.wait                              ? m_strings.get(Str::CommonYes)
                                                                           : m_strings.get(Str::CommonNo);
        const float width = std::ceil(canvas.text().measure(text, TypeStyle::Caption)) + 8;
        const RectF tag{rect.x, rect.y + (rect.height - kTag) / 2, width, kTag};
        const bool clickable = step.type == PostSetupStep::Type::Command;
        canvas.strokeRoundRect(tag, ui::tokens::radius::r2, clickable && cell.hoveredCell ? Color::TextTertiary : Color::LineStrong);
        canvas.drawText(text, {tag.x + 4, tag.y, tag.width - 8, tag.height}, TypeStyle::Caption,
                        clickable ? Color::TextSecondary : Color::TextTertiary);
        break;
    }
    default: break;
    }
}

void PostSetupPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    m_when->setBounds({b.x, y, kWhenWidth, kToolbar});
    const float toggleX = b.x + kWhenWidth + 8;
    m_continue->setBounds({toggleX, y, m_continue->measure({}).width, kToolbar});
    y += kToolbar + kGap;
    if (m_note->visible()) {
        m_note->setBounds({b.x, y, b.width, kNote});
        y += kNote + kGap;
    }
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y - kHint, 0.0f)});
}

void PostSetupPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const PostSetupPlan& plan = m_controller.plan();
    // Toolbar, right: "7 adım · ~4 dk tahmini".
    const std::wstring summary = m_strings.format(
        Str::PostsetupSummary,
        {{L"n", std::to_wstring(plan.steps.size())},
         {L"t", formatDuration(core::estimatePostSetupSeconds(plan), m_language, /*approx=*/true)}});
    canvas.drawText(summary, {b.x, b.y + kTop, b.width, kToolbar}, TypeStyle::Caption, Color::TextSecondary,
                    ui::TextAlign::Trailing);
    if (plan.steps.empty()) {
        canvas.drawText(m_strings.get(Str::PostsetupEmpty),
                        {b.x, m_table->bounds().y + ui::TableView::kHeader + 12, b.width, 20}, TypeStyle::Body,
                        Color::TextTertiary, ui::TextAlign::Center);
    } else {
        canvas.drawText(m_strings.get(Str::PostsetupKeysHint), {b.x, b.bottom() - kHint, b.width, kHint}, TypeStyle::Caption,
                        Color::TextTertiary);
    }
}

} // namespace wl::app
