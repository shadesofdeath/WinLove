#include "app/pages/ProgramsPage.h"

#include "app/pages/PageBits.h"
#include "app/pages/programs/ProgramInspector.h"
#include "base/Text.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 0.0f;
constexpr float kToolbar = ui::tokens::size::control;
constexpr float kGap = 10.0f;
constexpr float kInfoBar = ui::InfoBar::kHeight;
constexpr float kIcon = 16.0f;
constexpr float kProgramMin = 220.0f; // the name column's least at a narrow window (D-136)
constexpr std::size_t kSearchLimit = 200;

enum Column : int { kName, kId, kVersion };

ui::icons::Icon bundleIcon(std::string_view id) {
    using ui::icons::Icon;
    if (id == "essentials") return Icon::SuccessCircle;
    if (id == "gamer") return Icon::Play;
    if (id == "developer") return Icon::LogTerminal;
    if (id == "office") return Icon::File;
    if (id == "creator") return Icon::Edit;
    if (id == "privacy") return Icon::Lock;
    return Icon::Download;
}
} // namespace

ProgramsPage::ProgramsPage(AppState& state, ProgramsController& controller, const Localization& strings, Language language,
                           std::function<void()> goToImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_goToImages(std::move(goToImages)) {
    m_search = &add<ui::SearchBox>(strings.get(Str::ProgramsSearchPlain), std::vector<std::wstring>{L"/"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = std::wstring(wl::text::trim(text));
        rebuildRows();
    };
    m_category = &add<ui::Dropdown>(strings.get(Str::ProgramsCategory), std::vector<std::wstring>{strings.get(Str::CommonAll)}, 0);
    m_category->onChange = [this](int index) {
        m_categoryFilter = index;
        rebuildRows();
    };
    m_pickedOnly = &add<ui::Toggle>(strings.get(Str::ProgramsOnlySelected), false);
    m_pickedOnly->onChange = [this](bool on) {
        m_onlyPicked = on;
        rebuildRows();
    };
    m_summaryLine = &add<SummaryLine>();
    m_bundleStrip = &add<BundleStrip>();
    m_bundleStrip->onPick = [this](int card) {
        if (card >= 0 && card < static_cast<int>(m_bundles.size())) {
            m_controller.toggleBundle(m_bundles[static_cast<std::size_t>(card)]);
        }
    };
    m_bar = &add<ui::InfoBar>(ui::InfoKind::Info, L"", L"", strings.get(Str::CommonClose));
    m_bar->onClose = [this] {
        m_bar->setVisible(false);
        layout();
    };
    m_bar->setVisible(false);
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::ProgramsColProgram), 0},
        {.title = strings.get(Str::ProgramsColId), .width = 276, .minWidth = 180},
        {strings.get(Str::ProgramsColVersion), 148, ui::TextAlign::Trailing, false, 110},
    });
    m_table->setFlexMin(kProgramMin); // D-136: the program's name keeps its room beside the side card
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) { click(row, column, p); };
    m_table->onActivate = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[static_cast<std::size_t>(row)].group) {
            return;
        }
        const Row& r = m_rows[static_cast<std::size_t>(row)];
        if (r.more >= 0) {
            openCategory(r.more);
        } else {
            togglePick(row);
        }
    };
    m_table->onSelect = [this](int) {
        selectionChanged();
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Download, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::ProgramsTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Programs) {
            // The index arrived (rows) or a program's details / icon did (repaint).
            if (m_categories.empty() && m_controller.status() == ProgramsController::Status::Ready) {
                refresh();
            } else if (m_controller.status() != ProgramsController::Status::Ready) {
                refresh();
            } else {
                m_table->refresh();
            }
            updateSummary();
            selectionChanged();
        } else if (change == AppState::Change::Queue) {
            // The picks group follows the queue: "only selected", and the start view, which lists
            // the picks the catalog lacks. A category or the whole repository does not change
            // with a pick (audit PG-06: each click built the 15,000 rows of the repository again).
            if (m_onlyPicked || (!searching() && m_categoryFilter == 0)) {
                rebuildRows();
            }
            updateBar();
            updateBundles();
            m_table->refresh();
            invalidate();
            updateSummary();
            selectionChanged();
        }
    });
    m_controller.load();
    refresh();
}

ProgramsPage::~ProgramsPage() {
    m_state.unsubscribe(m_subscription);
}

void ProgramsPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

bool ProgramsPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

std::optional<core::WingetPackage> ProgramsPage::selectedProgram() const {
    const int row = m_table->selected();
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[static_cast<std::size_t>(row)].group ||
        m_rows[static_cast<std::size_t>(row)].more >= 0) {
        return std::nullopt;
    }
    return m_rows[static_cast<std::size_t>(row)].package;
}

void ProgramsPage::refresh() {
    using Status = ProgramsController::Status;
    const Status status = m_controller.status();
    const bool ready = m_state.mounted() && status == Status::Ready;
    if (!m_state.mounted()) {
        // The page's own icon, as every "Bağlı imaj yok" has (2026-11-10).
        m_empty->setContent(ui::icons::Icon::Programs, m_strings.get(Str::ProgramsNoMountTitle), m_strings.get(Str::ProgramsNoMountBody));
        m_empty->setAction(m_strings.get(Str::CommonGoImages)).onInvoke = m_goToImages;
        m_empty->setVisible(true);
    } else if (status == Status::Failed) {
        m_empty->setContent(ui::icons::Icon::ErrorOctagon, m_strings.get(Str::ProgramsFailedTitle),
                            m_controller.error().message + L" — " + m_controller.error().context);
        m_empty->setAction(m_strings.get(Str::CommonRetry)).onInvoke = [this] { m_controller.load(/*refresh=*/true); };
        m_empty->setVisible(true);
    } else if (status != Status::Ready) {
        // D-139: a quiet line under the page's icon while the index is read (no spinner that never turns).
        m_empty->setContent(ui::icons::Icon::Programs, m_strings.get(Str::ProgramsLoadingTitle), m_strings.get(Str::ProgramsLoadingBody));
        m_empty->hideAction();
        m_empty->setVisible(true);
    } else {
        m_empty->setVisible(false);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_category, m_pickedOnly, m_summaryLine, m_bundleStrip, m_table}) {
        w->setVisible(ready);
    }
    if (ready) {
        m_search->setPlaceholder(m_strings.format(Str::ProgramsSearch, {{L"n", std::to_wstring(m_controller.packageCount())}}));
        m_categories = m_controller.categories();
        std::vector<std::wstring> names{m_strings.get(Str::CommonAll)};
        for (const auto& c : m_categories) {
            names.push_back(c.name);
        }
        names.push_back(m_strings.get(Str::ProgramsEverything));
        m_categoryFilter = std::clamp(m_categoryFilter, 0, everythingEntry());
        m_category->setItems(std::move(names), m_categoryFilter);
        m_bundles = m_controller.bundles();
        updateBundles();
    }
    rebuildRows();
    updateBar();
    layout();
    invalidate();
}

void ProgramsPage::rebuildRows() {
    std::wstring selectedId;
    if (const auto selected = selectedProgram()) {
        selectedId = selected->id;
    }
    m_rows.clear();
    auto group = [&](std::wstring title, int count) { m_rows.push_back({true, std::move(title), count, {}, -1}); };
    auto program = [&](core::WingetPackage package) { m_rows.push_back({false, {}, 0, std::move(package), -1}); };
    // A pick as the index has it (else as it was picked).
    auto pickPackage = [&](const core::PostSetupProgram& pick) {
        if (auto found = m_controller.package(pick.id)) {
            return std::move(*found);
        }
        return core::WingetPackage{pick.id, pick.name, {}, {}};
    };
    if (m_controller.status() == ProgramsController::Status::Ready) {
        if (searching()) {
            auto found = m_controller.search(m_needle, kSearchLimit);
            if (m_onlyPicked) {
                std::erase_if(found, [&](const core::WingetPackage& p) { return !m_controller.picked(p.id); });
            }
            group(m_strings.get(Str::ProgramsSearchGroup), static_cast<int>(found.size()));
            for (auto& p : found) {
                program(std::move(p));
            }
        } else if (m_onlyPicked) {
            group(m_strings.get(Str::ProgramsPicksGroup), static_cast<int>(m_controller.pickCount()));
            for (const auto& pick : m_controller.picks()) {
                program(pickPackage(pick));
            }
        } else if (m_categoryFilter == everythingEntry()) {
            auto all = m_controller.everything();
            group(m_strings.get(Str::ProgramsEverything), static_cast<int>(all.size()));
            for (auto& p : all) {
                program(std::move(p));
            }
        } else if (m_categoryFilter > 0) {
            // One category: its featured programs, then every package winget tags as it.
            const auto c = static_cast<std::size_t>(m_categoryFilter - 1);
            if (!m_categories[c].programs.empty()) {
                group(m_strings.get(Str::ProgramsFeatured), static_cast<int>(m_categories[c].programs.size()));
                for (const auto& p : m_categories[c].programs) {
                    program(p);
                }
            }
            auto tagged = m_controller.moreIn(c);
            if (!tagged.empty()) {
                group(m_strings.get(Str::ProgramsTagged), static_cast<int>(tagged.size()));
                for (auto& p : tagged) {
                    program(std::move(p));
                }
            }
        } else {
            for (std::size_t c = 0; c < m_categories.size(); ++c) {
                const auto& category = m_categories[c];
                if (category.programs.empty() && category.more == 0) {
                    continue;
                }
                group(category.name, static_cast<int>(category.programs.size() + category.more));
                for (const auto& p : category.programs) {
                    program(p);
                }
                if (category.more > 0) {
                    m_rows.push_back({false, {}, static_cast<int>(category.more), {}, static_cast<int>(c)});
                }
            }
            // Picks from searches (not in the catalog) at the end, so they are seen too.
            std::vector<core::WingetPackage> others;
            for (const auto& pick : m_controller.picks()) {
                const bool listed = std::ranges::any_of(m_categories, [&](const ProgramsController::Category& c) {
                    return std::ranges::any_of(c.programs, [&](const core::WingetPackage& p) { return text::iequals(p.id, pick.id); });
                });
                if (!listed) {
                    others.push_back(pickPackage(pick));
                }
            }
            if (!others.empty() && m_categoryFilter == 0) {
                group(m_strings.get(Str::ProgramsPicksGroup), static_cast<int>(others.size()));
                for (auto& p : others) {
                    program(std::move(p));
                }
            }
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    int found = -1;
    for (std::size_t i = 0; i < m_rows.size() && !selectedId.empty(); ++i) {
        if (!m_rows[i].group && m_rows[i].more < 0 && text::iequals(m_rows[i].package.id, selectedId)) {
            found = static_cast<int>(i);
            break;
        }
    }
    if (found >= 0) {
        m_table->setSelected(found, /*reveal=*/false);
    } else {
        m_table->clearSelection();
    }
    invalidate();
    updateSummary();
    selectionChanged();
}

void ProgramsPage::selectionChanged() {
    if (onSelectionChanged) {
        onSelectionChanged();
    }
    detailChanged();
}

void ProgramsPage::updateSummary() {
    // D-139: what a search found (quiet), what is picked (in the accent) — as every page's header row.
    std::vector<SummaryPart> parts;
    if (searching()) {
        const auto n = static_cast<std::size_t>(std::ranges::count_if(m_rows, [](const Row& r) { return !r.group && r.more < 0; }));
        parts.push_back({n == 0 ? m_strings.get(Str::ProgramsNoResults) : m_strings.format(Str::ProgramsResults, {{L"n", std::to_wstring(n)}})});
    }
    if (m_controller.pickCount() > 0) {
        parts.push_back({m_strings.format(Str::ProgramsPickedN, {{L"n", std::to_wstring(m_controller.pickCount())}}), SummaryPart::Tone::Change});
    } else if (!searching()) {
        parts.push_back({m_strings.get(Str::ProgramsNoneQueued)});
    }
    m_summaryLine->set(std::move(parts));
}

void ProgramsPage::updateBar() {
    // Only the warning that stops everything: a note that came and went with the first pick moved
    // the list under the pointer (the next click hit another row).
    const bool before = m_bar->visible();
    if (m_controller.wingetRemoved() && m_controller.pickCount() > 0) {
        m_bar->set(ui::InfoKind::Error, m_strings.get(Str::ProgramsWingetRemovedTitle), m_strings.get(Str::ProgramsWingetRemovedBody));
        m_bar->setVisible(true);
    } else {
        m_bar->setVisible(false);
    }
    if (before != m_bar->visible()) {
        layout();
    }
}

void ProgramsPage::updateBundles() {
    std::vector<BundleStrip::Card> cards;
    for (const auto& bundle : m_bundles) {
        const auto n = std::to_wstring(bundle.programs.size());
        const auto state = m_controller.bundleState(bundle);
        const auto picked = std::ranges::count_if(bundle.programs, [&](const core::WingetPackage& p) { return m_controller.picked(p.id); });
        BundleStrip::Card card;
        card.icon = bundleIcon(bundle.id);
        card.title = bundle.name;
        switch (state) {
        case ProgramsController::BundleState::None:
            card.caption = m_strings.format(Str::ProgramsBundlePrograms, {{L"n", n}});
            card.state = BundleStrip::State::None;
            break;
        case ProgramsController::BundleState::Some:
            card.caption = m_strings.format(Str::ProgramsBundleSome, {{L"k", std::to_wstring(picked)}, {L"n", n}});
            card.state = BundleStrip::State::Some;
            break;
        case ProgramsController::BundleState::All:
            card.caption = m_strings.get(Str::ProgramsBundleAll);
            card.state = BundleStrip::State::All;
            break;
        }
        card.description = bundle.description; // one line: the tooltip has no room for the program names
        cards.push_back(std::move(card));
    }
    m_bundleStrip->setCards(std::move(cards));
}

void ProgramsPage::openCategory(int category) {
    m_categoryFilter = category < 0 ? 0 : category + 1;
    m_category->setSelected(m_categoryFilter);
    rebuildRows();
    m_table->scrollToTop();
}

void ProgramsPage::click(int row, int column, ui::PointF p) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[static_cast<std::size_t>(row)].group) {
        return;
    }
    if (const int more = m_rows[static_cast<std::size_t>(row)].more; more >= 0) {
        openCategory(more);
        return;
    }
    if (column != kName) {
        return;
    }
    const RectF cell = m_table->cellRect(row, column);
    const float boxX = cell.x + ui::TableView::kCellPad;
    if (p.x >= boxX - 4 && p.x < boxX + ui::Checkbox::kBox + 6) {
        togglePick(row);
    }
}

void ProgramsPage::togglePick(int row) {
    const std::wstring id = m_rows[static_cast<std::size_t>(row)].package.id;
    m_controller.toggle(m_rows[static_cast<std::size_t>(row)].package);
    // The user's tick draws in - when the row is still that program (the picks group above the
    // start view is rebuilt with the queue and may have moved it).
    if (row < static_cast<int>(m_rows.size()) && m_rows[static_cast<std::size_t>(row)].more < 0 &&
        !m_rows[static_cast<std::size_t>(row)].group && m_rows[static_cast<std::size_t>(row)].package.id == id &&
        m_controller.picked(id)) {
        m_table->revealCheck(row);
    }
}

void ProgramsPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    Row& r = m_rows[static_cast<std::size_t>(row)];
    if (r.more >= 0) {
        if (column == kName) {
            const float x = rect.x + ui::Checkbox::kBox + 8;
            canvas.drawIcon(ui::icons::Icon::ArrowRight, {x, rect.y + (rect.height - 16) / 2},
                            cell.hoveredCell ? Color::AccentHover : Color::AccentBase);
            canvas.drawText(m_strings.format(Str::ProgramsMoreRow, {{L"n", std::to_wstring(r.count)}}),
                            {x + kIcon + 8, rect.y, rect.right() - x - kIcon - 8, rect.height}, TypeStyle::Body,
                            cell.hoveredCell ? Color::TextPrimary : Color::AccentText);
        }
        return;
    }
    if (r.group) {
        if (column == kName) {
            canvas.drawText(r.title, rect, kGroupStyle, Color::TextPrimary); // D-139: a group row is a head
        } else if (column == kId) {
            canvas.drawText(m_strings.format(Str::ComponentsItemsN, {{L"n", std::to_wstring(r.count)}}), rect, TypeStyle::Caption,
                            Color::TextTertiary);
        }
        return;
    }
    const auto& package = r.package;
    switch (column) {
    case kName: {
        float x = rect.x;
        if (const std::uint64_t revision = m_state.revision(); r.pickedAt != revision) {
            r.pickedAt = revision;
            r.picked = m_controller.picked(package.id);
        }
        ui::Checkbox::paintBox(canvas, {x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                               r.picked ? ui::CheckState::On : ui::CheckState::Off, cell.hoveredCell, cell.checkReveal);
        x += ui::Checkbox::kBox + 8;
        const RectF iconBox{x, rect.y + (rect.height - kIcon) / 2, kIcon, kIcon};
        // Rows on screen ask for their icon: fetched one at a time, newest first, cached on disk.
        const auto icon = m_controller.icon(package.id);
        if (icon.empty() || !canvas.drawFileIcon(icon.wstring(), 0, iconBox)) {
            ProgramInspector::paintLetter(canvas, iconBox, package.name, /*large=*/false);
        }
        x += kIcon + 8;
        if (searching()) {
            if (!r.matchKnown) {
                r.matchKnown = true;
                r.matchAt = wl::text::fold(package.name).find(wl::text::fold(m_needle));
            }
            const auto at = r.matchAt;
            if (at != std::wstring::npos) {
                const float x0 = x + canvas.text().measure(std::wstring_view(package.name).substr(0, at), TypeStyle::Body);
                const float x1 = x + canvas.text().measure(std::wstring_view(package.name).substr(0, at + m_needle.size()), TypeStyle::Body);
                canvas.fillRect({x0, rect.y + 4, x1 - x0, rect.height - 8}, Color::AccentSubtle);
            }
        }
        canvas.drawText(package.name, {x, rect.y, rect.right() - x, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        break;
    }
    case kId: canvas.drawText(package.id, rect, TypeStyle::Mono, Color::TextTertiary); break;
    case kVersion:
        canvas.drawText(package.version.empty() ? std::wstring(L"—") : package.version, rect, TypeStyle::Mono, Color::TextSecondary,
                        ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void ProgramsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(detailShown() ? 180.0f : 280.0f);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_category, m_pickedOnly}) {
        const ui::SizeF size = w->measure({});
        const float width = w == m_category ? std::min(size.width, detailShown() ? 160.0f : 200.0f) : size.width;
        w->setBounds({x, y, width, kToolbar});
        x += width + kGap;
    }
    m_summaryLine->setBounds({x + kHeaderGap - kGap, y, std::max(b.right() - x - kHeaderGap + kGap, 0.0f), kToolbar});
    y += kToolbar + 12;
    if (m_bundleStrip->visible()) {
        const float height = m_bundleStrip->heightFor(b.width);
        m_bundleStrip->setBounds({b.x, y, b.width, height});
        y += height + 12;
    }
    if (m_bar->visible()) {
        m_bar->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + 8;
    }
    m_table->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
    fitColumns(*m_table, b.width, detailShown(), {kId}, kProgramMin);
}

void ProgramsPage::paint(ui::Canvas&) {}

DetailModel ProgramsPage::detail() const {
    // D-139: what ProgramInspector showed, as the shared card: icon, name, publisher, id; what the
    // manifest says about it; version, licence, installer, architectures; its tags; pick / homepage.
    DetailModel model;
    const auto program = m_table->visible() ? selectedProgram() : std::nullopt;
    if (!program) {
        return model;
    }
    const core::WingetDetails* d = m_controller.details(program->id); // asks for them once (Change::Programs when they come)
    const bool picked = m_controller.picked(program->id);
    model.iconFile = m_controller.icon(program->id).wstring();
    model.letter = true;
    model.title = d && !d->name.empty() ? d->name : program->name;
    model.subtitle = d && !d->publisher.empty() ? d->publisher : program->id.substr(0, program->id.find(L'.'));
    model.identity = program->id;
    if (picked) {
        model.state = {ui::icons::Icon::QueueClock, m_strings.get(Str::ProgramsWillInstall), DetailTone::Change};
    }
    if (!d) {
        model.notes.push_back({m_controller.detailsFailed(program->id) ? ui::icons::Icon::ErrorOctagon : ui::icons::Icon::InfoCircle,
                               m_strings.get(m_controller.detailsFailed(program->id) ? Str::ProgramsDetailsFailed : Str::ProgramsDetailsLoading),
                               m_controller.detailsFailed(program->id) ? DetailTone::Error : DetailTone::Quiet});
    } else {
        model.about = !d->shortDescription.empty() ? d->shortDescription : d->description;
    }
    model.facts.push_back({m_strings.get(Str::ProgramsVersion), d && !d->version.empty() ? d->version : program->version, DetailTone::Normal, true});
    if (d) {
        auto joined = [](const std::vector<std::wstring>& parts, std::wstring_view separator) {
            std::wstring out;
            for (const auto& part : parts) {
                out += (out.empty() ? L"" : std::wstring(separator)) + part;
            }
            return out;
        };
        if (!d->license.empty()) {
            model.facts.push_back({m_strings.get(Str::ProgramsLicense), d->license});
        }
        std::vector<std::wstring> scopes;
        for (const auto& scope : d->scopes) {
            scopes.push_back(text::iequals(scope, L"user") ? m_strings.get(Str::ProgramsScopeUser) : m_strings.get(Str::ProgramsScopeMachine));
        }
        std::wstring installer = joined(d->installerTypes, L", ");
        if (!scopes.empty()) {
            installer += (installer.empty() ? L"" : L" · ") + joined(scopes, L", ");
        }
        if (!installer.empty()) {
            model.facts.push_back({m_strings.get(Str::ProgramsInstaller), installer});
        }
        if (!d->architectures.empty()) {
            model.facts.push_back({m_strings.get(Str::ProgramsArchitecture), joined(d->architectures, L", "), DetailTone::Normal, true});
        }
        if (!d->tags.empty()) {
            model.lists.push_back({m_strings.get(Str::ProgramsTags), d->tags});
        }
    }
    model.actions.push_back({kActionToggle, m_strings.get(picked ? Str::ProgramsRemove : Str::ProgramsAdd)});
    if (d && text::istartsWith(d->homepage, L"https://")) {
        model.actions.push_back({kActionHomepage, m_strings.get(Str::ProgramsHomepage)});
    }
    return model;
}

void ProgramsPage::detailAction(int id) {
    const auto program = selectedProgram();
    if (!program) {
        return;
    }
    if (id == kActionToggle) {
        togglePick(m_table->selected()); // the row's own click / Enter
    } else if (id == kActionHomepage && openUrl) {
        if (const core::WingetDetails* d = m_controller.details(program->id); d && text::istartsWith(d->homepage, L"https://")) {
            openUrl(d->homepage);
        }
    }
}

} // namespace wl::app
