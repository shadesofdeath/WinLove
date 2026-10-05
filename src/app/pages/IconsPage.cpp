#include "app/pages/IconsPage.h"

#include "app/pages/PageBits.h"

#include <windows.h>

#include <algorithm>

namespace wl::app {

using ui::PointF;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
int g_lastTab = 0; // the tab shown when the page opens again
constexpr float kTop = 12.0f;
constexpr float kTabs = 24.0f;
constexpr float kNote = 20.0f;
constexpr float kSection = 36.0f;
constexpr float kCardW = 172.0f;
constexpr float kCardH = 88.0f;
constexpr float kGap = 8.0f;
constexpr float kIcon = 32.0f;
} // namespace

// Every slot as a card; one focusable widget with a roving selection (arrows, Enter, Delete).
class IconsPage::Grid : public ui::Widget {
public:
    Grid(IconController& controller, const Localization& strings) : m_controller(controller), m_strings(strings) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, strings.get(Str::IconsTitle));
        for (const auto& slot : iconSlots()) {
            m_slots.push_back(&slot);
        }
    }
    std::function<void(const IconSlot&)> onPick;
    std::function<void(const IconSlot&)> onReset;

    [[nodiscard]] float contentHeight(float width) const {
        const int columns = std::max(1, static_cast<int>((width + kGap) / (kCardW + kGap)));
        float h = 0;
        for (const auto group : {IconSlot::Group::Desktop, IconSlot::Group::Explorer}) {
            const auto n = std::ranges::count_if(m_slots, [&](const IconSlot* s) { return s->group == group; });
            const int rows = static_cast<int>((n + columns - 1) / columns);
            h += kSection + rows * (kCardH + kGap);
        }
        return h;
    }
    void layout() override {
        m_cards.assign(m_slots.size(), RectF{});
        const RectF b = bounds();
        const int columns = std::max(1, static_cast<int>((b.width + kGap) / (kCardW + kGap)));
        float y = b.y;
        m_sections.clear();
        for (const auto group : {IconSlot::Group::Desktop, IconSlot::Group::Explorer}) {
            m_sections.push_back({b.x, y, b.width, kSection});
            y += kSection;
            int i = 0;
            for (std::size_t s = 0; s < m_slots.size(); ++s) {
                if (m_slots[s]->group != group) {
                    continue;
                }
                const int column = i % columns;
                const int row = i / columns;
                m_cards[s] = {b.x + column * (kCardW + kGap), y + row * (kCardH + kGap), kCardW, kCardH};
                ++i;
            }
            y += ((i + columns - 1) / columns) * (kCardH + kGap);
        }
    }
    void paint(ui::Canvas& canvas) override {
        paintFormSection(canvas, m_sections.size() > 0 ? m_sections[0] : RectF{}, m_strings.get(Str::IconsDesktop));
        if (m_sections.size() > 1) {
            paintFormSection(canvas, m_sections[1], m_strings.get(Str::IconsExplorer));
        }
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            paintCard(canvas, i);
        }
    }
    [[nodiscard]] RectF focusRect() const override {
        return m_selected >= 0 && m_selected < static_cast<int>(m_cards.size()) ? m_cards[static_cast<std::size_t>(m_selected)] : bounds();
    }
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r3; }
    void onPointerMove(PointF p) override {
        const int hover = cardAt(p);
        const bool overReset = hover >= 0 && resetRect(hover).contains(p) && assigned(hover);
        if (hover != m_hover || overReset != m_hoverReset) {
            m_hover = hover;
            m_hoverReset = overReset;
            setTooltip(overReset ? m_strings.get(Str::IconsReset) : std::wstring());
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
            m_hoverReset = false;
        }
        invalidate();
    }
    void onPointerDown(PointF p) override {
        m_down = cardAt(p);
        m_downReset = m_down >= 0 && resetRect(m_down).contains(p) && assigned(m_down);
        if (m_down >= 0) {
            m_selected = m_down;
            invalidate();
        }
    }
    void onClick() override {
        if (m_down < 0) {
            return;
        }
        const auto& slot = *m_slots[static_cast<std::size_t>(m_down)];
        if (m_downReset) {
            if (onReset) {
                onReset(slot);
            }
        } else if (onPick) {
            onPick(slot);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        const int count = static_cast<int>(m_slots.size());
        const int columns = std::max(1, static_cast<int>((bounds().width + kGap) / (kCardW + kGap)));
        auto move = [&](int to) {
            m_selected = std::clamp(to, 0, count - 1);
            invalidate();
            return true;
        };
        switch (key.virtualKey) {
        case VK_LEFT: return move(m_selected - 1);
        case VK_RIGHT: return move(m_selected + 1);
        case VK_UP: return move(m_selected - columns);
        case VK_DOWN: return move(m_selected + columns);
        case VK_HOME: return move(0);
        case VK_END: return move(count - 1);
        case VK_RETURN:
        case VK_SPACE:
            if (m_selected >= 0 && onPick) {
                onPick(*m_slots[static_cast<std::size_t>(m_selected)]);
            }
            return true;
        case VK_DELETE:
        case VK_BACK:
            if (m_selected >= 0 && onReset) {
                onReset(*m_slots[static_cast<std::size_t>(m_selected)]);
            }
            return true;
        default: return false;
        }
    }

private:
    [[nodiscard]] bool assigned(int i) const { return m_controller.assigned(*m_slots[static_cast<std::size_t>(i)]).has_value(); }
    [[nodiscard]] int cardAt(PointF p) const {
        for (std::size_t i = 0; i < m_cards.size(); ++i) {
            if (m_cards[i].contains(p)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    [[nodiscard]] RectF resetRect(int i) const {
        const RectF c = m_cards[static_cast<std::size_t>(i)];
        return {c.right() - 22, c.y + 6, 16, 16};
    }
    void paintCard(ui::Canvas& canvas, std::size_t i) {
        const RectF c = m_cards[i];
        const auto& slot = *m_slots[i];
        const auto icon = m_controller.assigned(slot);
        const bool hover = static_cast<int>(i) == m_hover;
        const bool selected = static_cast<int>(i) == m_selected;
        canvas.fillRoundRect(c, ui::tokens::radius::r3, hover ? Color::BgRaised : Color::BgPanel);
        canvas.strokeRoundRect(c, ui::tokens::radius::r3, icon ? Color::AccentBase : selected ? Color::LineStrong : Color::LineSubtle);
        // Now → then.
        const auto [file, index] = m_controller.defaultIcon(slot);
        const float y = c.y + 12;
        const RectF now{c.x + 20, y, kIcon, kIcon};
        if (!canvas.drawFileIcon(file.wstring(), index, now, icon ? 0.55f : 1.0f)) {
            canvas.strokeRoundRect(now, ui::tokens::radius::r2, Color::LineSubtle);
        }
        canvas.drawIcon(ui::icons::Icon::ArrowRight, {c.x + c.width / 2 - 8, y + 8}, icon ? Color::AccentBase : Color::TextTertiary);
        const RectF next{c.right() - 20 - kIcon, y, kIcon, kIcon};
        if (icon) {
            if (!canvas.drawFileIcon(icon->wstring(), 0, next)) {
                canvas.strokeRoundRect(next, ui::tokens::radius::r2, Color::StatusError);
            }
        } else {
            canvas.strokeRoundRect(next, ui::tokens::radius::r2, hover ? Color::LineStrong : Color::LineSubtle);
            canvas.drawIcon(ui::icons::Icon::Add, {next.x + 8, next.y + 8}, Color::TextTertiary);
        }
        canvas.drawText(m_strings.get(slot.name), {c.x + 10, c.y + 52, c.width - 20, 16},
                        selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        const bool noArrow = slot.id == "shortcut" && m_controller.shortcutArrowRemoved();
        const std::wstring source = noArrow ? m_strings.get(Str::IconsArrowNone)
                                    : icon  ? icon->filename().wstring()
                                            : m_strings.get(Str::IconsWindowsDefault);
        canvas.drawText(source, {c.x + 10, c.y + 68, c.width - 20, 14}, TypeStyle::Caption, icon ? Color::AccentBase : Color::TextTertiary);
        if (icon && hover) {
            const RectF r = resetRect(static_cast<int>(i));
            if (m_hoverReset) {
                canvas.fillRoundRect(r, ui::tokens::radius::r1, Color::BgPressed);
            }
            canvas.drawIcon(ui::icons::Icon::Close, {r.x, r.y}, m_hoverReset ? Color::TextPrimary : Color::TextSecondary);
        }
    }

    IconController& m_controller;
    const Localization& m_strings;
    std::vector<const IconSlot*> m_slots;
    std::vector<RectF> m_cards;
    std::vector<RectF> m_sections;
    int m_selected = 0;
    int m_hover = -1;
    bool m_hoverReset = false;
    int m_down = -1;
    bool m_downReset = false;
};

IconsPage::IconsPage(AppState& state, IconController& controller, IconPatchController& patches, const Localization& strings,
                     Language, Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_intents(std::move(intents)) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::IconsTabFiles), strings.get(Str::IconsTabRedirect)},
                              g_lastTab);
    m_tabs->onChange = [this](int tab) { showTab(tab); };
    m_files = &add<IconFilesView>(state, patches, strings, m_intents.files);
    m_grid = &add<Grid>(controller, strings);
    m_grid->onPick = [this](const IconSlot& slot) {
        if (!m_intents.pickIcon) {
            return;
        }
        if (const auto file = m_intents.pickIcon()) {
            if (auto ok = m_controller.assign(slot, *file); !ok && m_intents.refused) {
                m_intents.refused(file->filename().wstring());
            }
        }
    };
    m_grid->onReset = [this](const IconSlot& slot) { m_controller.reset(slot); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::DensityComfortable, strings.get(Str::IconsNoMountTitle), strings.get(Str::IconsNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = [this] {
        if (m_intents.goImages) {
            m_intents.goImages();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::IconsTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Queue || change == AppState::Change::Mount || change == AppState::Change::Apply) {
            refresh();
        }
    });
    refresh();
}

void IconsPage::showTab(int tab) {
    g_lastTab = std::clamp(tab, 0, 1);
    m_tabs->setSelected(g_lastTab);
    refresh();
}

IconsPage::~IconsPage() {
    m_state.unsubscribe(m_subscription);
}

void IconsPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_files->setVisible(mounted && g_lastTab == 0);
    m_grid->setVisible(mounted && g_lastTab == 1);
    if (m_files->visible()) {
        m_files->refresh();
    }
    layout();
    invalidate();
}

void IconsPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y + kTop, b.width, kTabs});
    const float content = b.y + kTop + kTabs + 12;
    m_files->setBounds({b.x, content, b.width, std::max(b.bottom() - content, 0.0f)});
    const float y = content + kNote;
    m_grid->setBounds({b.x, y, b.width, m_grid->contentHeight(b.width)});
}

void IconsPage::paint(ui::Canvas& canvas) {
    if (!m_state.mounted() || g_lastTab != 1) {
        return;
    }
    const RectF b = bounds();
    const float content = b.y + kTop + kTabs + 12;
    canvas.drawText(m_strings.get(Str::IconsNote), {b.x, content - 4, b.width, kNote}, TypeStyle::Caption, Color::TextTertiary);
}

} // namespace wl::app
