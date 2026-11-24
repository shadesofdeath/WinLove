#include "app/pages/PoliciesPage.h"

#include "app/Format.h"
#include "base/Text.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace wl::app {

using core::policy::PolicyState;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr float kTableGap = 12.0f;
constexpr float kChoiceH = ui::tokens::size::controlSmall;
constexpr float kChoiceW = 150.0f;
constexpr float kIconGap = ui::tokens::spacing::s3;
constexpr int kMarkSlots = 1; // state

enum Column : int { kPolicy, kMarks, kCategory, kScope, kState };

constexpr PolicyState kChoices[] = {PolicyState::NotConfigured, PolicyState::Enabled, PolicyState::Disabled};
} // namespace

Str PoliciesPage::stateName(PolicyState state) {
    switch (state) {
    case PolicyState::Enabled: return Str::PoliciesStateEnabled;
    case PolicyState::Disabled: return Str::PoliciesStateDisabled;
    case PolicyState::NotConfigured: break;
    }
    return Str::PoliciesStateNotConfigured;
}

PoliciesPage::PoliciesPage(AppState& state, PolicyController& controller, const Localization& strings, Language language,
                           Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::PoliciesSearch), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        refilter();
    };
    m_scopeBox = &add<ui::Dropdown>(strings.get(Str::PoliciesScope),
                                    std::vector<std::wstring>{strings.get(Str::PoliciesScopeAll), strings.get(Str::PoliciesScopeMachine),
                                                              strings.get(Str::PoliciesScopeUser)},
                                    0);
    m_scopeBox->onChange = [this](int index) {
        m_scope = index;
        refilter();
    };
    m_rootBox = &add<ui::Dropdown>(strings.get(Str::PoliciesCategory), std::vector<std::wstring>{strings.get(Str::PoliciesCategoryAll)}, 0);
    m_rootBox->onChange = [this](int index) {
        m_root = index;
        refilter();
    };
    m_changed = &add<ui::Toggle>(strings.get(Str::CommonOnlyChanged), false);
    m_changed->onChange = [this](bool on) {
        m_onlyChanged = on;
        refilter();
    };
    m_summaryLine = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::PoliciesColPolicy), 0, ui::TextAlign::Leading, true},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::PoliciesColCategory), 220, ui::TextAlign::Leading, true, 140},
        {strings.get(Str::PoliciesColScope), 110, ui::TextAlign::Leading, true, 90},
        {.title = strings.get(Str::PoliciesColState), .width = 176, .minWidth = kChoiceW + ui::tokens::spacing::s4},
    });
    m_table->setFlexMin(kNameColumnMin);
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->cellText = [this](int row, int column) -> ui::TableCellText {
        const auto* r = rowAt(row);
        const auto* policy = r ? m_controller.policyOf(*r) : nullptr;
        if (!policy) {
            return {};
        }
        if (column == kPolicy) {
            return {policy->displayName, TypeStyle::BodyStrong, ui::tokens::size::icon + kIconGap};
        }
        if (column == kCategory) {
            const auto path = m_controller.catalog()->path(*policy);
            return {path.empty() ? std::wstring() : path.back(), TypeStyle::Body};
        }
        return {};
    };
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        const auto* r = rowAt(row);
        const auto* policy = r ? m_controller.policyOf(*r) : nullptr;
        if (!policy || column != kCategory) {
            return {};
        }
        std::wstring path;
        for (const auto& name : m_controller.catalog()->path(*policy)) {
            path += (path.empty() ? L"" : L" › ") + name;
        }
        return path;
    };
    m_table->sortKey = [this](int row, int column) -> ui::TableSortKey {
        const auto* r = rowAt(row);
        const auto* policy = r ? m_controller.policyOf(*r) : nullptr;
        if (!policy) {
            return {};
        }
        switch (column) {
        case kPolicy: return {policy->displayName};
        case kCategory: {
            const auto path = m_controller.catalog()->path(*policy);
            return {path.empty() ? std::wstring() : path.back()};
        }
        case kScope: return {m_strings.get(r->user ? Str::PoliciesScopeUser : Str::PoliciesScopeMachine)};
        case kState: return {m_strings.get(stateName(m_controller.choice(*r).state))};
        default: return {};
        }
    };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF) {
        if (column == kState) {
            openChoice(row);
        }
    };
    m_table->onActivate = [this](int row) { openChoice(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::ScriptFile, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::PoliciesTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Policies) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            if (m_onlyChanged) {
                refilter();
            } else {
                m_table->refresh();
                invalidate();
            }
            updateSummary();
            detailChanged();
        }
    });
    m_controller.load();
    refresh();
}

PoliciesPage::~PoliciesPage() {
    m_state.unsubscribe(m_subscription);
}

void PoliciesPage::selectRow(int row) {
    if (row >= 0 && row < static_cast<int>(m_rows.size())) {
        m_table->setSelected(row);
        detailChanged();
    }
}

void PoliciesPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

void PoliciesPage::search(const std::wstring& text) {
    m_search->setText(text);
    m_needle = wl::text::fold(text);
    refilter();
}

bool PoliciesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

const PolicyController::Row* PoliciesPage::rowAt(int row) const {
    const auto& rows = m_controller.rows();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    return index < rows.size() ? &rows[index] : nullptr;
}

void PoliciesPage::showEmpty(ui::icons::Icon icon, Str title, std::wstring body, std::optional<Str> action,
                             std::function<void()> onAction) {
    m_empty->setContent(icon, m_strings.get(title), std::move(body));
    if (action) {
        m_empty->setAction(m_strings.get(*action)).onInvoke = std::move(onAction);
    } else {
        m_empty->hideAction();
    }
    m_empty->setVisible(true);
}

void PoliciesPage::rebuildIndex() {
    const auto* catalog = m_controller.catalog();
    if (catalog == m_indexed) {
        return;
    }
    m_indexed = catalog;
    m_haystack.clear();
    m_rootOf.clear();
    m_rootIds.clear();
    std::vector<std::wstring> rootNames{m_strings.get(Str::PoliciesCategoryAll)};
    if (catalog) {
        // The roots by their names (as the filter lists them), then each row's root and search text.
        std::map<std::wstring, std::wstring> roots; // folded name → id
        for (const auto& policy : catalog->policies()) {
            if (const std::wstring id = catalog->rootOf(policy); !id.empty()) {
                if (const auto* c = catalog->category(id)) {
                    roots.emplace(wl::text::fold(c->displayName), id);
                }
            }
        }
        for (const auto& [name, id] : roots) {
            m_rootIds.push_back(id);
            rootNames.push_back(catalog->category(id)->displayName);
        }
        for (const auto& row : m_controller.rows()) {
            const auto& policy = catalog->policies()[row.policy];
            std::wstring text = policy.displayName + L"\n" + policy.name + L"\n" + policy.key + L"\\" + policy.valueName;
            for (const auto& name : catalog->path(policy)) {
                text += L"\n" + name;
            }
            m_haystack.push_back(wl::text::fold(text));
            const std::wstring root = catalog->rootOf(policy);
            const auto it = std::ranges::find_if(m_rootIds, [&](const std::wstring& id) { return wl::text::iequals(id, root); });
            m_rootOf.push_back(it == m_rootIds.end() ? -1 : static_cast<int>(it - m_rootIds.begin()));
        }
    }
    m_root = 0;
    m_rootBox->setItems(std::move(rootNames), 0);
}

void PoliciesPage::refresh() {
    const auto& list = m_state.policyList();
    const bool ready = list && list->status == AppState::PolicyList::Status::Ready;
    if (!m_state.mounted()) {
        showEmpty(ui::icons::Icon::ScriptFile, Str::PoliciesNoMountTitle, m_strings.get(Str::PoliciesNoMountBody),
                  Str::CommonGoImages, m_intents.goImages);
    } else if (!list || list->status == AppState::PolicyList::Status::Loading) {
        m_controller.load();
        showEmpty(ui::icons::Icon::ScriptFile, Str::PoliciesLoadingTitle, m_strings.get(Str::PoliciesLoadingBody),
                  std::nullopt, {});
    } else if (list->status == AppState::PolicyList::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::PoliciesFailedTitle, list->error.message + L" — " + list->error.context,
                  Str::CommonRetry, [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_scopeBox, m_rootBox, m_changed, m_summaryLine, m_table}) {
        w->setVisible(ready);
    }
    rebuildIndex();
    refilter();
    updateSummary();
    layout();
    invalidate();
    detailChanged();
}

void PoliciesPage::refilter() {
    m_rows.clear();
    const auto& rows = m_controller.rows();
    if (m_controller.catalog() && m_haystack.size() == rows.size()) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            if ((m_scope == 1 && row.user) || (m_scope == 2 && !row.user)) {
                continue;
            }
            if (m_root > 0 && m_rootOf[i] != m_root - 1) {
                continue;
            }
            if (m_onlyChanged && !m_controller.changed(row)) {
                continue;
            }
            if (!m_needle.empty() && m_haystack[i].find(m_needle) == std::wstring::npos) {
                continue;
            }
            m_rows.push_back(static_cast<int>(i));
        }
    }
    m_rowStates.assign(m_rows.size(), RowState{});
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

void PoliciesPage::updateSummary() {
    std::vector<SummaryPart> parts;
    if (m_controller.catalog()) {
        parts.push_back({m_strings.format(Str::PoliciesSummary, {{L"n", formatCount(m_controller.rows().size(), m_language)}})});
        if (const int n = m_controller.changedCount(); n > 0) {
            const std::wstring text = m_strings.format(Str::PoliciesSummaryChange, {{L"n", formatCount(static_cast<std::size_t>(n), m_language)}});
            parts.push_back({text, SummaryPart::Tone::Change, text});
        }
    }
    m_summaryLine->set(std::move(parts));
}

RowMarks PoliciesPage::marksOf(int row) {
    RowMarks marks;
    const auto* r = rowAt(row);
    if (!r || static_cast<std::size_t>(row) >= m_rowStates.size()) {
        return marks;
    }
    RowState& state = m_rowStates[static_cast<std::size_t>(row)];
    if (const std::uint64_t revision = m_state.revision(); state.at != revision) {
        state.at = revision;
        state.changed = m_controller.changed(*r);
        state.state = m_controller.choice(*r).state;
    }
    marks.mark = state.changed ? RowMark::Queued : RowMark::None;
    return marks;
}

DetailModel PoliciesPage::detail() const {
    DetailModel model;
    const auto* r = m_table->visible() ? rowAt(m_table->selected()) : nullptr;
    const auto* policy = r ? m_controller.policyOf(*r) : nullptr;
    if (!policy) {
        return model;
    }
    const auto choice = m_controller.choice(*r);
    const bool changed = m_controller.changed(*r);
    model.icon = ui::icons::Icon::ScriptFile;
    model.title = policy->displayName;
    model.identity = policy->key + (policy->valueName.empty() ? L"" : L"\\" + policy->valueName);
    if (changed) {
        model.state = {ui::icons::Icon::QueueClock,
                       m_strings.format(Str::PoliciesWillBe, {{L"state", m_strings.get(stateName(choice.state))}}), DetailTone::Change};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::PoliciesKeeps), DetailTone::Quiet};
    }
    model.about = policy->explain;
    model.facts.push_back({m_strings.get(Str::PoliciesColScope), m_strings.get(r->user ? Str::PoliciesScopeUser : Str::PoliciesScopeMachine)});
    std::wstring path;
    for (const auto& name : m_controller.catalog()->path(*policy)) {
        path += (path.empty() ? L"" : L" › ") + name;
    }
    if (!path.empty()) {
        model.facts.push_back({m_strings.get(Str::PoliciesColCategory), path});
    }
    model.facts.push_back({m_strings.get(Str::DetailAtApply), m_strings.get(stateName(choice.state)),
                           changed ? DetailTone::Change : DetailTone::Normal});
    if (!policy->supportedOn.empty()) {
        model.facts.push_back({m_strings.get(Str::PoliciesFactSupported), policy->supportedOn});
    }
    model.facts.push_back({m_strings.get(Str::PoliciesFactFile), policy->file});
    model.facts.push_back({m_strings.get(Str::PoliciesFactRegistry), std::wstring(r->user ? L"HKCU\\" : L"HKLM\\") + policy->key,
                           DetailTone::Normal, true});
    if (choice.state == PolicyState::Enabled && !choice.values.empty()) {
        DetailList list{m_strings.get(Str::PoliciesFactOptions)};
        for (const auto& control : policy->presentation) {
            const auto it = choice.values.find(control.refId);
            if (control.refId.empty() || it == choice.values.end()) {
                continue;
            }
            std::wstring value = it->second;
            if (const auto* e = policy->element(control.refId); e && e->kind == core::policy::PolicyElement::Kind::Enum) {
                const auto index = static_cast<std::size_t>(_wtoi(value.c_str()));
                value = index < e->items.size() ? e->items[index].displayName : value;
            } else if (e && e->kind == core::policy::PolicyElement::Kind::Boolean) {
                value = m_strings.get(value == L"1" ? Str::CommonYes : Str::CommonNo);
            }
            std::ranges::replace(value, L'\n', L';');
            list.lines.push_back(control.label + L" " + value);
        }
        if (!list.lines.empty()) {
            model.lists.push_back(std::move(list));
        }
    }
    model.notes.push_back({ui::icons::Icon::InfoCircle, m_strings.get(Str::PoliciesNoteGpedit)});
    model.actions.push_back({kActionChoose, m_strings.get(Str::PoliciesChooseState)});
    if (!policy->elements.empty()) {
        model.actions.push_back({kActionOptions, m_strings.get(Str::PoliciesOptions)});
    }
    if (changed) {
        model.actions.push_back({kActionRevert, m_strings.get(Str::PoliciesRevert)});
    }
    return model;
}

void PoliciesPage::detailAction(int id) {
    const int row = m_table->selected();
    const auto* r = rowAt(row);
    if (!r) {
        return;
    }
    const PolicyController::Row copy = *r;
    if (id == kActionChoose) {
        openChoice(row);
    } else if (id == kActionOptions && m_intents.editOptions) {
        m_intents.editOptions(copy);
    } else if (id == kActionRevert) {
        choose(copy, PolicyState::NotConfigured);
    }
}

RectF PoliciesPage::choiceRect(RectF cell) const {
    return {cell.x, cell.y + std::round((cell.height - kChoiceH) / 2), std::min(cell.width - ui::tokens::spacing::s4, kChoiceW), kChoiceH};
}

void PoliciesPage::openChoice(int row) {
    const auto* r = rowAt(row);
    if (!r || !host()) {
        return;
    }
    std::vector<std::wstring> names;
    int selected = 0;
    const PolicyState current = m_controller.choice(*r).state;
    for (std::size_t i = 0; i < std::size(kChoices); ++i) {
        names.push_back(m_strings.get(stateName(kChoices[i])));
        if (kChoices[i] == current) {
            selected = static_cast<int>(i);
        }
    }
    const PolicyController::Row copy = *r;
    auto popup = std::make_unique<ui::MenuPopup>(
        choiceRect(m_table->cellRect(row, kState)), std::move(names), selected,
        [this, copy](int index) { choose(copy, kChoices[static_cast<std::size_t>(index)]); }, [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false, /*owner=*/this);
}

void PoliciesPage::choose(PolicyController::Row row, PolicyState state) {
    const auto* policy = m_controller.policyOf(row);
    if (!policy) {
        return;
    }
    // gpedit: enabling a policy with options shows them (the defaults are what Etkin writes).
    if (state == PolicyState::Enabled && !policy->elements.empty() && m_intents.editOptions) {
        m_intents.editOptions(row);
        return;
    }
    if (auto r = m_controller.set(row, state); !r && m_intents.warn) {
        m_intents.warn(m_strings.get(Str::PoliciesInvalid), r.error().message + L" — " + r.error().context);
    }
}

void PoliciesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    const auto* r = rowAt(row);
    const auto* policy = r ? m_controller.policyOf(*r) : nullptr;
    if (!policy || static_cast<std::size_t>(row) >= m_rowStates.size()) {
        return;
    }
    const RowMarks marks = marksOf(row);
    const RowState& state = m_rowStates[static_cast<std::size_t>(row)];
    switch (column) {
    case kMarks: paintMarks(canvas, rect, marks, kMarkSlots); break;
    case kPolicy: {
        canvas.drawIcon(ui::icons::Icon::ScriptFile, {rect.x, rect.y + std::round((rect.height - ui::tokens::size::icon) / 2)},
                        Color::TextSecondary);
        const float x = rect.x + ui::tokens::size::icon + kIconGap;
        canvas.drawText(policy->displayName, {x, rect.y, rect.right() - x, rect.height},
                        cell.selected || state.changed ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kCategory: {
        const auto path = m_controller.catalog()->path(*policy);
        canvas.drawText(path.empty() ? std::wstring() : path.back(), rect, TypeStyle::Body, Color::TextSecondary);
        break;
    }
    case kScope:
        canvas.drawText(m_strings.get(r->user ? Str::PoliciesScopeUser : Str::PoliciesScopeMachine), rect, TypeStyle::Body,
                        Color::TextPrimary);
        break;
    case kState: {
        const RectF box = choiceRect(rect);
        const Color border = state.changed ? Color::AccentBase : cell.hoveredCell ? Color::TextTertiary : Color::LineStrong;
        canvas.fillRoundRect(box, ui::tokens::radius::r2, Color::BgInput);
        canvas.strokeRoundRect(box, ui::tokens::radius::r2, border);
        const float chevronX = box.right() - 4 - ui::tokens::size::icon;
        canvas.drawText(m_strings.get(stateName(state.state)), {box.x + 6, box.y, chevronX - box.x - 8, box.height},
                        TypeStyle::Body, Color::TextPrimary);
        canvas.drawIcon(ui::icons::Icon::ChevronDown, {chevronX, box.y + (box.height - ui::tokens::size::icon) / 2},
                        Color::TextTertiary);
        break;
    }
    default: break;
    }
}

void PoliciesPage::layout() {
    const RectF b = bounds();
    float x = b.x;
    m_search->setWidth(detailShown() ? 160.0f : 240.0f);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_scopeBox, m_rootBox, m_changed}) {
        const ui::SizeF size = w->measure({});
        float width = size.width;
        if (w == m_rootBox) {
            width = std::min(width, detailShown() ? 170.0f : 240.0f);
        } else if (w == m_scopeBox && detailShown()) {
            width = std::min(width, 170.0f);
        }
        w->setBounds({x, b.y, width, kToolbar});
        x += width + kGap;
    }
    m_summaryLine->setBounds({x, b.y, std::max(b.right() - x, 0.0f), kToolbar});
    const float top = b.y + kToolbar + kTableGap;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kCategory, kScope});
    m_empty->setBounds(b);
}

void PoliciesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {b.x, m_table->bounds().y, b.width, 0}, m_strings.get(Str::PoliciesNoResults));
    }
}

} // namespace wl::app
