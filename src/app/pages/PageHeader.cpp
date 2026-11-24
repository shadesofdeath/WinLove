#include "app/pages/PageHeader.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr TypeStyle kStyle = TypeStyle::Caption;
constexpr wchar_t kDot[] = L" · ";

Color inkOf(SummaryPart::Tone tone) {
    switch (tone) {
    case SummaryPart::Tone::Change: return Color::AccentText;
    case SummaryPart::Tone::Warning: return Color::StatusWarning;
    case SummaryPart::Tone::Quiet: break;
    }
    return Color::TextTertiary;
}
} // namespace

SummaryLine::SummaryLine() {
    setHitTestVisible(false);
}

void SummaryLine::set(std::vector<SummaryPart> parts) {
    std::erase_if(parts, [](const SummaryPart& p) { return p.text.empty(); });
    if (parts == m_parts) {
        return;
    }
    m_parts = std::move(parts);
    setAccessible(ui::AccessRole::Text, text());
    layout();
    invalidate();
}

std::wstring SummaryLine::text() const {
    std::wstring out;
    for (const auto& part : m_parts) {
        out += (out.empty() ? L"" : kDot) + part.text;
    }
    return out;
}

std::wstring SummaryLine::shownText() const {
    std::wstring out;
    for (std::size_t i = 0; i < m_parts.size() && i < m_placed.size(); ++i) {
        if (m_placed[i].shown) {
            out += (out.empty() ? L"" : kDot) + (m_placed[i].brief ? m_parts[i].shortText : m_parts[i].text);
        }
    }
    return out;
}

void SummaryLine::layout() {
    // Right-aligned, the last part at the right edge. Too long: the quiet parts (what the list has)
    // drop first, from the left; what remains is cut at its left end by the room it gets.
    m_placed.assign(m_parts.size(), {});
    if (!host() || m_parts.empty()) {
        return;
    }
    const auto& text = host()->text();
    m_dotWidth = std::ceil(text.measure(kDot, kStyle));
    std::vector<float> widths;
    std::vector<float> briefs;
    widths.reserve(m_parts.size());
    for (const auto& part : m_parts) {
        widths.push_back(std::ceil(text.measure(part.text, kStyle)));
        briefs.push_back(part.shortText.empty() ? widths.back() : std::ceil(text.measure(part.shortText, kStyle)));
    }
    const float room = bounds().width;
    std::vector<bool> shown(m_parts.size(), true);
    auto total = [&] {
        float w = 0;
        int n = 0;
        for (std::size_t i = 0; i < m_parts.size(); ++i) {
            if (shown[i]) {
                w += widths[i];
                ++n;
            }
        }
        return w + static_cast<float>(std::max(n - 1, 0)) * m_dotWidth;
    };
    for (std::size_t i = 0; i < m_parts.size() && total() > room; ++i) {
        if (m_parts[i].tone == SummaryPart::Tone::Quiet && std::ranges::count(shown, true) > 1) {
            shown[i] = false;
        }
    }
    std::vector<bool> brief(m_parts.size(), false);
    for (std::size_t i = 0; i < m_parts.size() && total() > room; ++i) {
        if (!m_parts[i].shortText.empty()) {
            brief[i] = true;
            widths[i] = briefs[i];
        }
    }
    float x = bounds().right();
    for (std::size_t i = m_parts.size(); i-- > 0;) {
        if (!shown[i]) {
            continue;
        }
        const float w = std::min(widths[i], std::max(x - bounds().x, 0.0f));
        m_placed[i] = {x - w, w, w > 0, brief[i]};
        x -= w + m_dotWidth;
    }
}

void SummaryLine::paint(ui::Canvas& canvas) {
    const ui::RectF b = bounds();
    bool first = true;
    for (std::size_t i = 0; i < m_parts.size() && i < m_placed.size(); ++i) {
        const Placed& p = m_placed[i];
        if (!p.shown) {
            continue;
        }
        if (!first) {
            canvas.drawText(kDot, {p.x - m_dotWidth, b.y, m_dotWidth, b.height}, kStyle, Color::TextTertiary);
        }
        first = false;
        canvas.drawText(p.brief ? m_parts[i].shortText : m_parts[i].text, {p.x, b.y, p.width, b.height}, kStyle, inkOf(m_parts[i].tone));
    }
}

} // namespace wl::app
