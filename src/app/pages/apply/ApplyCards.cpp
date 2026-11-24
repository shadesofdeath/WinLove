#include "app/pages/apply/ApplyCards.h"

#include "app/pages/PageBits.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::icons::Icon;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace spacing = ui::tokens::spacing;

namespace {

constexpr float kPad = spacing::s6;
constexpr float kIconTile = 40.0f;
constexpr float kStrip = 3.0f; // the status strip on a card's left edge (as the impact card's)

void paintStrip(ui::Canvas& canvas, RectF card, Color ink) {
    canvas.fillRoundRect({card.x + 1, card.y + 10, kStrip, std::max(card.height - 20, 0.0f)}, 1.5f, ink);
}

// "● 5 tamam   ● 1 atlandı   ● 2 bekliyor" from x; returns where it ended.
float paintCounts(ui::Canvas& canvas, float x, float y, float height, const std::array<DotCount, 3>& counts) {
    for (const auto& count : counts) {
        if (count.text.empty()) {
            continue;
        }
        canvas.fillEllipse({x + 3.5f, y + height / 2}, 3.5f, count.dot);
        canvas.drawText(count.text, {x + 12, y, count.width + 1, height}, TypeStyle::Caption, Color::TextSecondary);
        x += 12 + count.width + 18;
    }
    return x;
}

} // namespace

// ---- PlanHeader -------------------------------------------------------------------------------

PlanHeader::PlanHeader() {
    m_apply = &add<ui::Button>(ui::ButtonKind::Primary, L"", Icon::ApplyPlay);
    m_apply->setHeight(32.0f);
    m_apply->onInvoke = [this] {
        if (auto run = onApply) {
            run();
        }
    };
}

void PlanHeader::set(Texts texts) {
    m_texts = std::move(texts);
    m_apply->setText(m_texts.action);
    setAccessible(ui::AccessRole::Group, m_texts.title);
    layout();
    invalidate();
}

void PlanHeader::layout() {
    const RectF b = bounds();
    const ui::SizeF size = m_apply->measure({});
    const RectF button{b.right() - kPad - size.width, b.y + std::round((b.height - size.height) / 2), size.width, size.height};
    m_apply->setBounds(button);
    const float room = std::max(button.x - 24 - (b.x + kPad), 0.0f);
    const float middle = std::clamp(std::floor(room * 0.38f), std::min(room, 200.0f), 300.0f);
    m_middle = {button.x - 24 - middle, b.y + 16, middle, b.height - 32};
    m_labelWidth = std::ceil(textWidth(m_texts.sizeLabel, TypeStyle::Caption));
}

void PlanHeader::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    const RectF tile{b.x + kPad, b.y + std::round((b.height - kIconTile) / 2), kIconTile, kIconTile};
    canvas.fillRoundRect(tile, ui::tokens::radius::r2, Color::AccentSubtle);
    canvas.drawIcon(Icon::ApplyPlay, {tile.x + 8, tile.y + 8}, Color::AccentBase, ui::IconVariant::Regular24);
    const float x = tile.right() + 14;
    const float width = std::max(m_middle.x - 24 - x, 0.0f);
    canvas.drawText(m_texts.title, {x, b.y + 14, width, 22}, TypeStyle::Title, Color::TextPrimary);
    canvas.drawText(m_texts.subtitle, {x, b.y + 40, width, 18}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_texts.note, {x, b.y + 58, width, 18}, TypeStyle::Caption, Color::TextTertiary);

    const RectF m = m_middle;
    canvas.hairlineV(m.x - 12, b.y + 14, b.height - 28, Color::LineSubtle);
    canvas.drawText(m_texts.sizeLabel, {m.x, m.y, m_labelWidth + 1, 20}, TypeStyle::Caption, Color::TextSecondary);
    const float vx = m.x + m_labelWidth + 10;
    canvas.drawText(m_texts.sizeValue, {vx, m.y, std::max(m.right() - vx, 0.0f), 20}, TypeStyle::BodySemi, Color::TextPrimary);
    paintSizeBar(canvas, {m.x, m.y + 27, m.width, 6}, m_texts.before, m_texts.after);
    canvas.drawText(m_texts.sizeDelta, {m.x, m.y + 40, m.width, 18}, TypeStyle::Caption, m_texts.deltaInk);
    canvas.drawText(m_texts.time, {m.x, m.y + 40, m.width, 18}, TypeStyle::Caption, Color::TextSecondary, ui::TextAlign::Trailing);
}

// ---- RunHeader --------------------------------------------------------------------------------

RunHeader::RunHeader(const Localization& strings) {
    m_folder = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::ApplyOpenFolder), Icon::OpenFolder);
    m_stop = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::ApplyStop), Icon::Stop);
    m_resume = &add<ui::Button>(ui::ButtonKind::Primary, strings.get(Str::ApplySaveContinue), Icon::Save);
    m_ring = &add<ui::ProgressRing>(ui::tokens::size::ringLarge);
    m_folder->onInvoke = [this] {
        if (auto run = onOpenFolder) {
            run();
        }
    };
    m_stop->onInvoke = [this] {
        if (auto run = onStop) {
            run();
        }
    };
    m_resume->onInvoke = [this] {
        if (auto run = onResume) {
            run();
        }
    };
}

void RunHeader::set(Texts texts) {
    m_texts = std::move(texts);
    m_ring->setRunning(!m_texts.icon);
    m_ring->setVisible(!m_texts.icon);
    m_ring->setInk(m_texts.ink);
    setAccessible(ui::AccessRole::Group, m_texts.title + L" " + m_texts.pct);
    layout();
    invalidate();
}

void RunHeader::setLine(std::wstring line) {
    if (line != m_texts.line) {
        m_texts.line = std::move(line);
        invalidate();
    }
}

void RunHeader::layout() {
    const RectF b = bounds();
    m_titleWidth = std::ceil(textWidth(m_texts.title, TypeStyle::Title));
    m_pctWidth = std::ceil(textWidth(m_texts.pct, TypeStyle::Display));
    for (auto& count : m_texts.counts) {
        count.width = std::ceil(textWidth(count.text, TypeStyle::Caption));
    }
    for (ui::Button* button : {m_folder, m_stop, m_resume}) {
        button->setVisible(m_texts.paused);
    }
    // D-142: the progress ring centred where the 16 DIP icon of the other states is.
    const float iconMid = ui::tokens::size::icon / 2;
    m_ring->setBounds(ui::ringBox({b.x + kPad + iconMid, b.y + 17 + iconMid}, ui::tokens::size::ringLarge));
    if (m_texts.paused) {
        // The choices on the right of the paused band, the save last (the way on).
        float x = b.right() - kPad;
        const float y = b.y + kHeight + std::round((kPausedHeight - kHeight - ui::tokens::size::control) / 2);
        for (ui::Button* button : {m_resume, m_stop, m_folder}) {
            const ui::SizeF size = button->measure({});
            x -= size.width;
            button->setBounds({x, y, size.width, size.height});
            x -= spacing::s4;
        }
    }
}

void RunHeader::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    paintStrip(canvas, b, m_texts.ink);
    const float x = b.x + kPad;
    const float right = b.right() - kPad;
    if (m_texts.icon) {
        canvas.drawIcon(*m_texts.icon, {x, b.y + 17}, m_texts.ink);
    }
    const float titleX = x + ui::tokens::size::icon + 10;
    const float pctX = right - m_pctWidth;
    const float titleWidth = std::min(m_titleWidth + 1, std::max(pctX - 16 - titleX, 0.0f));
    canvas.drawText(m_texts.title, {titleX, b.y + 14, titleWidth, 22}, TypeStyle::Title, Color::TextPrimary);
    const float detailX = titleX + titleWidth + 10;
    if (!m_texts.detail.empty() && detailX < pctX - 16) {
        canvas.drawText(m_texts.detail, {detailX, b.y + 14, pctX - 16 - detailX, 22}, TypeStyle::Body, Color::TextSecondary);
    }
    canvas.drawText(m_texts.pct, {pctX, b.y + 12, m_pctWidth + 1, 26}, TypeStyle::Display, Color::TextPrimary);
    canvas.drawText(m_texts.line, {titleX, b.y + 40, std::max(right - titleX, 0.0f), 18}, TypeStyle::Caption, Color::TextSecondary);
    if (!m_texts.edition.empty()) {
        canvas.drawText(m_texts.edition, {titleX, b.y + 40, std::max(right - titleX, 0.0f), 18}, TypeStyle::Caption,
                        Color::TextSecondary, ui::TextAlign::Trailing);
    }
    paintMeter(canvas, {x, b.y + 66, right - x, 6}, m_texts.fraction, m_texts.paused ? ui::Ink(Color::StatusInfo) : ui::Ink(Color::AccentBase));
    paintCounts(canvas, x, b.y + 78, 18, m_texts.counts);
    if (m_texts.paused) {
        canvas.hairlineH(x, b.y + kHeight, right - x, Color::LineSubtle);
        const float textRight = m_folder->bounds().x - kPad;
        canvas.drawTextWrapped(m_texts.body, {x, b.y + kHeight + 12, std::max(textRight - x, 0.0f), kPausedHeight - kHeight - 20},
                               TypeStyle::Body, Color::TextSecondary);
    }
}

// ---- ResultCard -------------------------------------------------------------------------------

namespace {
constexpr float kResultTop = 14.0f;
constexpr float kResultTitle = 24.0f;
constexpr float kResultBodyGap = 6.0f;
constexpr float kResultStats = 48.0f;
constexpr float kResultGap = 14.0f;
constexpr float kResultBottom = 16.0f;
constexpr float kResultMeter = 22.0f; // a save / discard after the run: its meter and percentage
constexpr float kIsoBand = 36.0f;      // D-144: the ISO's line (a button's height and air)
constexpr float kIsoMeter = 14.0f;     // its meter under it while it builds
} // namespace

ResultCard::ResultCard(Texts texts, std::vector<Action> actions, std::vector<Action> isoActions) : m_texts(std::move(texts)) {
    setAccessible(ui::AccessRole::Group, m_texts.title);
    for (auto& action : actions) {
        auto& button = add<ui::Button>(action.primary ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary, std::move(action.label),
                                       action.icon);
        button.onInvoke = std::move(action.run);
        m_buttons.push_back(&button);
    }
    for (auto& action : isoActions) {
        auto& button = add<ui::Button>(action.primary ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary, std::move(action.label),
                                       action.icon);
        button.onInvoke = std::move(action.run);
        m_isoButtons.push_back(&button);
    }
    m_isoRing = &add<ui::ProgressRing>(ui::tokens::size::ring);
    m_isoRing->setRunning(m_texts.iso && m_texts.iso->running);
    m_isoRing->setVisible(m_texts.iso && m_texts.iso->running);
}

void ResultCard::setIsoProgress(std::wstring text, float fraction, std::wstring progressText) {
    if (!m_texts.iso) {
        return;
    }
    auto& iso = *m_texts.iso;
    if (iso.text == text && iso.fraction == fraction && iso.progressText == progressText) {
        return;
    }
    iso.text = std::move(text);
    iso.fraction = fraction;
    iso.progressText = std::move(progressText);
    m_isoPctWidth = std::ceil(textWidth(iso.progressText, TypeStyle::Caption));
    invalidate();
}

float ResultCard::isoBandHeight() const noexcept {
    if (!m_texts.iso) {
        return 0.0f;
    }
    return kIsoBand + (m_texts.iso->running ? kIsoMeter : 0.0f);
}

float ResultCard::isoBandTop() const {
    const RectF b = bounds();
    return b.y + kResultTop + kResultTitle + (m_bodyHeight > 0 ? kResultBodyGap + m_bodyHeight : 0.0f) +
           (m_texts.progress ? kResultMeter : 0.0f) + kResultGap / 2;
}

void ResultCard::setProgress(float fraction, std::wstring text) {
    if (m_texts.progress && *m_texts.progress == fraction && m_texts.progressText == text) {
        return;
    }
    m_texts.progress = fraction;
    m_texts.progressText = std::move(text);
    m_progressWidth = std::ceil(textWidth(m_texts.progressText, TypeStyle::Caption));
    invalidate();
}

float ResultCard::bodyHeight(float width) const {
    if (m_texts.body.empty() || !host()) {
        return m_texts.body.empty() ? 0.0f : 18.0f;
    }
    return std::min(host()->text().measureWrapped(m_texts.body, TypeStyle::Body, std::max(width, 1.0f)), 3 * 18.0f);
}

float ResultCard::preferredHeight(float width) const {
    const float body = bodyHeight(width - 2 * kPad - ui::tokens::size::icon - 10);
    return kResultTop + kResultTitle + (body > 0 ? kResultBodyGap + body : 0.0f) + (m_texts.progress ? kResultMeter : 0.0f) +
           (m_texts.iso ? kResultGap / 2 + isoBandHeight() : 0.0f) + kResultGap + kResultStats +
           (m_buttons.empty() ? 0.0f : kResultGap + ui::tokens::size::control) + kResultBottom;
}

void ResultCard::layout() {
    const RectF b = bounds();
    const float textX = b.x + kPad + ui::tokens::size::icon + 10;
    m_bodyHeight = bodyHeight(b.right() - kPad - textX);
    m_titleWidth = std::ceil(textWidth(m_texts.title, TypeStyle::Title));
    m_progressWidth = std::ceil(textWidth(m_texts.progressText, TypeStyle::Caption));
    for (auto& count : m_texts.counts) {
        count.width = std::ceil(textWidth(count.text, TypeStyle::Caption));
    }
    float x = textX;
    const float y = b.bottom() - kResultBottom - ui::tokens::size::control;
    for (ui::Button* button : m_buttons) {
        const ui::SizeF size = button->measure({});
        button->setBounds({x, y, size.width, size.height});
        x += size.width + spacing::s4;
    }
    // D-144: the ISO band's buttons at its right, its ring where its icon would be.
    if (m_texts.iso) {
        const float top = isoBandTop();
        float right = b.right() - kPad;
        for (auto it = m_isoButtons.rbegin(); it != m_isoButtons.rend(); ++it) {
            const ui::SizeF size = (*it)->measure({});
            right -= size.width;
            (*it)->setBounds({right, top + std::round((kIsoBand - size.height) / 2), size.width, size.height});
            right -= spacing::s4;
        }
        m_isoRing->setBounds(ui::ringBox({textX + ui::tokens::size::icon / 2, top + kIsoBand / 2}, ui::tokens::size::ring));
        m_isoPctWidth = std::ceil(textWidth(m_texts.iso->progressText, TypeStyle::Caption));
    }
}

void ResultCard::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r3, Color::BgRaised);
    paintStrip(canvas, b, m_texts.ink);
    const float x = b.x + kPad;
    const float right = b.right() - kPad;
    float y = b.y + kResultTop;
    canvas.drawIcon(m_texts.icon, {x, y + 4}, m_texts.ink);
    const float textX = x + ui::tokens::size::icon + 10;
    canvas.drawText(m_texts.title, {textX, y, std::min(m_titleWidth + 1, std::max(right - textX, 0.0f)), kResultTitle}, TypeStyle::Title,
                    Color::TextPrimary);
    if (textX + m_titleWidth + 24 < right) {
        canvas.drawText(m_texts.time, {textX + m_titleWidth + 24, y, right - textX - m_titleWidth - 24, kResultTitle}, TypeStyle::Caption,
                        Color::TextSecondary, ui::TextAlign::Trailing);
    }
    y += kResultTitle;
    if (m_bodyHeight > 0) {
        y += kResultBodyGap;
        canvas.drawTextWrapped(m_texts.body, {textX, y, std::max(right - textX, 0.0f), m_bodyHeight}, TypeStyle::Body,
                               Color::TextSecondary);
        y += m_bodyHeight;
    }
    if (m_texts.progress) {
        const float meterRight = right - (m_progressWidth > 0 ? m_progressWidth + 12 : 0.0f);
        paintMeter(canvas, {textX, y + 10, std::max(meterRight - textX, 0.0f), 6}, *m_texts.progress);
        if (m_progressWidth > 0) {
            canvas.drawText(m_texts.progressText, {meterRight + 12, y + 3, m_progressWidth + 1, 18}, TypeStyle::Caption,
                            Color::TextSecondary);
        }
        y += kResultMeter;
    }
    if (m_texts.iso) {
        // D-144: the ISO after the run — a hairline above it, its icon (the ring while it builds), the
        // line; the meter under it while it builds.
        const auto& iso = *m_texts.iso;
        const float top = isoBandTop();
        canvas.hairlineH(textX, top, right - textX, Color::LineSubtle);
        if (!iso.running) {
            canvas.drawIcon(iso.icon, {textX, top + std::round((kIsoBand - ui::tokens::size::icon) / 2)}, iso.ink);
        }
        const float lineX = textX + ui::tokens::size::icon + 8;
        const float buttonsLeft = m_isoButtons.empty() ? right : m_isoButtons.front()->bounds().x - 12;
        canvas.drawText(iso.text, {lineX, top, std::max(buttonsLeft - lineX, 0.0f), kIsoBand}, TypeStyle::Body, Color::TextPrimary);
        if (iso.running) {
            const float meterRight = buttonsLeft - (m_isoPctWidth > 0 ? m_isoPctWidth + 12 : 0.0f);
            paintMeter(canvas, {lineX, top + kIsoBand - 2, std::max(meterRight - lineX, 0.0f), 6}, iso.fraction);
            if (m_isoPctWidth > 0) {
                canvas.drawText(iso.progressText, {meterRight + 10, top + kIsoBand - 8, m_isoPctWidth + 1, 18}, TypeStyle::Caption,
                                Color::TextSecondary);
            }
        }
        y = top + isoBandHeight();
    }
    y += kResultGap;
    // Three columns, each under a hairline: the size before → after, the gain, the steps.
    const float width = right - textX;
    const float column = std::floor((width - 2 * 24) / 3);
    const float c1 = textX;
    const float c2 = c1 + column + 24;
    const float c3 = c2 + column + 24;
    for (const float cx : {c1, c2, c3}) {
        canvas.hairlineH(cx, y, column, Color::LineSubtle);
    }
    canvas.drawText(m_texts.sizeLabel, {c1, y + 6, column, 16}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_texts.sizeValue, {c1, y + 22, column, 20}, TypeStyle::BodySemi, Color::TextPrimary);
    if (m_texts.after > 0) {
        paintSizeBar(canvas, {c1, y + 44, column, 4}, m_texts.before, m_texts.after);
    }
    canvas.drawText(m_texts.gainLabel, {c2, y + 6, column, 16}, TypeStyle::Caption, Color::TextSecondary);
    canvas.drawText(m_texts.gainValue, {c2, y + 22, column, 22}, TypeStyle::Title, m_texts.gainInk);
    canvas.drawText(m_texts.stepsLabel, {c3, y + 6, column, 16}, TypeStyle::Caption, Color::TextSecondary);
    canvas.pushClip({c3, y + 22, column, 22});
    paintCounts(canvas, c3, y + 23, 20, m_texts.counts);
    canvas.popClip();
}

} // namespace wl::app
