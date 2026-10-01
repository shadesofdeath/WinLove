#include "ui/widgets/Dropdown.h"

#include "ui/anim/Tween.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;

constexpr float kPadding = 8.0f;
constexpr float kGap = 6.0f;
constexpr float kMenuPadding = 4.0f;
constexpr float kItem = 24.0f;
constexpr float kMenuMinWidth = 160.0f; // contextmenu.md says 220 for menus; dropdown lists follow the field
constexpr float kMenuMaxWidth = 360.0f;
constexpr float kMenuOffset = 2.0f;

} // namespace

Dropdown::Dropdown(std::wstring label, std::vector<std::wstring> items, int selected)
    : m_label(std::move(label)), m_items(std::move(items)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Button, m_label);
}

Dropdown::~Dropdown() {
    closePopup();
}

void Dropdown::popupClosed() noexcept {
    m_open = false;
    m_popup = nullptr;
    invalidate();
}

void Dropdown::closePopup() {
    if (m_popup) {
        m_popup->close(); // pops it; its closed callback clears m_popup
    }
}

void Dropdown::setItems(std::vector<std::wstring> items, int selected) {
    // An open list would pick from the old items (a scan finished while it was open).
    closePopup();
    m_items = std::move(items);
    m_selected = std::clamp(selected, 0, std::max(static_cast<int>(m_items.size()) - 1, 0));
    invalidate();
}

void Dropdown::setSelected(int index) {
    m_selected = std::clamp(index, 0, std::max(static_cast<int>(m_items.size()) - 1, 0));
    invalidate();
}

SizeF Dropdown::measure(SizeF /*available*/) {
    if (!host()) {
        return {120, tokens::size::control};
    }
    const auto& text = host()->text();
    float value = 0;
    for (const auto& item : m_items) {
        value = std::max(value, text.measure(item, TypeStyle::BodyStrong));
    }
    const float label = m_label.empty() ? 0 : text.measure(m_label, TypeStyle::Body) + kGap;
    return {std::ceil(kPadding + label + value + kGap + tokens::size::icon + kGap), tokens::size::control};
}

void Dropdown::paint(Canvas& canvas) {
    const RectF b = bounds();
    const Color border = m_open ? Color::AccentBase : hovered() ? Color::TextTertiary : Color::LineStrong;
    canvas.fillRoundRect(b, tokens::radius::r2, Color::BgInput);
    canvas.strokeRoundRect(b, tokens::radius::r2, border);
    float x = b.x + kPadding;
    if (!m_label.empty()) {
        const float w = std::ceil(canvas.text().measure(m_label, TypeStyle::Body));
        canvas.drawText(m_label, {x, b.y, w, b.height}, TypeStyle::Body, Color::TextSecondary);
        x += w + kGap;
    }
    const float chevronX = b.right() - kGap - tokens::size::icon;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size())) {
        canvas.drawText(m_items[static_cast<std::size_t>(m_selected)], {x, b.y, chevronX - x - 2, b.height},
                        TypeStyle::BodyStrong, Color::TextPrimary);
    }
    canvas.drawIcon(icons::Icon::ChevronDown, {chevronX, b.y + (b.height - tokens::size::icon) / 2}, Color::TextTertiary);
}

void Dropdown::open() {
    if (m_open || !host() || m_items.empty()) {
        return;
    }
    m_open = true;
    invalidate();
    auto popup = std::make_unique<MenuPopup>(
        bounds(), m_items, m_selected,
        [this](int index) {
            if (index < 0 || index >= static_cast<int>(m_items.size())) {
                return;
            }
            const bool changed = index != m_selected;
            m_selected = index;
            invalidate();
            if (changed && onChange) {
                onChange(index);
            }
        },
        [this] { popupClosed(); });
    m_popup = popup.get();
    host()->pushModal(std::move(popup), m_popup, /*scrim=*/false);
}

void Dropdown::onClick() {
    open();
}

bool Dropdown::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_RETURN || key.virtualKey == VK_SPACE || (key.virtualKey == VK_DOWN && key.alt)) {
        open();
        return true;
    }
    // Closed-state arrows change the value directly (like a native combo box).
    if ((key.virtualKey == VK_DOWN || key.virtualKey == VK_UP) && !m_items.empty()) {
        const int next = std::clamp(m_selected + (key.virtualKey == VK_DOWN ? 1 : -1), 0,
                                    static_cast<int>(m_items.size()) - 1);
        if (next != m_selected) {
            m_selected = next;
            invalidate();
            if (onChange) {
                onChange(next);
            }
        }
        return true;
    }
    return false;
}

// ---- MenuPopup ------------------------------------------------------------------------------

MenuPopup::MenuPopup(RectF anchor, std::vector<std::wstring> items, int selected, std::function<void(int)> picked,
                     std::function<void()> closed)
    : m_anchor(anchor), m_items(std::move(items)), m_selected(selected), m_hover(selected),
      m_picked(std::move(picked)), m_closed(std::move(closed)) {
    setFocusable(true);
    setAccessible(AccessRole::List, L"");
}

MenuPopup::~MenuPopup() = default;

void MenuPopup::layout() {
    const RectF window = bounds();
    float width = std::max(m_anchor.width, kMenuMinWidth);
    if (host()) {
        for (const auto& item : m_items) {
            width = std::max(width, host()->text().measure(item, TypeStyle::Body) + 2 * kMenuPadding + 16 + 8 + 16);
        }
    }
    width = std::ceil(std::min(width, kMenuMaxWidth));
    // As many items as fit above or below the field (whichever has more room), at least 4.
    const float below = window.bottom() - 4 - (m_anchor.bottom() + kMenuOffset);
    const float above = m_anchor.y - kMenuOffset - (window.y + 4);
    const int fit = std::max(4, static_cast<int>((std::max(below, above) - 2 * kMenuPadding) / kItem));
    m_visible = std::min(static_cast<int>(m_items.size()), fit);
    reveal(std::max(m_hover, 0));
    const float height = 2 * kMenuPadding + kItem * static_cast<float>(m_visible);
    float x = std::min(m_anchor.x, window.right() - width - 4);
    float y = m_anchor.bottom() + kMenuOffset;
    if (y + height > window.bottom() - 4) {
        y = m_anchor.y - kMenuOffset - height; // flip above when it does not fit below
    }
    m_panel = {std::max(x, 4.0f), std::max(y, 4.0f), width, height};
}

Widget* MenuPopup::hitTest(PointF p) {
    return bounds().contains(p) ? this : nullptr; // everything, so outside clicks close the menu
}

int MenuPopup::itemAt(PointF p) const {
    if (!m_panel.contains(p)) {
        return -1;
    }
    const float offset = p.y - m_panel.y - kMenuPadding;
    if (offset < 0) {
        return -1; // the top padding (truncation would make it row 0)
    }
    const int row = static_cast<int>(offset / kItem);
    const int index = m_first + row;
    return row >= 0 && row < m_visible && index < static_cast<int>(m_items.size()) ? index : -1;
}

void MenuPopup::onPointerMove(PointF p) {
    const int index = itemAt(p);
    if (index >= 0 && index != m_hover) {
        m_hover = index;
        invalidate();
    }
}

void MenuPopup::onPointerDown(PointF p) {
    const int index = itemAt(p);
    if (index >= 0) {
        pick(index);
    } else if (!m_panel.contains(p)) {
        close();
    }
}

void MenuPopup::reveal(int index) {
    const int count = static_cast<int>(m_items.size());
    if (m_visible <= 0) {
        return;
    }
    if (index < m_first) {
        m_first = index;
    } else if (index >= m_first + m_visible) {
        m_first = index - m_visible + 1;
    }
    m_first = std::clamp(m_first, 0, std::max(count - m_visible, 0));
}

bool MenuPopup::onWheel(PointF /*p*/, float lines) {
    const int count = static_cast<int>(m_items.size());
    m_first = std::clamp(m_first - static_cast<int>(std::round(lines * 3)), 0, std::max(count - m_visible, 0));
    invalidate();
    return true;
}

bool MenuPopup::onChar(wchar_t ch) {
    if (ch < 0x20) {
        return true;
    }
    const double now = nowMs();
    if (now - m_typedAt > 900) {
        m_typed.clear();
    }
    m_typedAt = now;
    m_typed.push_back(static_cast<wchar_t>(std::towlower(ch)));
    const int count = static_cast<int>(m_items.size());
    for (int i = 0; i < count; ++i) {
        const auto& item = m_items[static_cast<std::size_t>(i)];
        if (item.size() >= m_typed.size() &&
            std::equal(m_typed.begin(), m_typed.end(), item.begin(),
                       [](wchar_t a, wchar_t b) { return a == static_cast<wchar_t>(std::towlower(b)); })) {
            m_hover = i;
            reveal(i);
            invalidate();
            break;
        }
    }
    return true;
}

bool MenuPopup::onContextMenu(PointF /*p*/) {
    close();
    return true;
}

void MenuPopup::close() {
    auto closed = m_closed;
    if (Host* h = host()) {
        h->popModal(this); // destroys this
    }
    if (closed) {
        closed();
    }
}

void MenuPopup::pick(int index) {
    auto picked = m_picked;
    auto closed = m_closed;
    if (Host* h = host()) {
        h->popModal(this); // destroys this
    }
    if (closed) {
        closed();
    }
    if (picked) {
        picked(index);
    }
}

bool MenuPopup::onKeyDown(const KeyEvent& key) {
    const int count = static_cast<int>(m_items.size());
    switch (key.virtualKey) {
    case VK_ESCAPE:
    case VK_TAB: close(); return true;
    case VK_DOWN: m_hover = std::min(m_hover + 1, count - 1); reveal(m_hover); invalidate(); return true;
    case VK_UP: m_hover = std::max(m_hover - 1, 0); reveal(m_hover); invalidate(); return true;
    case VK_NEXT: m_hover = std::min(m_hover + std::max(m_visible - 1, 1), count - 1); reveal(m_hover); invalidate(); return true;
    case VK_PRIOR: m_hover = std::max(m_hover - std::max(m_visible - 1, 1), 0); reveal(m_hover); invalidate(); return true;
    case VK_HOME: m_hover = 0; reveal(m_hover); invalidate(); return true;
    case VK_END: m_hover = count - 1; reveal(m_hover); invalidate(); return true;
    case VK_RETURN:
    case VK_SPACE:
        if (m_hover >= 0) {
            pick(m_hover);
        }
        return true;
    default: return true; // modal: swallow everything else
    }
}

void MenuPopup::paint(Canvas& canvas) {
    canvas.panel(m_panel, tokens::radius::r3, tokens::elevation::menu);
    const int count = static_cast<int>(m_items.size());
    const bool scrolls = m_visible < count;
    for (int i = m_first; i < std::min(m_first + m_visible, count); ++i) {
        const RectF row{m_panel.x + kMenuPadding, m_panel.y + kMenuPadding + kItem * static_cast<float>(i - m_first),
                        m_panel.width - 2 * kMenuPadding - (scrolls ? 4.0f : 0.0f), kItem};
        if (i == m_hover) {
            canvas.fillRoundRect(row, 3.0f, Color::BgRaised);
        }
        if (i == m_selected) {
            canvas.drawIcon(icons::Icon::Check, {row.x + 4, row.y + 4}, Color::AccentBase);
        }
        const float textX = row.x + 4 + tokens::size::icon + 8;
        canvas.drawText(m_items[static_cast<std::size_t>(i)], {textX, row.y, row.right() - textX - 4, kItem},
                        TypeStyle::Body, Color::TextPrimary);
    }
    if (scrolls) {
        const float track = m_panel.height - 2 * kMenuPadding;
        const float thumb = std::max(track * static_cast<float>(m_visible) / static_cast<float>(count), 16.0f);
        const float y = m_panel.y + kMenuPadding +
                        (track - thumb) * static_cast<float>(m_first) / static_cast<float>(std::max(count - m_visible, 1));
        canvas.fillRoundRect({m_panel.right() - kMenuPadding - 2, y, 2, thumb}, 1.0f, Color::LineStrong);
    }
}

} // namespace wl::ui
