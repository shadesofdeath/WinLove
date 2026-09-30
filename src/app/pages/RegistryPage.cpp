#include "app/pages/RegistryPage.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kCardH = 72.0f;
constexpr float kColumnGap = 24.0f;
constexpr float kSectionGap = 16.0f;
constexpr float kSectionH = 20.0f;

enum Column : int { kTweak, kKey, kScope };
} // namespace

// The category cards. Custom-drawn; click / ←→↑↓ select.
class CategoryGrid : public ui::Widget {
public:
    struct Card {
        std::wstring title;
        std::wstring desc;
        std::wstring count;
        bool any = false;
        bool custom = false;
    };
    std::function<void(int)> onSelect;

    CategoryGrid() {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, L"");
    }
    void setCards(std::vector<Card> cards, int selected) {
        m_cards = std::move(cards);
        m_selected = selected;
        invalidate();
    }
    [[nodiscard]] float heightFor() const {
        return kCardH * static_cast<float>((m_cards.size() + 1) / 2);
    }
    [[nodiscard]] ui::Cursor cursor() const override { return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow; }
    void onPointerMove(ui::PointF p) override {
        const int hit = cardAt(p);
        if (hit != m_hover) {
            m_hover = hit;
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
        }
        invalidate();
    }
    void onPointerDown(ui::PointF p) override {
        if (const int hit = cardAt(p); hit >= 0 && onSelect) {
            onSelect(hit);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        int next = m_selected;
        switch (key.virtualKey) {
        case VK_LEFT: next -= 1; break;
        case VK_RIGHT: next += 1; break;
        case VK_UP: next -= 2; break;
        case VK_DOWN: next += 2; break;
        default: return false;
        }
        if (next >= 0 && next < static_cast<int>(m_cards.size()) && onSelect) {
            onSelect(next);
        }
        return true;
    }
    [[nodiscard]] ui::RectF focusRect() const override { return rectOf(m_selected); }

    void paint(ui::Canvas& canvas) override {
        for (int i = 0; i < static_cast<int>(m_cards.size()); ++i) {
            const RectF r = rectOf(i);
            const auto& card = m_cards[static_cast<std::size_t>(i)];
            if (i == m_selected || i == m_hover) {
                canvas.fillRect({r.x, r.y + 1, r.width, r.height - 1}, i == m_selected ? Color::BgRaised : Color::BgPanel);
            }
            canvas.fillRect({r.x, r.y, r.width, 1.0f}, Color::LineSubtle);
            if (i == m_selected) {
                canvas.fillRect({r.x, r.y + 1, 2.0f, r.height - 1}, Color::AccentBase);
            }
            const float x = r.x + 8;
            canvas.drawIcon(card.custom ? ui::icons::Icon::RegFile : ui::icons::Icon::Registry, {x, r.y + 14},
                            Color::TextSecondary);
            const float tx = x + ui::tokens::size::icon + 8;
            const float countW = 120;
            canvas.drawText(card.title, {tx, r.y + 12, r.right() - tx - countW, 20}, TypeStyle::BodyStrong,
                            Color::TextPrimary);
            canvas.drawText(card.desc, {tx, r.y + 30, r.right() - tx - 40, 18}, TypeStyle::Caption,
                            Color::TextSecondary);
            canvas.drawText(card.count, {r.right() - countW - 8, r.y + 12, countW, 20}, TypeStyle::Mono,
                            card.any ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
            canvas.drawIcon(ui::icons::Icon::ChevronRight,
                            {r.right() - 8 - ui::tokens::size::icon, r.y + 38}, Color::TextTertiary);
        }
    }

private:
    [[nodiscard]] RectF rectOf(int i) const {
        const RectF b = bounds();
        const float w = (b.width - kColumnGap) / 2;
        return {b.x + static_cast<float>(i % 2) * (w + kColumnGap), b.y + static_cast<float>(i / 2) * kCardH, w, kCardH};
    }
    [[nodiscard]] int cardAt(ui::PointF p) const {
        for (int i = 0; i < static_cast<int>(m_cards.size()); ++i) {
            if (rectOf(i).contains(p)) {
                return i;
            }
        }
        return -1;
    }
    std::vector<Card> m_cards;
    int m_selected = 0;
    int m_hover = -1;
};

Str RegistryPage::categoryName(std::string_view id) {
    if (id == "privacy") return Str::RegistryCatPrivacy;
    if (id == "performance") return Str::RegistryCatPerformance;
    if (id == "appearance") return Str::RegistryCatAppearance;
    if (id == "explorer") return Str::RegistryCatExplorer;
    if (id == "start") return Str::RegistryCatStart;
    return Str::RegistryCatCustom;
}

RegistryPage::RegistryPage(AppState& state, RegistryController& controller, const Localization& strings,
                           Language language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language),
      m_intents(std::move(intents)) {
    m_grid = &add<CategoryGrid>();
    m_grid->onSelect = [this](int index) { select(index); };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::RegistryTweak), 0},
        {strings.get(Str::RegistryKey), 420},
        {strings.get(Str::RegistryScope), 190},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell);
    };
    m_table->onCellClick = [this](int row, int column, ui::PointF p) {
        if (column == kTweak) {
            const RectF cell = m_table->cellRect(row, kTweak);
            if (p.x < cell.x + ui::Checkbox::kBox + 8) {
                activate(row);
                return;
            }
        }
        if (isCustom() && column == kScope) {
            const RectF cell = m_table->cellRect(row, kScope);
            if (p.x > cell.right() - ui::tokens::size::icon - 4) { // trailing ✕ = remove the import
                m_controller.removeImport(static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]));
            }
        }
    };
    m_table->onActivate = [this](int row) { activate(row); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::Registry, L"", L"");
    setAccessible(ui::AccessRole::Group, strings.get(Str::RegistryTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Queue ||
            change == AppState::Change::Registry || change == AppState::Change::ImageValues) {
            refresh();
        }
    });
    refresh();
}

RegistryPage::~RegistryPage() {
    m_state.unsubscribe(m_subscription);
}

std::string RegistryPage::categoryId(int index) const {
    const auto& cats = m_controller.catalog().categories();
    return index >= 0 && index < static_cast<int>(cats.size()) ? cats[static_cast<std::size_t>(index)].id : "custom";
}

bool RegistryPage::isCustom() const {
    return categoryId(m_category) == "custom";
}

void RegistryPage::showCustom() {
    select(static_cast<int>(m_controller.catalog().categories().size()));
}

void RegistryPage::select(int category) {
    m_category = category;
    refresh();
}

void RegistryPage::activate(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    if (isCustom()) {
        m_controller.toggleImport(index);
    } else {
        m_controller.toggle(m_controller.catalog().tweaks()[index]);
    }
}

void RegistryPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    if (!mounted) {
        m_empty->setContent(ui::icons::Icon::Registry, m_strings.get(Str::RegistryNoMountTitle),
                            m_strings.get(Str::RegistryNoMountBody));
        m_empty->setAction(m_strings.get(Str::FeaturesGoImages)).onInvoke = m_intents.goImages;
    }
    m_empty->setVisible(!mounted);
    m_grid->setVisible(mounted);
    m_table->setVisible(mounted);

    std::vector<CategoryGrid::Card> cards;
    const auto& cats = m_controller.catalog().categories();
    for (std::size_t i = 0; i <= cats.size(); ++i) {
        const bool custom = i == cats.size();
        const std::string id = custom ? "custom" : cats[i].id;
        const auto [on, total] = m_controller.selection(id);
        cards.push_back({m_strings.get(categoryName(id)),
                         custom ? m_strings.get(Str::RegistryCustomDesc) : cats[i].desc(m_language),
                         m_strings.format(Str::RegistrySelectedOf, {{L"s", std::to_wstring(on)}, {L"n", std::to_wstring(total)}}),
                         on > 0, custom});
    }
    m_category = std::clamp(m_category, 0, static_cast<int>(cards.size()) - 1);
    m_grid->setCards(std::move(cards), m_category);

    m_rows.clear();
    if (isCustom()) {
        for (std::size_t i = 0; i < m_state.regImports().size(); ++i) {
            m_rows.push_back(static_cast<int>(i));
        }
    } else {
        const std::string id = categoryId(m_category);
        const auto& tweaks = m_controller.catalog().tweaks();
        for (std::size_t i = 0; i < tweaks.size(); ++i) {
            if (tweaks[i].category == id) {
                m_rows.push_back(static_cast<int>(i));
            }
        }
    }
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    layout();
    invalidate();
}

void RegistryPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(m_rows[static_cast<std::size_t>(row)]);
    const std::vector<core::RegistryWrite>* writes = nullptr;
    auto kind = core::ops::OpKind::SetRegistryValue;
    bool firstLogon = false;
    std::optional<bool> checked; // tweaks: queue + image (D-045); imports: the queue
    std::wstring name;
    std::wstring sub;
    auto subColor = Color::TextTertiary;
    if (isCustom()) {
        const auto& import = m_state.regImports()[index];
        writes = &import.writes;
        name = import.file.filename().wstring();
        sub = m_strings.format(Str::RegistryValuesN, {{L"n", std::to_wstring(import.writes.size())}});
        if (import.skipped > 0) {
            sub += L" · " + m_strings.format(Str::RegistrySkippedN, {{L"n", std::to_wstring(import.skipped)}});
        }
        // Every value of a .reg file is also re-imported after setup (D-026).
        sub += L" · " + m_strings.get(Str::RegistryReapplied);
        kind = RegistryController::kImportKind;
    } else {
        const auto& tweak = m_controller.catalog().tweaks()[index];
        writes = &tweak.writes;
        name = tweak.name(m_language);
        kind = RegistryController::kindOf(tweak);
        firstLogon = tweak.firstLogon;
        checked = m_controller.checked(tweak);
        if (m_controller.inImage(tweak)) {
            // The image has it: say so, and whether Uygula takes it back.
            sub = m_strings.get(*checked ? Str::RegistryInImage : Str::RegistryRevert);
            subColor = *checked ? Color::TextTertiary : Color::AccentBase;
            if (*checked && !m_controller.canUncheck(tweak)) {
                sub += L" · " + m_strings.get(Str::RegistryNoRevert);
            }
        }
    }
    switch (column) {
    case kTweak: {
        const bool on = checked ? *checked : m_controller.checked(*writes, kind);
        const auto state = on ? ui::CheckState::On : ui::CheckState::Off;
        ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2}, state,
                               cell.hoveredCell);
        const float x = rect.x + ui::Checkbox::kBox + 8;
        const float nameW = sub.empty() ? rect.right() - x
                                        : std::min(rect.right() - x, std::ceil(canvas.text().measure(name, TypeStyle::Body)));
        canvas.drawText(name, {x, rect.y, nameW, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        Color::TextPrimary);
        if (!sub.empty()) {
            const float sx = x + nameW + 8;
            canvas.drawText(sub, {sx, rect.y, std::max(rect.right() - sx, 0.0f), rect.height}, TypeStyle::Caption,
                            subColor);
        }
        break;
    }
    case kKey: {
        if (writes->empty()) {
            break;
        }
        std::wstring key = writes->front().key;
        const auto keys = std::ranges::count_if(*writes, [&](const core::RegistryWrite& w) { return w.key != key; });
        if (keys > 0) {
            key += std::format(L"  +{}", keys);
        }
        canvas.drawText(key, rect, TypeStyle::Mono, Color::TextPrimary);
        break;
    }
    case kScope: {
        bool user = false;
        bool system = false;
        for (const auto& w : *writes) {
            (core::isUserKey(w.key) ? user : system) = true;
        }
        const std::wstring text = user && system ? m_strings.get(Str::RegistryScopeSystem) + L" + " +
                                                       m_strings.get(Str::RegistryScopeUser)
                                  : user ? m_strings.get(Str::RegistryScopeUser)
                                         : m_strings.get(Str::RegistryScopeSystem);
        const float w = std::ceil(canvas.text().measure(text, TypeStyle::Caption)) + 12;
        const RectF badge{rect.x, rect.y + (rect.height - 16) / 2, w, 16};
        canvas.strokeRoundRect(badge, ui::tokens::radius::r2, Color::LineStrong);
        canvas.drawText(text, {badge.x + 6, badge.y, badge.width - 12, badge.height}, TypeStyle::Caption,
                        Color::TextSecondary);
        if (firstLogon) {
            const std::wstring later = m_strings.get(Str::RegistryFirstLogon);
            const float lw = std::ceil(canvas.text().measure(later, TypeStyle::Caption)) + 12;
            const RectF tag{badge.right() + 6, badge.y, lw, 16};
            canvas.fillRoundRect(tag, ui::tokens::radius::r2, Color::AccentSubtle);
            canvas.drawText(later, {tag.x + 6, tag.y, tag.width - 12, tag.height}, TypeStyle::Caption, Color::AccentBase);
        }
        if (isCustom()) {
            canvas.drawIcon(ui::icons::Icon::Close, {rect.right() - ui::tokens::size::icon, rect.y + 4},
                            cell.hoveredCell ? Color::TextPrimary : Color::TextTertiary);
        }
        break;
    }
    default: break;
    }
}

void RegistryPage::layout() {
    const RectF b = bounds();
    m_grid->setBounds({b.x, b.y + kTop, b.width, m_grid->heightFor()});
    const float top = m_grid->bounds().bottom() + kSectionGap + kSectionH + 4;
    m_table->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
    m_empty->setBounds(b);
}

void RegistryPage::paint(ui::Canvas& canvas) {
    if (!m_table->visible()) {
        return;
    }
    const RectF b = bounds();
    const float y = m_grid->bounds().bottom() + kSectionGap;
    canvas.drawText(m_strings.get(categoryName(categoryId(m_category))), {b.x, y, b.width, kSectionH},
                    TypeStyle::Section, Color::TextSecondary);
    if (m_rows.empty()) {
        const bool custom = isCustom();
        canvas.drawText(m_strings.get(custom ? Str::RegistryNoImports : Str::FeaturesNoResults),
                        {b.x, m_table->bounds().y + ui::TableView::kHeader + 12, b.width, 20}, TypeStyle::Body,
                        Color::TextTertiary, ui::TextAlign::Center);
    }
}

} // namespace wl::app
