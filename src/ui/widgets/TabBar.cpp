#include "ui/widgets/TabBar.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
constexpr float kTabPadding = 12.0f;
constexpr float kTabGap = 4.0f;
constexpr float kRadio = 12.0f;
constexpr float kRadioGap = 8.0f;
constexpr float kOptionGap = 16.0f;

float textWidth(const Widget& w, const std::wstring& text, TypeStyle style) {
    return w.host() ? std::ceil(w.host()->text().measure(text, style)) : 60.0f;
}
} // namespace

// ---- TabBar ---------------------------------------------------------------------------------

TabBar::TabBar(std::vector<std::wstring> tabs, int selected) : m_tabs(std::move(tabs)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::TabItem, m_tabs.empty() ? std::wstring() : m_tabs.front());
}

void TabBar::setSelected(int index) {
    m_selected = std::clamp(index, 0, static_cast<int>(m_tabs.size()) - 1);
    invalidate();
}

SizeF TabBar::measure(SizeF available) {
    return {available.width, tokens::size::control + 2};
}

std::vector<RectF> TabBar::tabRects() const {
    std::vector<RectF> rects;
    const RectF b = bounds();
    float x = b.x;
    for (const auto& tab : m_tabs) {
        const float w = textWidth(*this, tab, TypeStyle::BodyStrong) + 2 * kTabPadding;
        rects.push_back({x, b.y, w, b.height});
        x += w + kTabGap;
    }
    return rects;
}

void TabBar::paint(Canvas& canvas) {
    const RectF b = bounds();
    canvas.hairlineH(b.x, b.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const bool selected = static_cast<int>(i) == m_selected;
        const RectF r = rects[i];
        if (!selected && static_cast<int>(i) == m_hover) {
            canvas.fillRoundRect({r.x, r.y + 1, r.width, r.height - 4}, tokens::radius::r2, Color::BgRaised);
        }
        canvas.drawText(m_tabs[i], {r.x, r.y, r.width, r.height - 2}, selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                        selected ? Color::TextPrimary : Color::TextSecondary, TextAlign::Center);
        if (selected) {
            canvas.fillRect({r.x + 4, r.bottom() - 2, r.width - 8, 2}, Color::AccentBase);
        }
    }
}

void TabBar::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
}

void TabBar::onPointerDown(PointF p) {
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p) && static_cast<int>(i) != m_selected) {
            m_selected = static_cast<int>(i);
            invalidate();
            if (onChange) {
                onChange(m_selected);
            }
        }
    }
}

bool TabBar::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey != VK_LEFT && key.virtualKey != VK_RIGHT) {
        return false;
    }
    const int next = std::clamp(m_selected + (key.virtualKey == VK_RIGHT ? 1 : -1), 0, static_cast<int>(m_tabs.size()) - 1);
    if (next != m_selected) {
        m_selected = next;
        invalidate();
        if (onChange) {
            onChange(next);
        }
    }
    return true;
}

// ---- RadioGroup -------------------------------------------------------------------------------

RadioGroup::RadioGroup(std::vector<std::wstring> options, int selected)
    : m_options(std::move(options)), m_enabled(m_options.size(), true), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Group, L"");
}

void RadioGroup::setSelected(int index) {
    m_selected = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    invalidate();
}

void RadioGroup::setOptionEnabled(int index, bool enabled) {
    if (index >= 0 && index < static_cast<int>(m_enabled.size())) {
        m_enabled[static_cast<std::size_t>(index)] = enabled;
        invalidate();
    }
}

std::vector<RectF> RadioGroup::optionRects() const {
    std::vector<RectF> rects;
    const RectF b = bounds();
    float x = b.x;
    for (const auto& option : m_options) {
        const float w = kRadio + kRadioGap + textWidth(*this, option, TypeStyle::Body);
        rects.push_back({x, b.y, w, b.height});
        x += w + kOptionGap;
    }
    return rects;
}

SizeF RadioGroup::measure(SizeF /*available*/) {
    const auto rects = optionRects();
    return {rects.empty() ? 0.0f : rects.back().right() - bounds().x, tokens::size::control};
}

void RadioGroup::paint(Canvas& canvas) {
    const auto rects = optionRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const RectF r = rects[i];
        const bool selected = static_cast<int>(i) == m_selected;
        const bool enabled = m_enabled[i];
        if (!enabled) {
            canvas.pushOpacity(tokens::opacity::disabled);
        }
        const PointF center{r.x + kRadio / 2, r.y + r.height / 2};
        canvas.fillEllipse(center, kRadio / 2, Color::BgInput);
        canvas.strokeRoundRect({r.x, center.y - kRadio / 2, kRadio, kRadio}, kRadio / 2,
                               selected ? Color::AccentBase : Color::LineStrong);
        if (selected) {
            canvas.fillEllipse(center, 3.0f, Color::AccentBase);
        }
        const float x = r.x + kRadio + kRadioGap;
        canvas.drawText(m_options[i], {x, r.y, r.right() - x + 2, r.height}, TypeStyle::Body, Color::TextPrimary);
        if (!enabled) {
            canvas.popOpacity();
        }
    }
}

void RadioGroup::choose(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size()) || !m_enabled[static_cast<std::size_t>(index)] ||
        index == m_selected) {
        return;
    }
    m_selected = index;
    invalidate();
    if (onChange) {
        onChange(index);
    }
}

void RadioGroup::onPointerDown(PointF p) {
    const auto rects = optionRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            choose(static_cast<int>(i));
        }
    }
}

bool RadioGroup::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_LEFT || key.virtualKey == VK_RIGHT) {
        int next = m_selected;
        const int step = key.virtualKey == VK_RIGHT ? 1 : -1;
        do {
            next += step;
        } while (next >= 0 && next < static_cast<int>(m_options.size()) && !m_enabled[static_cast<std::size_t>(next)]);
        choose(next);
        return true;
    }
    return false;
}

} // namespace wl::ui
