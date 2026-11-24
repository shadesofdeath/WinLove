#include "app/pages/FeaturesPage.h"
#include <windows.h>
#include <set>
#include <map>
#include <functional>

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Text.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <array>

namespace wl::app {

using core::OptionalFeature;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Status = FeatureController::Status;

namespace {
// D-111 (the reference tool at 175 %, DIP): the tabs 30 high, the filter row 24, then rows of 23.5. A category's
// chevron at 0 and its circle 17 in; an item 17 further in; columns Açıklama · Etkin · Yüklendi.
constexpr float kTabs = 30.0f;
constexpr float kFilterRow = ui::tokens::size::control;
constexpr float kGap = 8.0f;
constexpr float kFilterWidth = 78.0f;
constexpr float kIndent = kTreeIndent; // D-139: one indent for every tree
constexpr float kChevronW = 14.0f;
constexpr float kTextX = 30.0f;
constexpr float kEnabledColumn = 70.0f;
constexpr float kSizeColumn = 90.0f;
constexpr float kNameColumn = 300.0f;
constexpr int kMarkSlots = 1; // D-139: the state (features carry no risk)

enum Column : int { kName, kMarks, kDescription, kEnabled, kSize };

constexpr std::array<Str, 5> kCategoryNames = {Str::FeaturesCatLanguages, Str::FeaturesCatFonts, Str::FeaturesCatDrivers,
                                               Str::FeaturesCatRsat, Str::FeaturesCatOther};

bool startsWithNoCase(const std::wstring& text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && _wcsnicmp(text.c_str(), prefix.data(), prefix.size()) == 0;
}

bool containsNoCase(const std::wstring& text, std::wstring_view part) {
    return wl::text::fold(text).find(wl::text::fold(std::wstring(part))) != std::wstring::npos;
}

// Enabled in the image now (not what the queue makes of it): the reference tool's "Etkin" column.
bool onNow(const OptionalFeature& item) {
    return item.isOn();
}
} // namespace

std::wstring FeaturesPage::languageOf(const std::wstring& name) {
    if (!name.starts_with(L"Language.") || name.starts_with(L"Language.Fonts.")) {
        return {};
    }
    // Language.<Kind>~~~<tag>~<version>
    const auto tilde = name.find(L"~~~");
    if (tilde == std::wstring::npos) {
        return {};
    }
    const auto end = name.find(L'~', tilde + 3);
    return name.substr(tilde + 3, end == std::wstring::npos ? std::wstring::npos : end - tilde - 3);
}

std::wstring FeaturesPage::languageTitle(const std::wstring& tag) {
    wchar_t localized[LOCALE_NAME_MAX_LENGTH * 2] = {};
    wchar_t native[LOCALE_NAME_MAX_LENGTH * 2] = {};
    const bool known = GetLocaleInfoEx(tag.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, localized, LOCALE_NAME_MAX_LENGTH * 2) > 0;
    GetLocaleInfoEx(tag.c_str(), LOCALE_SNATIVEDISPLAYNAME, native, LOCALE_NAME_MAX_LENGTH * 2);
    if (!known) {
        return tag;
    }
    std::wstring title = localized;
    if (native[0] != 0 && _wcsicmp(native, localized) != 0) {
        title += L" - " + std::wstring(native);
    }
    return title + L" - " + tag;
}

FeaturesPage::Category FeaturesPage::categoryOf(const std::wstring& name) {
    // The reference tool sorts the capabilities by their id (research README §7.6).
    if (startsWithNoCase(name, L"Language.Fonts.")) {
        return Category::Fonts;
    }
    if (startsWithNoCase(name, L"Language.")) {
        return Category::Languages;
    }
    if (startsWithNoCase(name, L"Rsat.")) {
        return Category::Rsat;
    }
    if (startsWithNoCase(name, L"Microsoft.Windows.") && (containsNoCase(name, L"Wifi") || containsNoCase(name, L"Ethernet"))) {
        return Category::Drivers;
    }
    return Category::Other;
}

FeaturesPage::FeaturesPage(AppState& state, FeatureController& controller, const Localization& strings,
                           Language language, std::function<void()> goToImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_goToImages(std::move(goToImages)) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{L"", L""}, 0);
    m_tabs->onChange = [this](int index) {
        m_capabilities = index == 0;
        m_table->clearSelection();
        refilter();
        updateSummary();
        detailChanged();
    };
    m_filterBox = &add<ui::Dropdown>(L"",
                                     std::vector<std::wstring>{strings.get(Str::CommonAll), strings.get(Str::FeaturesOn),
                                                               strings.get(Str::FeaturesOff), strings.get(Str::FeaturesQueued)},
                                     0);
    m_filterBox->onChange = [this](int index) {
        m_filter = static_cast<Filter>(std::clamp(index, 0, 3));
        refilter();
    };
    m_search = &add<ui::SearchBox>(strings.get(Str::FeaturesFilterHint));
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::fold(text);
        refilter();
    };
    m_summary = &add<SummaryLine>();
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::FeaturesFeature), kNameColumn},
        {L"", marksColumnWidth(kMarkSlots)},
        {strings.get(Str::FeaturesColDescription), 0},
        {strings.get(Str::FeaturesColEnabled), kEnabledColumn, ui::TextAlign::Center},
        {strings.get(Str::FeaturesColInstalled), kSizeColumn, ui::TextAlign::Trailing},
    });
    m_table->setRowHeight(ui::tokens::size::tableRow);
    // D-139: the column heads are the table's own (the header row has the summary at its right).
    m_table->cellTooltip = [this](int row, int column) -> std::wstring {
        const auto* item = column == kMarks ? itemAt(row) : nullptr;
        if (!item) {
            return {};
        }
        const RowMarks marks = marksOf(row);
        const std::wstring reason = marks.mark == RowMark::Held && explainGuard ? explainGuard(*item)
                                    : marks.mark == RowMark::Locked            ? m_strings.get(statusName(m_controller.status(*item)))
                                                                               : std::wstring();
        return marksTooltip(m_strings, marks, m_strings.get(statusName(m_controller.status(*item))), reason);
    };
    m_table->onSelect = [this](int) { detailChanged(); };
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) { click(row, column, p); };
    m_table->onActivate = [this](int row) { toggleByUser(row); };
    m_table->tooltipFor = [this](int row) { return tooltipOf(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::PuzzleFeatures, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::FeaturesTitle));
    // The reference tool opens with the categories closed.
    for (int c = 0; c < static_cast<int>(kCategoryNames.size()); ++c) {
        m_collapsed.insert(c);
    }

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Features) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            updateCounts();
            if (m_filter != Filter::All) {
                refilter();
            } else {
                m_table->refresh();
                invalidate();
            }
            updateTabs();
            updateSummary();
            detailChanged();
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

bool FeaturesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
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

void FeaturesPage::setAllExpanded(bool expanded) {
    m_collapsed.clear();
    if (!expanded) {
        for (int c = 0; c < static_cast<int>(kCategoryNames.size()); ++c) {
            m_collapsed.insert(c);
        }
    }
    refilter();
}

void FeaturesPage::reveal(const std::wstring& name) {
    m_search->setText({});
    m_needle.clear();
    m_filterBox->setSelected(0);
    m_filter = Filter::All;
    const auto& features = m_state.optionalFeatures();
    if (!features) {
        return;
    }
    const auto found = std::ranges::find(features->items, name, &OptionalFeature::name);
    if (found == features->items.end()) {
        return;
    }
    m_capabilities = found->kind == OptionalFeature::Kind::Capability;
    m_tabs->setSelected(m_capabilities ? 0 : 1);
    if (m_capabilities) {
        m_collapsed.erase(static_cast<int>(categoryOf(found->name)));
    }
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

const OptionalFeature* FeaturesPage::itemAt(int row) const {
    const auto& features = m_state.optionalFeatures();
    if (!features || row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[static_cast<std::size_t>(row)].item < 0) {
        return nullptr;
    }
    return &features->items[static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)].item)];
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
        // D-139: a quiet line under the page's icon while the list is read (no spinner that never turns).
        showEmpty(ui::icons::Icon::PuzzleFeatures, Str::FeaturesLoadingTitle, m_strings.get(Str::FeaturesLoadingBody),
                  std::nullopt, {});
    } else if (features->status == AppState::OptionalFeatures::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::FeaturesFailedTitle,
                  features->error.message + L" — " + features->error.context, Str::CommonRetry,
                  [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_tabs, m_filterBox, m_search, m_summary, m_table}) {
        w->setVisible(ready);
    }
    updateCounts();
    updateTabs();
    refilter();
    layout();
    invalidate();
    detailChanged();
}

void FeaturesPage::updateTabs() {
    // The reference tool: "Talep Üzerine Özellikler 57/489" — what stays on of all there are.
    int capOn = 0, capAll = 0, featOn = 0, featAll = 0;
    if (const auto& features = m_state.optionalFeatures(); features && features->status == AppState::OptionalFeatures::Status::Ready) {
        for (const auto& item : features->items) {
            const bool on = m_controller.targetOn(item);
            if (item.kind == OptionalFeature::Kind::Capability) {
                ++capAll;
                capOn += on ? 1 : 0;
            } else {
                ++featAll;
                featOn += on ? 1 : 0;
            }
        }
    }
    m_tabs->setTabs({m_strings.format(Str::FeaturesTabCapabilities, {{L"on", std::to_wstring(capOn)}, {L"n", std::to_wstring(capAll)}}),
                     m_strings.format(Str::FeaturesTabFeatures, {{L"on", std::to_wstring(featOn)}, {L"n", std::to_wstring(featAll)}})});
    updateSummary();
}

void FeaturesPage::updateSummary() {
    // The shown tab's "9 / 10 açık" and what Uygula changes of both tabs ("· 2 değişecek").
    std::vector<SummaryPart> parts;
    if (const auto& features = m_state.optionalFeatures(); features && features->status == AppState::OptionalFeatures::Status::Ready) {
        std::size_t on = 0, all = 0, queued = 0;
        for (const auto& item : features->items) {
            queued += m_controller.queued(item) ? 1 : 0;
            if ((item.kind == OptionalFeature::Kind::Capability) == m_capabilities) {
                ++all;
                on += m_controller.targetOn(item) ? 1 : 0;
            }
        }
        parts.push_back({m_strings.format(Str::FeaturesSummary, {{L"on", formatCount(on, m_language)}, {L"n", formatCount(all, m_language)}})});
        if (queued > 0) {
            parts.push_back({m_strings.format(Str::FeaturesSummaryChange, {{L"n", formatCount(queued, m_language)}}), SummaryPart::Tone::Change});
        }
    }
    m_summary->set(std::move(parts));
}

bool FeaturesPage::visible(const OptionalFeature& item) const {
    const bool queued = m_controller.queued(item) != nullptr;
    const bool on = m_controller.targetOn(item);
    if ((m_filter == Filter::On && !on) || (m_filter == Filter::Off && on) || (m_filter == Filter::Queued && !queued)) {
        return false;
    }
    if (!m_needle.empty() && wl::text::fold(item.displayName).find(m_needle) == std::wstring::npos &&
        wl::text::fold(item.name).find(m_needle) == std::wstring::npos) {
        return false;
    }
    return true;
}

void FeaturesPage::refilter() {
    m_rows.clear();
    const auto& features = m_state.optionalFeatures();
    if (features && features->status == AppState::OptionalFeatures::Status::Ready) {
        const auto& items = features->items;
        std::vector<int> order(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            order[i] = static_cast<int>(i);
        }
        std::ranges::sort(order, [&](int a, int b) {
            return _wcsicmp(items[static_cast<std::size_t>(a)].displayName.c_str(), items[static_cast<std::size_t>(b)].displayName.c_str()) < 0;
        });
        const bool filtering = !m_needle.empty() || m_filter != Filter::All;
        if (m_capabilities) {
            for (int c = 0; c < static_cast<int>(kCategoryNames.size()); ++c) {
                std::vector<int> shown;
                for (const int i : order) {
                    const auto& item = items[static_cast<std::size_t>(i)];
                    if (item.kind == OptionalFeature::Kind::Capability && static_cast<int>(categoryOf(item.name)) == c && visible(item)) {
                        shown.push_back(i);
                    }
                }
                if (shown.empty()) {
                    continue;
                }
                m_rows.push_back({c, -1});
                if (!filtering && m_collapsed.contains(c)) {
                    continue;
                }
                if (c != static_cast<int>(Category::Languages)) {
                    for (const int i : shown) {
                        m_rows.push_back({-1, i, 1});
                    }
                    continue;
                }
                // D-114 (the reference tool): one group per language — typing, handwriting, OCR, speech … under it.
                std::map<std::wstring, std::vector<int>> byLanguage;
                std::vector<int> loose;
                for (const int i : shown) {
                    const std::wstring tag = languageOf(items[static_cast<std::size_t>(i)].name);
                    (tag.empty() ? loose : byLanguage[tag]).push_back(i);
                }
                std::vector<std::pair<std::wstring, std::wstring>> groups; // title, tag
                for (const auto& [tag, _] : byLanguage) {
                    groups.emplace_back(languageTitle(tag), tag);
                }
                std::ranges::sort(groups, [](const auto& a, const auto& b) { return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0; });
                for (const auto& [title, tag] : groups) {
                    m_rows.push_back({-1, -1, 1, tag, title});
                    if (filtering || m_openGroups.contains(tag)) {
                        for (const int i : byLanguage[tag]) {
                            m_rows.push_back({-1, i, 2});
                        }
                    }
                }
                for (const int i : loose) {
                    m_rows.push_back({-1, i, 1});
                }
            }
        } else {
            // D-114 (the reference tool): the features as the tree their manifests make (a parent before its children).
            std::map<std::wstring, std::vector<int>> children;
            std::set<std::wstring> names;
            for (const int i : order) {
                const auto& item = items[static_cast<std::size_t>(i)];
                if (item.kind == OptionalFeature::Kind::Feature && visible(item)) {
                    names.insert(item.name);
                }
            }
            for (const int i : order) {
                const auto& item = items[static_cast<std::size_t>(i)];
                if (item.kind == OptionalFeature::Kind::Feature && visible(item) && !item.parent.empty() && names.contains(item.parent)) {
                    children[item.parent].push_back(i);
                }
            }
            std::function<void(int, int)> emit = [&](int i, int depth) {
                m_rows.push_back({-1, i, depth});
                if (depth > 8) {
                    return;
                }
                if (const auto it = children.find(items[static_cast<std::size_t>(i)].name); it != children.end()) {
                    for (const int child : it->second) {
                        emit(child, depth + 1);
                    }
                }
            };
            for (const int i : order) {
                const auto& item = items[static_cast<std::size_t>(i)];
                if (item.kind == OptionalFeature::Kind::Feature && visible(item) && (item.parent.empty() || !names.contains(item.parent))) {
                    emit(i, 0);
                }
            }
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    invalidate();
}

void FeaturesPage::updateCounts() {
    m_countsAt = m_state.revision();
    m_categoryCounts = {};
    m_languageCounts.clear();
    const auto& features = m_state.optionalFeatures();
    if (!features || features->status != AppState::OptionalFeatures::Status::Ready) {
        return;
    }
    auto add = [](Counts& counts, const OptionalFeature& item, bool on) {
        ++counts.all;
        counts.on += on ? 1 : 0;
        counts.bytes += onNow(item) ? 0 : item.size; // what "Yüklendi" shows for the item
    };
    for (const auto& item : features->items) {
        if (item.kind != OptionalFeature::Kind::Capability) {
            continue;
        }
        const bool on = m_controller.targetOn(item);
        const auto category = static_cast<std::size_t>(categoryOf(item.name));
        if (category < m_categoryCounts.size()) {
            add(m_categoryCounts[category], item, on);
        }
        if (std::wstring tag = languageOf(item.name); !tag.empty()) {
            add(m_languageCounts[std::move(tag)], item, on);
        }
    }
    auto texts = [this](Counts& counts) {
        counts.count = treeCount(m_strings, static_cast<std::size_t>(counts.on), static_cast<std::size_t>(counts.all));
        counts.size = counts.bytes > 0 ? formatBytes(counts.bytes, m_language) : std::wstring();
    };
    for (auto& counts : m_categoryCounts) {
        texts(counts);
    }
    for (auto& [tag, counts] : m_languageCounts) {
        texts(counts);
    }
}

const FeaturesPage::Counts* FeaturesPage::countsOf(const Row& row) const {
    if (row.category >= 0) {
        const auto category = static_cast<std::size_t>(row.category);
        return category < m_categoryCounts.size() ? &m_categoryCounts[category] : nullptr;
    }
    if (!row.group.empty()) {
        const auto found = m_languageCounts.find(row.group);
        return found != m_languageCounts.end() ? &found->second : nullptr;
    }
    return nullptr;
}

std::wstring FeaturesPage::rowCountText(int row) const {
    const Counts* counts = row >= 0 && row < static_cast<int>(m_rows.size()) ? countsOf(m_rows[static_cast<std::size_t>(row)]) : nullptr;
    return counts ? counts->count : std::wstring();
}

std::wstring FeaturesPage::rowSizeText(int row) const {
    const Counts* counts = row >= 0 && row < static_cast<int>(m_rows.size()) ? countsOf(m_rows[static_cast<std::size_t>(row)]) : nullptr;
    return counts ? counts->size : std::wstring();
}

ui::RectF FeaturesPage::cellBox(int column) const {
    const RectF cell = m_table->cellRect(0, column);
    return {cell.x + ui::TableView::kCellPad, cell.y, std::max(cell.width - 2 * ui::TableView::kCellPad, 0.0f), cell.height};
}

void FeaturesPage::revealGroupCheck(int row) {
    // After a category's / language's box: the counts are new (the queue's notification), the row
    // is where it was unless a filter rebuilt the list.
    if (m_filter != Filter::All || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    if (const Counts* counts = countsOf(m_rows[static_cast<std::size_t>(row)]); counts && checkOf(*counts) != TreeCheck::Off) {
        m_table->revealCheck(row);
    }
}

void FeaturesPage::toggleByUser(int row) {
    const auto* item = itemAt(row);
    if (!item || !m_controller.canToggle(*item)) {
        return;
    }
    m_controller.toggle(*item);
    // The row the user ticked draws its tick in (a category's or a language's box, which ticks many
    // rows at once, stays instant). Under a filter the rows were just rebuilt: the index may have moved.
    if (m_filter == Filter::All && row < static_cast<int>(m_rows.size()) && itemAt(row) == item && m_controller.targetOn(*item)) {
        m_table->revealCheck(row);
    }
}

std::wstring FeaturesPage::tooltipOf(int row) const {
    const auto* item = itemAt(row);
    if (!item) {
        return {};
    }
    std::wstring tip = item->displayName + L"\n" + item->name;
    if (!item->description.empty()) {
        tip += L"\n" + item->description;
    }
    tip += L"\n" + m_strings.get(statusName(m_controller.status(*item)));
    return tip;
}

void FeaturesPage::click(int row, int column, ui::PointF p) {
    if (column != kName || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const RectF cell = m_table->cellRect(row, column);
    if (!r.group.empty()) {
        const float gx = cell.x + ui::TableView::kCellPad + kIndent;
        if (p.x < gx + kChevronW) {
            if (!m_openGroups.erase(r.group)) {
                m_openGroups.insert(r.group);
            }
            refilter();
            return;
        }
        // The group's circle: every capability of the language on or off together.
        if (const auto& features = m_state.optionalFeatures()) {
            bool allOn = true;
            for (const auto& item : features->items) {
                if (item.kind == OptionalFeature::Kind::Capability && languageOf(item.name) == r.group && m_controller.canToggle(item) && !m_controller.guarded(item) &&
                    !m_controller.targetOn(item)) {
                    allOn = false;
                }
            }
            {
                const auto batch = m_state.batchQueue(); // one notification, one undo step (audit PG-04)
                for (const auto& item : features->items) {
                    if (item.kind == OptionalFeature::Kind::Capability && languageOf(item.name) == r.group && m_controller.canToggle(item) &&
                        !m_controller.guarded(item) && m_controller.targetOn(item) == allOn) {
                        m_controller.toggle(item);
                    }
                }
            }
            revealGroupCheck(row); // the box clicked draws in; the rows it ticked stay instant
        }
        return;
    }
    const bool nested = m_capabilities;
    if (r.category >= 0) {
        if (p.x < cell.x + ui::TableView::kCellPad + kChevronW) {
            if (m_collapsed.contains(r.category)) {
                m_collapsed.erase(r.category);
            } else {
                m_collapsed.insert(r.category);
            }
            refilter();
            return;
        }
        // The category's circle: every item of it on or off together.
        const auto& features = m_state.optionalFeatures();
        if (!features) {
            return;
        }
        bool allOn = true;
        for (const auto& item : features->items) {
            if (item.kind == OptionalFeature::Kind::Capability && static_cast<int>(categoryOf(item.name)) == r.category &&
                m_controller.canToggle(item) && !m_controller.guarded(item) && !m_controller.targetOn(item)) {
                allOn = false;
            }
        }
        {
            const auto batch = m_state.batchQueue(); // one notification, one undo step (audit PG-04)
            for (const auto& item : features->items) {
                if (item.kind == OptionalFeature::Kind::Capability && static_cast<int>(categoryOf(item.name)) == r.category &&
                    m_controller.canToggle(item) && !m_controller.guarded(item) && m_controller.targetOn(item) == allOn) {
                    m_controller.toggle(item);
                }
            }
        }
        revealGroupCheck(row);
        return;
    }
    const float x0 = cell.x + ui::TableView::kCellPad + kIndent * static_cast<float>(r.depth) + kChevronW;
    (void)nested;
    if (p.x >= x0 - 4 && p.x < x0 + 16) {
        toggleByUser(row);
    }
}

void FeaturesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    Row& r = m_rows[static_cast<std::size_t>(row)];
    const float cy = rect.y + rect.height / 2;
    if (r.category >= 0 || !r.group.empty()) {
        // A category / a language: its circle from the counts (mixed when only some of it stays on),
        // "on/all" at the name cell's right and the not-installed size under "Yüklendi" (cached:
        // updateCounts()).
        static const Counts kNone{};
        const Counts* found = countsOf(r);
        const Counts& counts = found ? *found : kNone;
        if (column == kSize) {
            canvas.drawText(counts.size, rect, TypeStyle::Body, Color::TextTertiary, ui::TextAlign::Trailing);
            return;
        }
        if (column != kName) {
            return;
        }
        const float gx = r.category >= 0 ? rect.x : rect.x + kIndent;
        const bool open = !m_needle.empty() || m_filter != Filter::All ||
                          (r.category >= 0 ? !m_collapsed.contains(r.category) : m_openGroups.contains(r.group));
        canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {gx, cy - 6}, Color::TextTertiary,
                        ui::IconVariant::Regular16, 12.0f);
        paintTreeCheck(canvas, {gx + kChevronW + 6, cy}, checkOf(counts), std::nullopt, false, cell);
        const float x = gx + kTextX;
        const float right = rect.right() - (counts.count.empty() ? 0.0f : kTreeCountRoom + 8);
        const std::wstring& title = r.category >= 0 ? m_strings.get(kCategoryNames[static_cast<std::size_t>(r.category)]) : r.title;
        // D-139: a category reads as the head of what is under it (a language group stays plain).
        canvas.drawText(title, {x, rect.y, std::max(right - x, 0.0f), rect.height}, r.category >= 0 ? kGroupStyle : TypeStyle::Body,
                        Color::TextPrimary);
        paintTreeCount(canvas, rect, counts.count);
        return;
    }
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    switch (column) {
    case kName: {
        const float x0 = rect.x + kIndent * static_cast<float>(r.depth);
        syncRow(r, *item);
        const bool locked = r.locked;
        paintTreeCheck(canvas, {x0 + kChevronW + 6, cy}, r.check, std::nullopt, locked, cell);
        const float x = x0 + kTextX;
        canvas.drawText(item->displayName, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height}, TypeStyle::Body,
                        locked ? Color::TextTertiary : Color::TextPrimary);
        break;
    }
    case kMarks: {
        // Cached with the row's circle (the guard's question builds an operation: not per frame).
        syncRow(r, *item);
        RowMarks marks;
        marks.mark = r.mark;
        paintMarks(canvas, rect, marks, kMarkSlots);
        break;
    }
    case kDescription:
        canvas.drawText(item->description, rect, TypeStyle::Body, Color::TextTertiary);
        break;
    case kEnabled:
        if (onNow(*item)) {
            canvas.drawIcon(ui::icons::Icon::SuccessCircle, {rect.x + std::round((rect.width - 16) / 2), cy - 8}, Color::StatusSuccess);
        }
        break;
    case kSize:
        // The reference tool's "Yüklendi": the size of what is not installed yet.
        if (!onNow(*item) && item->size > 0) {
            canvas.drawText(formatBytes(item->size, m_language), rect, TypeStyle::Body, Color::TextSecondary, ui::TextAlign::Trailing);
        }
        break;
    default: break;
    }
}

void FeaturesPage::layout() {
    // D-139: the tabs, the header row ("Tümü ▾", the search box, the summary at the right), the table
    // with its own heads. Beside the detail card the description is the card's and the name takes the room.
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y, std::min(m_tabs->measure({}).width, b.width), kTabs});
    float y = b.y + kTabs + 6;
    m_filterBox->setBounds({b.x, y, kFilterWidth, kFilterRow});
    const float searchX = b.x + kFilterWidth + kGap;
    const float searchW = std::max(kNameColumn - ui::TableView::kCellPad - kFilterWidth - kGap, 200.0f);
    m_search->setWidth(searchW);
    m_search->setBounds({searchX, y, searchW, kFilterRow});
    const float summaryX = m_search->bounds().right() + kHeaderGap;
    m_summary->setBounds({summaryX, y, std::max(b.right() - summaryX, 0.0f), kFilterRow});
    y += kFilterRow + kHeaderGap;
    const bool compact = detailShown();
    m_table->setColumnHidden(kDescription, compact);
    m_table->setColumnWidth(kName, compact ? 0.0f : kNameColumn);
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
    // The heads where their cells are (tests: the "Etkin" head centred over its icons).
    m_headDescription = columnHead(*m_table, kDescription, y, ui::TableView::kHeader);
    m_headEnabled = columnHead(*m_table, kEnabled, y, ui::TableView::kHeader);
    m_headSize = columnHead(*m_table, kSize, y, ui::TableView::kHeader);
}

void FeaturesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF row{m_table->bounds().x, m_filterBox->bounds().y, m_table->bounds().width, kFilterRow};
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {row.x, m_table->bounds().y, row.width, 0}, m_strings.get(Str::FeaturesNoResults));
    }
}

} // namespace wl::app

namespace wl::app {

void FeaturesPage::syncRow(Row& r, const OptionalFeature& item) const {
    const std::uint64_t revision = m_state.revision();
    if (r.stateAt == revision) {
        return;
    }
    r.stateAt = revision;
    const bool guarded = m_controller.guarded(item);
    r.locked = !m_controller.canToggle(item) || guarded; // D-120: a guard holds it
    r.check = m_controller.targetOn(item) ? TreeCheck::On : TreeCheck::Off;
    r.mark = m_controller.queued(item)                                                           ? RowMark::Queued
             : guarded                                                                          ? RowMark::Held
             : !m_controller.canToggle(item) && m_controller.status(item) == Status::Permanent ? RowMark::Locked
                                                                                                : RowMark::None;
}

RowMarks FeaturesPage::marksOf(int row) const {
    const auto* item = itemAt(row);
    if (!item) {
        return {};
    }
    RowMarks marks;
    marks.mark = m_controller.queued(*item)                                                        ? RowMark::Queued
                 : m_controller.guarded(*item)                                                    ? RowMark::Held
                 : !m_controller.canToggle(*item) && m_controller.status(*item) == Status::Permanent ? RowMark::Locked
                                                                                                  : RowMark::None;
    return marks;
}

DetailModel FeaturesPage::detail() const {
    DetailModel model;
    const int row = m_table->selected();
    if (!m_table->visible() || row < 0 || row >= static_cast<int>(m_rows.size())) {
        return model;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.category >= 0 || !r.group.empty()) {
        // A category / a language: how much of it stays on, and what is not installed of it.
        const Counts* counts = countsOf(r);
        model.icon = r.group.empty() ? ui::icons::Icon::Folder : ui::icons::Icon::LanguageGlobe;
        model.title = r.category >= 0 ? m_strings.get(kCategoryNames[static_cast<std::size_t>(r.category)]) : r.title;
        if (counts) {
            model.facts.push_back({m_strings.get(Str::DetailInImage),
                                   m_strings.format(Str::FeaturesSummary, {{L"on", formatCount(static_cast<std::size_t>(counts->on), m_language)},
                                                                           {L"n", formatCount(static_cast<std::size_t>(counts->all), m_language)}})});
            if (!counts->size.empty()) {
                model.facts.push_back({m_strings.get(Str::FeaturesColInstalled), counts->size, DetailTone::Normal, true});
            }
        }
        return model;
    }
    const auto* item = itemAt(row);
    if (!item) {
        return model;
    }
    const bool capability = item->kind == OptionalFeature::Kind::Capability;
    const Status status = m_controller.status(*item);
    const bool queued = m_controller.queued(*item) != nullptr;
    const bool guarded = m_controller.guarded(*item);
    model.icon = ui::icons::Icon::PuzzleFeatures;
    model.title = item->displayName;
    model.subtitle = capability ? m_strings.get(kCategoryNames[static_cast<std::size_t>(categoryOf(item->name))])
                                : m_strings.get(Str::FeaturesCardFeature);
    model.identity = item->name;
    if (queued) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(statusName(status)), DetailTone::Change};
    } else if (guarded) {
        model.state = {ui::icons::Icon::ShieldCheck, m_strings.get(Str::MarksHeld), DetailTone::Quiet};
    } else if (status == Status::Permanent) {
        model.state = {ui::icons::Icon::Lock, m_strings.get(statusName(status)), DetailTone::Quiet};
    } else {
        model.state = {ui::icons::Icon::Check, m_strings.get(Str::DetailUnchanged), DetailTone::Quiet};
    }
    model.about = item->description;
    model.facts.push_back({m_strings.get(Str::DetailInImage),
                           onNow(*item) ? m_strings.get(capability ? Str::FeaturesInstalled : Str::FeaturesEnabled)
                                        : m_strings.get(capability ? Str::FeaturesNotInstalled : Str::FeaturesDisabled)});
    model.facts.push_back({m_strings.get(Str::DetailAtApply), m_strings.get(queued ? statusName(status) : Str::DetailUnchanged),
                           queued ? DetailTone::Change : DetailTone::Normal});
    model.facts.push_back({m_strings.get(Str::CommonType), m_strings.get(capability ? Str::FeaturesCardCapability : Str::FeaturesCardFeature)});
    if (item->size > 0) {
        model.facts.push_back({m_strings.get(Str::CommonSize), formatBytes(item->size, m_language), DetailTone::Normal, true});
    }
    if (!item->parent.empty()) {
        model.facts.push_back({m_strings.get(Str::FeaturesParent), item->parent, DetailTone::Normal, true});
    }
    if (item->restartRequired) {
        model.facts.push_back({m_strings.get(Str::FeaturesRestart), m_strings.get(Str::FeaturesRestartYes)});
    }
    if (guarded && explainGuard) {
        model.notes.push_back({ui::icons::Icon::ShieldCheck, explainGuard(*item) + L"\n" + m_strings.get(Str::CompatLockedHint)});
    }
    if (m_controller.canToggle(*item)) {
        const Str label = queued                  ? Str::FeaturesUndoChange
                          : !m_controller.targetOn(*item) ? Str::FeaturesTurnOn
                          : capability              ? Str::FeaturesRemoveCap
                                                    : Str::FeaturesTurnOff;
        model.actions.push_back({kActionToggle, m_strings.get(label), !guarded});
    }
    return model;
}

void FeaturesPage::detailAction(int id) {
    if (id == kActionToggle) {
        toggleByUser(m_table->selected()); // the row's own Space / double click
    }
}

} // namespace wl::app
