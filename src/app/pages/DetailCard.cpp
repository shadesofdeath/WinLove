#include "app/pages/DetailCard.h"

#include "ui/widget/Host.h"
#include "ui/widgets/FormView.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;
namespace spacing = ui::tokens::spacing;

namespace {
// The card's measures (D-139), from the tokens: the side panels' width, a 16 inset, a 32 tile.
constexpr float kPad = spacing::s6;
constexpr float kTile = size::controlXL;
constexpr float kTileGap = spacing::s5 - spacing::s1;                     // tile → name (10)
constexpr float kLine = 16.0f;                                            // a Small / Caption / Mono line
constexpr float kStateH = 20.0f;
constexpr float kFactH = spacing::s8;                                     // a label / value row (24)
constexpr float kLabelW = 116.0f;                                         // the label column of the facts
constexpr float kSectionAbove = spacing::s5;
constexpr float kSectionLine = ui::FormView::kSectionLine;
constexpr float kSectionBelow = spacing::s3;
constexpr float kSection = kSectionAbove + kSectionLine + kSectionBelow;  // a section title block
constexpr float kListLine = 20.0f;
constexpr int kAboutLines = 5;
constexpr float kEmptyTile = 40.0f;
constexpr float kTitleLine = 18.0f; // BodySemi's line

Color inkOf(DetailTone tone) {
    switch (tone) {
    case DetailTone::Quiet: return Color::TextTertiary;
    case DetailTone::Change: return Color::AccentText;
    case DetailTone::Success: return Color::StatusSuccess;
    case DetailTone::Warning: return Color::StatusWarning;
    case DetailTone::Error: return Color::StatusError;
    case DetailTone::Normal: break;
    }
    return Color::TextPrimary;
}
} // namespace

const DetailFact* DetailModel::fact(std::wstring_view label) const {
    const auto it = std::ranges::find(facts, label, &DetailFact::label);
    return it == facts.end() ? nullptr : &*it;
}

const DetailAction* DetailModel::action(int id) const {
    const auto it = std::ranges::find(actions, id, &DetailAction::id);
    return it == actions.end() ? nullptr : &*it;
}

bool detailCardFits(float pageCardWidth) noexcept {
    return pageCardWidth - (size::inspector + size::contentPad) >= kDetailMinPageCard;
}

DetailCard::DetailCard(const Localization& strings) : m_strings(strings) {
    m_emptyTitle = strings.get(Str::DetailEmptyTitle);
    m_emptyBody = strings.get(Str::DetailEmptyBody);
    m_detailsTitle = strings.get(Str::CommonDetails);
    for (int i = 0; i < kMaxActions; ++i) {
        auto& button = add<ui::Button>(ui::ButtonKind::Secondary, L"");
        button.setVisible(false);
        // Only the text changes with the row (set() may run inside this very button's click).
        button.onInvoke = [this, i] {
            if (onAction) {
                onAction(m_actionIds[i]);
            }
        };
        m_buttons.push_back(&button);
    }
    setAccessible(ui::AccessRole::Group, strings.get(Str::CommonDetails));
}

void DetailCard::setEmptyIcon(ui::icons::Icon icon) {
    m_emptyIcon = icon;
    invalidate();
}

void DetailCard::set(DetailModel model) {
    m_model = std::move(model);
    m_letter.clear();
    if (m_model.letter && !m_model.title.empty()) {
        m_letter.assign(1, static_cast<wchar_t>(std::towupper(m_model.title.front())));
    }
    for (int i = 0; i < kMaxActions; ++i) {
        ui::Button* button = m_buttons[static_cast<std::size_t>(i)];
        if (i < static_cast<int>(m_model.actions.size())) {
            const auto& action = m_model.actions[static_cast<std::size_t>(i)];
            m_actionIds[i] = action.id;
            button->setText(action.label);
            button->setEnabled(action.enabled);
            button->setVisible(true);
        } else {
            m_actionIds[i] = 0;
            button->setVisible(false);
        }
    }
    setAccessible(ui::AccessRole::Group, m_model.empty() ? m_strings.get(Str::CommonDetails) : m_model.title);
    layout();
    invalidate();
}

void DetailCard::layout() {
    const RectF b = bounds();
    const float x = b.x + kPad;
    const float width = std::max(b.width - 2 * kPad, 0.0f);
    // The actions first: a flow of quiet buttons at the foot, wrapping to a new row when they do not fit.
    struct Placed {
        ui::Button* button;
        ui::SizeF size;
    };
    std::vector<std::vector<Placed>> rows;
    float rowWidth = 0;
    for (ui::Button* button : m_buttons) {
        if (!button->visible()) {
            continue;
        }
        ui::SizeF s = button->measure({});
        s.width = std::min(s.width, width);
        if (rows.empty() || rowWidth + spacing::s4 + s.width > width) {
            rows.emplace_back();
            rowWidth = 0;
        } else {
            rowWidth += spacing::s4;
        }
        rows.back().push_back({button, s});
        rowWidth += s.width;
    }
    float y = b.bottom() - kPad;
    for (auto row = rows.rbegin(); row != rows.rend(); ++row) {
        float height = 0;
        for (const auto& p : *row) {
            height = std::max(height, p.size.height);
        }
        y -= height;
        float bx = x;
        for (const auto& p : *row) {
            p.button->setBounds({bx, y, p.size.width, p.size.height});
            bx += p.size.width + spacing::s4;
        }
        y -= spacing::s4;
    }
    m_foot = rows.empty() ? b.bottom() - kPad : y - spacing::s4;

    m_facts.clear();
    m_notes.clear();
    m_lists.clear();
    if (m_model.empty() || !host()) {
        return;
    }
    const auto& text = host()->text();
    y = b.y + kPad;
    // The name takes two lines when it needs them (a service's long display name), the subtitle under it.
    const float nameRoom = std::max(width - kTile - kTileGap, 0.0f);
    m_titleH = std::min(std::ceil(text.measureWrapped(m_model.title, TypeStyle::BodySemi, nameRoom)), 2 * kTitleLine);
    m_titleH = std::max(m_titleH, kTitleLine);
    const float headH = std::max(kTile, m_titleH + (m_model.subtitle.empty() ? 0.0f : kLine));
    m_head = {y, headH};
    y += headH + spacing::s4;
    m_identity = {y, m_model.identity.empty() ? 0.0f : kLine};
    y += m_model.identity.empty() ? 0.0f : kLine + spacing::s4;
    m_state = {y, m_model.state.text.empty() ? 0.0f : kStateH};
    y += m_model.state.text.empty() ? 0.0f : kStateH + spacing::s3;
    if (!m_model.about.empty()) {
        const float h = std::min(std::ceil(text.measureWrapped(m_model.about, TypeStyle::Small, width)), kLine * kAboutLines);
        m_about = {y, h};
        y += h;
    } else {
        m_about = {y, 0};
    }
    if (!m_model.facts.empty()) {
        m_factsTitle = {y, kSection};
        y += kSection;
        for (std::size_t i = 0; i < m_model.facts.size(); ++i) {
            m_facts.push_back({y, kFactH});
            y += kFactH;
        }
    }
    for (const auto& note : m_model.notes) {
        y += spacing::s4;
        const float h = std::max(std::ceil(text.measureWrapped(note.text, TypeStyle::Small, width - size::icon - spacing::s3)), kLine);
        m_notes.push_back({y, h});
        y += h;
    }
    for (const auto& list : m_model.lists) {
        ListBlock block;
        block.title = {y, kSection};
        y += kSection;
        block.linesY = y;
        const float room = std::max(m_foot - y, 0.0f);
        const int fit = static_cast<int>(std::floor(room / kListLine));
        const int n = static_cast<int>(list.lines.size());
        if (fit >= n) {
            block.shown = n;
        } else {
            block.shown = std::max(fit - 1, 0); // the last line that fits says how many more there are
            block.more = m_strings.format(Str::DetailMore, {{L"n", std::to_wstring(n - block.shown)}});
        }
        y += static_cast<float>(block.shown + (block.more.empty() ? 0 : 1)) * kListLine;
        m_lists.push_back(std::move(block));
    }
}

void DetailCard::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    // D-136: raised like the page card beside it (the side panels' look).
    canvas.raisedCard(b, ui::tokens::radius::r3, Color::BgPanel);
    const float x = b.x + kPad;
    const float width = std::max(b.width - 2 * kPad, 0.0f);
    if (m_model.empty()) {
        // Nothing selected: the page's icon, quietly, and what to do.
        float y = b.y + std::round(b.height * 0.36f) - kEmptyTile;
        const RectF tile{b.x + std::round((b.width - kEmptyTile) / 2), y, kEmptyTile, kEmptyTile};
        canvas.fillRoundRect(tile, ui::tokens::radius::r3, Color::BgRaised);
        canvas.drawIcon(m_emptyIcon, {tile.x + (kEmptyTile - size::iconLarge) / 2, tile.y + (kEmptyTile - size::iconLarge) / 2},
                        Color::TextTertiary, ui::IconVariant::Regular16, size::iconLarge);
        y += kEmptyTile + spacing::s5;
        canvas.drawText(m_emptyTitle, {x, y, width, 18}, TypeStyle::BodySemi, Color::TextSecondary, ui::TextAlign::Center);
        y += 18 + spacing::s2;
        canvas.drawTextWrapped(m_emptyBody, {x, y, width, 2 * kLine}, TypeStyle::Small, Color::TextTertiary, ui::TextAlign::Center);
        return;
    }
    canvas.pushClip({b.x, b.y, b.width, std::max(m_foot - b.y, 0.0f)});
    // Head: the tile, the name, the subtitle.
    const RectF tile{x, m_head.y, kTile, kTile};
    bool drawn = false;
    if (!m_model.iconFile.empty()) {
        drawn = canvas.drawFileIcon(m_model.iconFile, m_model.iconIndex, tile);
    }
    if (!drawn) {
        canvas.fillRoundRect(tile, ui::tokens::radius::r2, Color::AccentSubtle);
        if (!m_letter.empty()) {
            canvas.drawText(m_letter, tile, TypeStyle::Title, Color::AccentText, ui::TextAlign::Center);
        } else {
            canvas.drawIcon(m_model.icon, {tile.x + (kTile - size::icon) / 2, tile.y + (kTile - size::icon) / 2}, Color::AccentText);
        }
    }
    const float tx = x + kTile + kTileGap;
    const float tw = std::max(b.right() - kPad - tx, 0.0f);
    // Name (one or two lines) and subtitle, centred on the tile when they are shorter than it.
    const float nameBlock = m_titleH + (m_model.subtitle.empty() ? 0.0f : kLine);
    const float ty = m_head.y + std::max(std::round((kTile - nameBlock) / 2), 0.0f);
    canvas.pushClip({tx, ty, tw, m_titleH});
    canvas.drawTextWrapped(m_model.title, {tx, ty, tw, m_titleH + kTitleLine}, TypeStyle::BodySemi, Color::TextPrimary);
    canvas.popClip();
    if (!m_model.subtitle.empty()) {
        canvas.drawText(m_model.subtitle, {tx, ty + m_titleH, tw, kLine}, TypeStyle::Caption, Color::TextSecondary);
    }
    if (m_identity.height > 0) {
        canvas.drawText(m_model.identity, {x, m_identity.y, width, kLine}, TypeStyle::Mono, Color::TextTertiary);
    }
    if (m_state.height > 0) {
        const Color ink = inkOf(m_model.state.tone);
        canvas.drawIcon(m_model.state.icon, {x, m_state.y + (kStateH - size::icon) / 2}, ink);
        canvas.drawText(m_model.state.text, {x + size::icon + spacing::s3, m_state.y, width - size::icon - spacing::s3, kStateH},
                        TypeStyle::BodySemi, ink);
    }
    if (m_about.height > 0) {
        canvas.pushClip({x, m_about.y, width, m_about.height});
        canvas.drawTextWrapped(m_model.about, {x, m_about.y, width, m_about.height + kLine}, TypeStyle::Small, Color::TextSecondary);
        canvas.popClip();
    }
    if (!m_model.facts.empty()) {
        ui::FormView::paintSectionTitle(canvas, {x, m_factsTitle.y, width, kSectionAbove + kSectionLine + spacing::s4}, m_detailsTitle);
        for (std::size_t i = 0; i < m_model.facts.size() && i < m_facts.size(); ++i) {
            const auto& fact = m_model.facts[i];
            const float y = m_facts[i].y;
            canvas.drawText(fact.label, {x, y, kLabelW - spacing::s4, kFactH}, TypeStyle::Small, Color::TextTertiary);
            float vx = x + kLabelW;
            const Color ink = inkOf(fact.tone);
            if (fact.dot) {
                canvas.fillEllipse({vx + size::dot / 2, y + kFactH / 2}, size::dot / 2, ink);
                vx += size::dot + spacing::s3;
            }
            canvas.drawText(fact.value, {vx, y, std::max(x + width - vx, 0.0f), kFactH}, fact.mono ? TypeStyle::Mono : TypeStyle::Body,
                            fact.tone == DetailTone::Normal ? Color::TextPrimary : ink);
        }
    }
    for (std::size_t i = 0; i < m_model.notes.size() && i < m_notes.size(); ++i) {
        const auto& note = m_model.notes[i];
        const Block& block = m_notes[i];
        const Color ink = inkOf(note.tone);
        canvas.drawIcon(note.icon, {x, block.y}, note.tone == DetailTone::Normal ? Color::TextSecondary : ink);
        const float nx = x + size::icon + spacing::s3;
        canvas.drawTextWrapped(note.text, {nx, block.y, std::max(x + width - nx, 0.0f), block.height}, TypeStyle::Small,
                               note.tone == DetailTone::Normal || note.tone == DetailTone::Quiet ? Color::TextSecondary : ink);
    }
    for (std::size_t i = 0; i < m_model.lists.size() && i < m_lists.size(); ++i) {
        const auto& list = m_model.lists[i];
        const ListBlock& block = m_lists[i];
        ui::FormView::paintSectionTitle(canvas, {x, block.title.y, width, kSectionAbove + kSectionLine + spacing::s4}, list.title);
        float y = block.linesY;
        for (int n = 0; n < block.shown; ++n) {
            canvas.drawText(list.lines[static_cast<std::size_t>(n)], {x, y, width, kListLine}, list.mono ? TypeStyle::Mono : TypeStyle::Small,
                            Color::TextSecondary);
            y += kListLine;
        }
        if (!block.more.empty()) {
            canvas.drawText(block.more, {x, y, width, kListLine}, TypeStyle::Small, Color::TextTertiary);
        }
    }
    canvas.popClip();
}

} // namespace wl::app
