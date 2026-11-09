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
constexpr float kIndent = 17.0f;
constexpr float kChevronW = 14.0f;
constexpr float kTextX = 30.0f;
constexpr float kEnabledColumn = 70.0f;
constexpr float kSizeColumn = 90.0f;

enum Column : int { kName, kDescription, kEnabled, kSize };

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
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::FeaturesFeature), 300},
        {strings.get(Str::FeaturesColDescription), 0},
        {strings.get(Str::FeaturesColEnabled), kEnabledColumn, ui::TextAlign::Center},
        {strings.get(Str::FeaturesColInstalled), kSizeColumn, ui::TextAlign::Trailing},
    });
    m_table->setRowHeight(ui::tokens::size::tableRow);
    m_table->setHeaderHeight(0); // the heads are in the filter row
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) { click(row, column, p); };
    m_table->onActivate = [this](int row) {
        if (const auto* item = itemAt(row); item && m_controller.canToggle(*item)) {
            m_controller.toggle(*item);
        }
    };
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
            if (m_filter != Filter::All) {
                refilter();
            } else {
                m_table->refresh();
                invalidate();
            }
            updateTabs();
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
        showEmpty(ui::icons::Icon::Spinner, Str::FeaturesLoadingTitle, m_strings.get(Str::FeaturesLoadingBody),
                  std::nullopt, {});
    } else if (features->status == AppState::OptionalFeatures::Status::Failed) {
        showEmpty(ui::icons::Icon::ErrorOctagon, Str::FeaturesFailedTitle,
                  features->error.message + L" — " + features->error.context, Str::CommonRetry,
                  [this] { m_controller.load(/*force=*/true); });
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_tabs, m_filterBox, m_search, m_table}) {
        w->setVisible(ready);
    }
    updateTabs();
    refilter();
    layout();
    invalidate();
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
                    m_rows.push_back({-1, -1, 1, tag});
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
            for (const auto& item : features->items) {
                if (item.kind == OptionalFeature::Kind::Capability && languageOf(item.name) == r.group && m_controller.canToggle(item) && !m_controller.guarded(item) &&
                    m_controller.targetOn(item) == allOn) {
                    m_controller.toggle(item);
                }
            }
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
        for (const auto& item : features->items) {
            if (item.kind == OptionalFeature::Kind::Capability && static_cast<int>(categoryOf(item.name)) == r.category &&
                m_controller.canToggle(item) && !m_controller.guarded(item) && m_controller.targetOn(item) == allOn) {
                m_controller.toggle(item);
            }
        }
        return;
    }
    const float x0 = cell.x + ui::TableView::kCellPad + kIndent * static_cast<float>(r.depth) + kChevronW;
    (void)nested;
    if (p.x >= x0 - 4 && p.x < x0 + 16) {
        if (const auto* item = itemAt(row); item && m_controller.canToggle(*item)) {
            m_controller.toggle(*item);
        }
    }
}

void FeaturesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const Row& r = m_rows[static_cast<std::size_t>(row)];
    const float cy = rect.y + rect.height / 2;
    if (r.category >= 0) {
        if (column != kName) {
            return;
        }
        const bool open = !m_needle.empty() || m_filter != Filter::All || !m_collapsed.contains(r.category);
        canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {rect.x, cy - 6},
                        Color::TextTertiary, ui::IconVariant::Regular16, 12.0f);
        // Mixed when only some of it stays on.
        int on = 0, all = 0;
        if (const auto& features = m_state.optionalFeatures()) {
            for (const auto& item : features->items) {
                if (item.kind == OptionalFeature::Kind::Capability && static_cast<int>(categoryOf(item.name)) == r.category) {
                    ++all;
                    on += m_controller.targetOn(item) ? 1 : 0;
                }
            }
        }
        paintTreeCheck(canvas, {rect.x + kChevronW + 6, cy}, on == 0 ? TreeCheck::Off : on == all ? TreeCheck::On : TreeCheck::Mixed,
                       std::nullopt, false, cell);
        const float x = rect.x + kTextX;
        canvas.drawText(m_strings.get(kCategoryNames[static_cast<std::size_t>(r.category)]), {x, rect.y, rect.right() - x, rect.height},
                        TypeStyle::Body, Color::TextPrimary);
        return;
    }
    if (!r.group.empty()) {
        if (column != kName) {
            return;
        }
        const float gx = rect.x + kIndent;
        const bool open = !m_needle.empty() || m_filter != Filter::All || m_openGroups.contains(r.group);
        canvas.drawIcon(open ? ui::icons::Icon::ChevronDown : ui::icons::Icon::ChevronRight, {gx, cy - 6}, Color::TextTertiary,
                        ui::IconVariant::Regular16, 12.0f);
        int on = 0, all = 0;
        if (const auto& features = m_state.optionalFeatures()) {
            for (const auto& item : features->items) {
                if (item.kind == OptionalFeature::Kind::Capability && languageOf(item.name) == r.group) {
                    ++all;
                    on += m_controller.targetOn(item) ? 1 : 0;
                }
            }
        }
        paintTreeCheck(canvas, {gx + kChevronW + 6, cy}, on == 0 ? TreeCheck::Off : on == all ? TreeCheck::On : TreeCheck::Mixed,
                       std::nullopt, false, cell);
        const float x = gx + kTextX;
        canvas.drawText(languageTitle(r.group), {x, rect.y, rect.right() - x, rect.height}, TypeStyle::Body, Color::TextPrimary);
        return;
    }
    const auto* item = itemAt(row);
    if (!item) {
        return;
    }
    switch (column) {
    case kName: {
        const float x0 = rect.x + kIndent * static_cast<float>(r.depth);
        const bool locked = !m_controller.canToggle(*item) || m_controller.guarded(*item); // D-120: a guard holds it
        paintTreeCheck(canvas, {x0 + kChevronW + 6, cy}, m_controller.targetOn(*item) ? TreeCheck::On : TreeCheck::Off, std::nullopt,
                       locked, cell);
        const float x = x0 + kTextX;
        canvas.drawText(item->displayName, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height}, TypeStyle::Body,
                        locked ? Color::TextTertiary : Color::TextPrimary);
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
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y, std::min(m_tabs->measure({}).width, b.width), kTabs});
    float y = b.y + kTabs + 6;
    m_filterBox->setBounds({b.x, y, kFilterWidth, kFilterRow});
    const float searchX = b.x + kFilterWidth + kGap;
    const float searchW = std::max(300.0f - kFilterWidth - kGap, 160.0f);
    m_search->setWidth(searchW);
    m_search->setBounds({searchX, y, searchW, kFilterRow});
    y += kFilterRow + 6;
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
}

void FeaturesPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    // The column heads in the filter row (the reference tool: "Açıklama · Etkin · Yüklendi").
    const RectF row{m_table->bounds().x, m_filterBox->bounds().y, m_table->bounds().width, kFilterRow};
    const float right = row.right() - ui::TableView::kCellPad;
    canvas.drawText(m_strings.get(Str::FeaturesColInstalled), {right - kSizeColumn, row.y, kSizeColumn, row.height}, TypeStyle::Body,
                    Color::TextTertiary, ui::TextAlign::Trailing);
    canvas.drawText(m_strings.get(Str::FeaturesColEnabled), {right - kSizeColumn - kEnabledColumn, row.y, kEnabledColumn, row.height},
                    TypeStyle::Body, Color::TextTertiary, ui::TextAlign::Center);
    const float descX = row.x + 300 + ui::TableView::kCellPad;
    if (m_search->bounds().right() + 16 < descX) {
        canvas.drawText(m_strings.get(Str::FeaturesColDescription), {descX, row.y, 200, row.height}, TypeStyle::Body, Color::TextTertiary);
    }
    if (m_rows.empty()) {
        paintTableEmpty(canvas, {row.x, m_table->bounds().y, row.width, 0}, m_strings.get(Str::FeaturesNoResults));
    }
}

} // namespace wl::app
