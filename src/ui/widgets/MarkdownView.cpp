#include "ui/widgets/MarkdownView.h"

#include "ui/platform/Clipboard.h"
#include "ui/widget/Host.h"
#include "ui/widgets/ScrollBar.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;
namespace spacing = tokens::spacing;
namespace md = markdown;

constexpr float kPad = spacing::s7;          // the view's edge ↔ a card
constexpr float kCardPad = spacing::s7;      // a card's edge ↔ its text
constexpr float kCardGap = spacing::s6;      // between cards
constexpr float kBlockGap = 10.0f;           // between blocks of a body
constexpr float kTightGap = 4.0f;            // between blocks of a list item, and between items
constexpr float kHeadingTop = 8.0f;          // more air above a heading than under it
constexpr float kQuoteIndent = 14.0f;
constexpr float kQuoteBar = 3.0f;
constexpr float kCodePad = 10.0f;
constexpr float kBulletWidth = 18.0f;
constexpr float kCellPadX = 10.0f;
constexpr float kCellPadY = 6.0f;
constexpr float kMinColumn = 56.0f;
constexpr float kTagHeight = 20.0f;
constexpr float kDividerHeight = 28.0f;
constexpr float kWheelStep = 22.0f;          // DIPs per wheel line
constexpr float kDragStart = 3.0f;

TypeStyle headingStyle(int level) {
    switch (level) {
    case 1: return TypeStyle::Display;
    case 2: return TypeStyle::Title;
    case 3: return TypeStyle::Panel;
    default: return TypeStyle::BodySemi;
    }
}

const wchar_t* bulletFor(int depth) {
    static constexpr const wchar_t* kBullets[] = {L"•", L"◦", L"▪"};
    return kBullets[std::min(depth, 2)];
}

TextAlign textAlignOf(md::Align align) {
    switch (align) {
    case md::Align::Center: return TextAlign::Center;
    case md::Align::Right: return TextAlign::Trailing;
    default: return TextAlign::Leading;
    }
}

} // namespace

MarkdownView::MarkdownView() {
    setFocusable(true);
    m_bar = &add<ScrollBar>();
    m_bar->onScroll = [this](float offset) { scrollTo(offset); };
    setAccessible(AccessRole::Text, L"");
}

MarkdownView::~MarkdownView() = default;

void MarkdownView::setSections(std::vector<Section> sections) {
    m_sections = std::move(sections);
    m_laidWidth = -1;
    m_offset = 0;
    clearSelection();
    layout();
    invalidate();
}

void MarkdownView::setPlaceholder(std::wstring text) {
    m_placeholder = std::move(text);
    invalidate();
}

float MarkdownView::viewportHeight() const noexcept {
    return bounds().height;
}

PointF MarkdownView::toContent(PointF p) const noexcept {
    return {p.x - bounds().x, p.y - bounds().y + m_offset};
}

RectF MarkdownView::runRect(std::size_t run) const {
    const RectF r = m_runs[run].rect;
    return {bounds().x + r.x, bounds().y + r.y - m_offset, r.width, r.height};
}

void MarkdownView::syncBar() {
    m_bar->setRange(m_content, viewportHeight());
    m_bar->setOffset(m_offset);
    m_bar->setVisible(m_bar->needed());
}

void MarkdownView::scrollTo(float offset) {
    const float most = std::max(m_content - viewportHeight(), 0.0f);
    const float clamped = std::clamp(std::round(offset), 0.0f, most);
    if (clamped == m_offset) {
        return;
    }
    m_offset = clamped;
    m_bar->setOffset(m_offset);
    invalidate();
}

void MarkdownView::layout() {
    const RectF b = bounds();
    m_bar->setBounds({b.right() - ScrollBar::kWidth, b.y, ScrollBar::kWidth, b.height});
    if (host() && b.width > 0 && b.width != m_laidWidth) {
        rebuild(b.width);
    }
    scrollTo(m_offset); // a taller window: less to scroll
    syncBar();
}

// ---- building -----------------------------------------------------------------------------------

float MarkdownView::addPlain(const std::wstring& text, TypeStyle style, Color ink, float x, float y, float width,
                             TextAlign align, bool code) {
    md::Inline in;
    in.text = text;
    return addRun(in, style, ink, x, y, width, align, code);
}

float MarkdownView::addRun(const md::Inline& in, TypeStyle style, Color ink, float x, float y, float width, TextAlign align,
                           bool code) {
    if (in.text.empty() || width < 8.0f || !host()) {
        return 0.0f;
    }
    const TextStyles& styles = host()->text();
    auto made = styles.wrappedLayout(in.text, code ? TypeStyle::Mono : style, width, align);
    if (!made) {
        return 0.0f;
    }
    ComPtr<IDWriteTextLayout> layout = std::move(*made);
    if (code) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER); // a long command line wraps where it must
    }
    Run run;
    run.ink = ink;
    run.section = m_section;
    run.text = in.text;
    // The mono family of the bundled collection, for `code` inside a paragraph.
    IDWriteTextFormat* mono = styles.format(TypeStyle::Mono);
    ComPtr<IDWriteFontCollection> monoCollection;
    wchar_t monoFamily[128] = {};
    float monoSize = 12.0f;
    if (mono) {
        mono->GetFontCollection(&monoCollection);
        mono->GetFontFamilyName(monoFamily, static_cast<UINT32>(std::size(monoFamily)));
        monoSize = mono->GetFontSize();
    }
    const auto& spec = TextStyles::spec(style);
    for (const auto& span : in.spans) {
        const DWRITE_TEXT_RANGE range{span.start, span.length};
        if (span.style & md::kBold) {
            layout->SetFontWeight(spec.weight >= 600 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
        }
        if (span.style & (md::kItalic | md::kImage)) {
            layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        }
        if (span.style & md::kStrike) {
            layout->SetStrikethrough(TRUE, range);
        }
        if ((span.style & md::kCode) && !code) {
            if (monoCollection) {
                layout->SetFontCollection(monoCollection.Get(), range);
                layout->SetFontFamilyName(monoFamily, range);
            }
            layout->SetFontSize(std::min(monoSize, spec.size), range);
            run.code.emplace_back(span.start, span.length);
        }
        if ((span.style & md::kLink) && span.link >= 0 && static_cast<std::size_t>(span.link) < in.links.size()) {
            layout->SetUnderline(TRUE, range);
            run.inks.push_back({span.start, span.length, Color::AccentText});
            run.links.push_back({span.start, span.length, in.links[static_cast<std::size_t>(span.link)]});
        } else if (span.style & md::kImage) {
            run.inks.push_back({span.start, span.length, Color::TextTertiary});
        }
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float height = std::max(std::ceil(metrics.height), spec.lineHeight);
    run.layout = std::move(layout);
    run.rect = {x, y, width, height};
    m_runs.push_back(std::move(run));
    return height;
}

void MarkdownView::rebuild(float width) {
    m_laidWidth = width;
    m_runs.clear();
    m_boxes.clear();
    m_anchors.clear();
    clearSelection();
    float y = kPad;
    const float cardX = kPad;
    const float cardW = std::max(width - 2 * kPad - spacing::s2, 120.0f); // the scroll bar's room on the right
    const float textX = cardX + kCardPad;
    const float textW = cardW - 2 * kCardPad;
    for (std::size_t i = 0; i < m_sections.size(); ++i) {
        const Section& s = m_sections[i];
        m_section = static_cast<int>(i);
        if (!s.divider.empty()) {
            // "Önceki sürümler ────────": the label, then a line to the edge.
            const float labelH = addPlain(s.divider, TypeStyle::Caption, Color::TextTertiary, cardX, y + 6, cardW);
            float labelW = 0;
            if (labelH > 0) {
                DWRITE_TEXT_METRICS m{};
                m_runs.back().layout->GetMetrics(&m);
                labelW = std::ceil(m.widthIncludingTrailingWhitespace);
                m_runs.back().rect.width = labelW + 1;
            }
            m_boxes.push_back({BoxKind::Line, {cardX + labelW + 12, y + 6 + std::round(labelH / 2), std::max(cardW - labelW - 12, 0.0f), 0},
                               Color::LineSubtle, 0});
            y += kDividerHeight;
        }
        const std::size_t cardBox = m_boxes.size();
        m_boxes.push_back({BoxKind::Card, {cardX, y, cardW, 0}, Color::BgPanel, tokens::radius::r3});
        float cy = y + kCardPad;
        // The title, its tags after it on the same line, the facts under it.
        const float titleH = addPlain(s.title, TypeStyle::Title, Color::TextPrimary, textX, cy, textW);
        float tagX = textX;
        if (titleH > 0) {
            DWRITE_TEXT_METRICS m{};
            m_runs.back().layout->GetMetrics(&m);
            tagX = textX + std::ceil(m.widthIncludingTrailingWhitespace) + 10;
            if (m.lineCount > 1) {
                tagX = textX + textW; // a wrapped title: tags under it would crowd it; they go
            }
        }
        for (const auto& tag : s.tags) {
            const float w = std::ceil(host()->text().measure(tag.text, TypeStyle::Badge)) + 14;
            if (tagX + w > textX + textW) {
                break;
            }
            const RectF pill{tagX, cy + std::round((tokens::kTypeStyles[static_cast<std::size_t>(TypeStyle::Title)].lineHeight - kTagHeight) / 2),
                             w, kTagHeight};
            m_boxes.push_back({BoxKind::Tag, pill, tag.fill, kTagHeight / 2});
            if (addPlain(tag.text, TypeStyle::Badge, tag.ink, pill.x, pill.y + 3, pill.width, TextAlign::Center) > 0) {
                m_runs.back().section = -1; // a tag is no text to select
            }
            tagX += w + 6;
        }
        cy += std::max(titleH, 20.0f) + 2;
        if (!s.meta.empty()) {
            cy += addPlain(s.meta, TypeStyle::Caption, Color::TextSecondary, textX, cy, textW);
        }
        cy += spacing::s5;
        m_boxes.push_back({BoxKind::Line, {textX, cy, textW, 0}, Color::LineSubtle, 0});
        cy += spacing::s5 + 1;
        if (s.body.blocks.empty()) {
            cy += addPlain(s.emptyBody, TypeStyle::Body, Color::TextTertiary, textX, cy, textW);
        } else {
            cy = layoutBlocks(s.body.blocks, textX, textW, cy, Context{});
        }
        cy += kCardPad;
        m_boxes[cardBox].rect.height = cy - y;
        y = cy + kCardGap;
    }
    m_section = -1;
    m_content = m_sections.empty() ? 0.0f : y - kCardGap + kPad;
}

float MarkdownView::layoutBlocks(const std::vector<md::Block>& blocks, float x, float width, float y, const Context& ctx) {
    const float gap = ctx.tight ? kTightGap : kBlockGap;
    bool first = true;
    for (const auto& block : blocks) {
        if (!first) {
            y += gap;
        }
        switch (block.kind) {
        case md::BlockKind::Paragraph:
            y += addRun(block.text, TypeStyle::Body, ctx.ink, x, y, width);
            break;
        case md::BlockKind::Heading: {
            if (!first) {
                y += kHeadingTop;
            }
            m_anchors.push_back({m_section, md::anchorKey(block.text.text), y});
            y += addRun(block.text, headingStyle(block.level), ctx.ink == Color::TextPrimary ? Color::TextPrimary : ctx.ink, x, y, width);
            if (block.level <= 2) {
                y += 4;
                m_boxes.push_back({BoxKind::Line, {x, y, width, 0}, Color::LineSubtle, 0});
                y += 3;
            }
            break;
        }
        case md::BlockKind::Code: {
            const std::size_t box = m_boxes.size();
            m_boxes.push_back({BoxKind::Fill, {x, y, width, 0}, Color::BgRaised, tokens::radius::r2});
            float h = addPlain(block.code, TypeStyle::Mono, Color::TextPrimary, x + kCodePad, y + kCodePad, width - 2 * kCodePad,
                               TextAlign::Leading, /*code=*/true);
            if (block.code.empty()) {
                h = tokens::kTypeStyles[static_cast<std::size_t>(TypeStyle::Mono)].lineHeight;
            }
            m_boxes[box].rect.height = h + 2 * kCodePad;
            y += h + 2 * kCodePad;
            break;
        }
        case md::BlockKind::Quote: {
            const float top = y;
            Context inner = ctx;
            inner.ink = Color::TextSecondary;
            const float bottom = block.children.empty() ? y + 18.0f
                                                       : layoutBlocks(block.children, x + kQuoteIndent, width - kQuoteIndent, y, inner);
            m_boxes.push_back({BoxKind::Fill, {x, top, kQuoteBar, bottom - top}, Color::LineStrong, kQuoteBar / 2});
            y = bottom;
            break;
        }
        case md::BlockKind::List: {
            // The marks' column: a bullet, or the widest number of this list.
            float markW = kBulletWidth;
            if (block.ordered && host()) {
                const int last = block.start + static_cast<int>(block.items.size()) - 1;
                markW = std::ceil(host()->text().measure(std::to_wstring(std::max(last, block.start)) + L".", TypeStyle::Body)) + 8;
            }
            Context inner = ctx;
            inner.listDepth = ctx.listDepth + 1;
            inner.tight = true;
            for (std::size_t i = 0; i < block.items.size(); ++i) {
                if (i > 0) {
                    y += kTightGap;
                }
                const std::wstring mark = block.ordered ? std::to_wstring(block.start + static_cast<int>(i)) + L"."
                                                        : std::wstring(bulletFor(ctx.listDepth));
                const float markH = addPlain(mark, TypeStyle::Body, block.ordered ? Color::TextSecondary : Color::TextTertiary, x, y,
                                             block.ordered ? markW - 6 : markW, block.ordered ? TextAlign::Trailing : TextAlign::Leading);
                if (markH > 0) {
                    m_runs.back().section = -1; // a mark is not copied with the text
                }
                const auto& item = block.items[i];
                const float bottom = item.empty() ? y + markH : layoutBlocks(item, x + markW, width - markW, y, inner);
                y = std::max(bottom, y + markH);
            }
            break;
        }
        case md::BlockKind::Rule:
            y += 6;
            m_boxes.push_back({BoxKind::Line, {x, y, width, 0}, Color::LineSubtle, 0});
            y += 7;
            break;
        case md::BlockKind::Table: y = layoutTable(block, x, width, y, ctx); break;
        }
        first = false;
    }
    return y;
}

float MarkdownView::layoutTable(const md::Block& table, float x, float width, float y, const Context& ctx) {
    const std::size_t columns = table.align.size();
    if (columns == 0 || table.rows.empty() || !host()) {
        return y;
    }
    // Each column as wide as its widest cell on one line, shrunk together when they do not fit.
    std::vector<float> widths(columns, kMinColumn);
    for (const auto& row : table.rows) {
        for (std::size_t c = 0; c < columns && c < row.size(); ++c) {
            const float w = std::ceil(host()->text().measure(row[c].text.substr(0, 400), TypeStyle::Body)) + 2 * kCellPadX + 2;
            widths[c] = std::max(widths[c], w);
        }
    }
    float total = 0;
    for (const float w : widths) {
        total += w;
    }
    if (total > width) {
        const float scale = width / total;
        total = 0;
        for (auto& w : widths) {
            w = std::max(std::floor(w * scale), std::min(kMinColumn, width / static_cast<float>(columns)));
            total += w;
        }
    }
    const float tableW = std::min(total, width);
    const float top = y;
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        const auto& row = table.rows[r];
        const bool header = r == 0;
        const std::size_t headerBox = m_boxes.size();
        if (header) {
            m_boxes.push_back({BoxKind::Fill, {x, y, tableW, 0}, Color::BgHover, tokens::radius::r1});
        }
        float rowH = 18.0f;
        float cx = x;
        for (std::size_t c = 0; c < columns; ++c) {
            if (c < row.size()) {
                const float h = addRun(row[c], header ? TypeStyle::BodySemi : TypeStyle::Body, ctx.ink, cx + kCellPadX, y + kCellPadY,
                                       widths[c] - 2 * kCellPadX, textAlignOf(table.align[c]));
                rowH = std::max(rowH, h);
            }
            cx += widths[c];
        }
        rowH += 2 * kCellPadY;
        if (header) {
            m_boxes[headerBox].rect.height = rowH;
        }
        y += rowH;
        if (r + 1 < table.rows.size()) {
            m_boxes.push_back({BoxKind::Line, {x, y, tableW, 0}, Color::LineSubtle, 0});
        }
    }
    // The columns' lines and the frame.
    float cx = x;
    for (std::size_t c = 0; c + 1 < columns; ++c) {
        cx += widths[c];
        m_boxes.push_back({BoxKind::Line, {cx, top, 0, y - top}, Color::LineSubtle, 0});
    }
    m_boxes.push_back({BoxKind::Fill, {x, top, tableW, y - top}, Color::LineSubtle, -1.0f}); // radius < 0: a frame
    return y;
}

// ---- painting -----------------------------------------------------------------------------------

void MarkdownView::paint(Canvas& canvas) {
    const RectF b = bounds();
    if (m_sections.empty()) {
        if (!m_placeholder.empty()) {
            canvas.drawText(m_placeholder, {b.x, b.y, b.width, std::min(b.height, 120.0f)}, TypeStyle::Body, Color::TextTertiary,
                            TextAlign::Center);
        }
        return;
    }
    canvas.pushClip(b);
    const float top = m_offset;
    const float bottom = m_offset + b.height;
    auto place = [&](RectF r) { return RectF{b.x + r.x, b.y + r.y - m_offset, r.width, r.height}; };
    auto visible = [&](RectF r) { return r.y + r.height >= top - 2 && r.y <= bottom + 2; };
    // Cards first, then fills and lines, then text on top.
    for (const auto& box : m_boxes) {
        if (box.kind == BoxKind::Card && visible(box.rect)) {
            canvas.raisedCard(place(box.rect), box.radius, box.color);
        }
    }
    for (const auto& box : m_boxes) {
        if (box.kind == BoxKind::Card || !visible(box.rect)) {
            continue;
        }
        const RectF r = place(box.rect);
        if (box.kind == BoxKind::Line) {
            if (box.rect.height <= 0) {
                canvas.hairlineH(r.x, r.y, r.width, box.color);
            } else {
                canvas.hairlineV(r.x, r.y, r.height, box.color);
            }
        } else if (box.radius < 0) {
            canvas.strokeRoundRect(r, tokens::radius::r1, box.color);
        } else {
            canvas.fillRoundRect(r, box.radius, box.color);
        }
    }
    const Caret from = std::min(m_anchor, m_focus);
    const Caret to = std::max(m_anchor, m_focus);
    const bool selection = from.run >= 0 && !(from == to);
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    auto fillRange = [&](const Run& run, std::uint32_t start, std::uint32_t length, Color color, float grow) {
        UINT32 count = 0;
        run.layout->HitTestTextRange(start, length, 0, 0, nullptr, 0, &count);
        if (count == 0) {
            return;
        }
        hits.resize(count);
        if (FAILED(run.layout->HitTestTextRange(start, length, 0, 0, hits.data(), count, &count))) {
            return;
        }
        const RectF at = place(run.rect);
        for (UINT32 i = 0; i < count; ++i) {
            const auto& h = hits[i];
            canvas.fillRoundRect({at.x + h.left - grow, at.y + h.top + 1, h.width + 2 * grow, h.height - 2}, tokens::radius::r1, color);
        }
    };
    for (std::size_t i = 0; i < m_runs.size(); ++i) {
        const Run& run = m_runs[i];
        if (!run.layout || !visible(run.rect)) {
            continue;
        }
        for (const auto& [start, length] : run.code) {
            fillRange(run, start, length, Color::BgHover, 2.0f);
        }
        if (selection && static_cast<int>(i) >= from.run && static_cast<int>(i) <= to.run && run.section >= 0) {
            const std::uint32_t s = static_cast<int>(i) == from.run ? from.pos : 0;
            const std::uint32_t e = static_cast<int>(i) == to.run ? to.pos : static_cast<std::uint32_t>(run.text.size());
            if (e > s) {
                fillRange(run, s, e - s, Color::AccentSubtle, 0.0f);
            }
        }
        const RectF at = place(run.rect);
        canvas.drawTextLayout(run.layout.Get(), {at.x, at.y}, run.ink, run.inks);
    }
    canvas.popClip();
}

// ---- input --------------------------------------------------------------------------------------

std::optional<MarkdownView::Caret> MarkdownView::caretAt(PointF p, bool nearest) const {
    const PointF c = toContent(p);
    int best = -1;
    float bestDistance = 1e9f;
    for (std::size_t i = 0; i < m_runs.size(); ++i) {
        const Run& run = m_runs[i];
        if (!run.layout || run.section < 0) {
            continue;
        }
        const RectF& r = run.rect;
        const bool insideY = c.y >= r.y && c.y < r.y + r.height;
        if (insideY && c.x >= r.x - 4 && c.x <= r.x + r.width + 4) {
            best = static_cast<int>(i);
            bestDistance = 0;
            break;
        }
        if (nearest) {
            const float dy = c.y < r.y ? r.y - c.y : c.y >= r.y + r.height ? c.y - (r.y + r.height) : 0.0f;
            const float dx = c.x < r.x ? r.x - c.x : c.x > r.x + r.width ? c.x - (r.x + r.width) : 0.0f;
            const float d = dy * 4 + dx; // rows matter more than columns
            if (d < bestDistance) {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
    }
    if (best < 0) {
        return std::nullopt;
    }
    const Run& run = m_runs[static_cast<std::size_t>(best)];
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    run.layout->HitTestPoint(c.x - run.rect.x, std::clamp(c.y - run.rect.y, 0.0f, std::max(run.rect.height - 1, 0.0f)), &trailing, &inside,
                             &hit);
    Caret caret{best, hit.textPosition + (trailing ? hit.length : 0)};
    caret.pos = std::min<std::uint32_t>(caret.pos, static_cast<std::uint32_t>(run.text.size()));
    if (!nearest && !inside) {
        // Beside the text on its line: still this run, for a click on a link.
        caret.pos = hit.textPosition;
    }
    return caret;
}

const MarkdownView::Link* MarkdownView::linkOf(const Caret& caret) const {
    if (caret.run < 0) {
        return nullptr;
    }
    for (const auto& link : m_runs[static_cast<std::size_t>(caret.run)].links) {
        if (caret.pos >= link.start && caret.pos < link.start + link.length) {
            return &link;
        }
    }
    return nullptr;
}

std::optional<std::wstring> MarkdownView::linkAt(PointF p) const {
    const auto caret = caretAt(p, false);
    if (!caret) {
        return std::nullopt;
    }
    // The character under the point (HitTestPoint's leading edge), not the caret after it.
    const Run& run = m_runs[static_cast<std::size_t>(caret->run)];
    const PointF c = toContent(p);
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    run.layout->HitTestPoint(c.x - run.rect.x, c.y - run.rect.y, &trailing, &inside, &hit);
    if (!inside) {
        return std::nullopt;
    }
    if (const Link* link = linkOf({caret->run, hit.textPosition})) {
        return link->url;
    }
    return std::nullopt;
}

void MarkdownView::openLink(const std::wstring& url, int section) {
    if (url.starts_with(L"#")) {
        // A heading of the same document: the clicked section's first, else any.
        const std::wstring key = md::anchorKey(url);
        const Anchor* found = nullptr;
        for (const auto& a : m_anchors) {
            if (a.key == key && (a.section == section || !found)) {
                found = &a;
                if (a.section == section) {
                    break;
                }
            }
        }
        if (found) {
            scrollTo(found->y - kPad);
        }
        return;
    }
    if (onLink) {
        onLink(url);
    }
}

bool MarkdownView::onWheel(PointF /*p*/, float lines) {
    if (!m_bar->needed()) {
        return false;
    }
    scrollTo(m_offset - lines * kWheelStep);
    return true;
}

void MarkdownView::onPointerDown(PointF p) {
    m_pressAt = p;
    m_dragging = false;
    m_pressLink = linkAt(p);
    const auto caret = caretAt(p, false);
    m_pressSection = caret ? m_runs[static_cast<std::size_t>(caret->run)].section : -1;
    m_pressCaret = caretAt(p, true).value_or(Caret{});
    if (!m_pressLink) {
        m_anchor = m_focus = m_pressCaret;
        invalidate();
    }
    if (host()) {
        host()->setFocus(this, false);
    }
}

void MarkdownView::onPointerMove(PointF p) {
    if (captured()) {
        if (!m_dragging && (std::abs(p.x - m_pressAt.x) > kDragStart || std::abs(p.y - m_pressAt.y) > kDragStart)) {
            m_dragging = true;
            m_anchor = m_pressCaret; // a drag that began on a link selects too
        }
        if (m_dragging) {
            // Past the top or bottom edge: the view scrolls with the drag.
            const RectF b = bounds();
            if (p.y < b.y) {
                scrollTo(m_offset - (b.y - p.y));
            } else if (p.y > b.bottom()) {
                scrollTo(m_offset + (p.y - b.bottom()));
            }
            if (const auto caret = caretAt(p, true); caret && !(*caret == m_focus)) {
                m_focus = *caret;
                invalidate();
            }
        }
        return;
    }
    const bool overLink = linkAt(p).has_value();
    const bool overText = caretAt(p, false).has_value();
    if (overLink != m_overLink || overText != m_overText) {
        m_overLink = overLink;
        m_overText = overText;
        invalidate();
    }
}

void MarkdownView::onPointerUp(PointF p) {
    if (!m_dragging && m_pressLink && linkAt(p) == m_pressLink) {
        const std::wstring url = *m_pressLink;
        m_pressLink.reset();
        openLink(url, m_pressSection);
        return;
    }
    m_pressLink.reset();
    m_dragging = false;
}

void MarkdownView::onDoubleClick() {
    // The paragraph (run) under the press.
    if (m_pressCaret.run >= 0) {
        m_anchor = {m_pressCaret.run, 0};
        m_focus = {m_pressCaret.run, static_cast<std::uint32_t>(m_runs[static_cast<std::size_t>(m_pressCaret.run)].text.size())};
        invalidate();
    }
}

bool MarkdownView::onKeyDown(const KeyEvent& key) {
    if (key.ctrl && (key.virtualKey == 'C' || key.virtualKey == VK_INSERT)) {
        const std::wstring text = selectedText();
        if (!text.empty()) {
            setClipboardText(text);
        }
        return true;
    }
    if (key.ctrl && key.virtualKey == 'A') {
        selectAll();
        return true;
    }
    const float page = std::max(viewportHeight() - 60.0f, 60.0f);
    switch (key.virtualKey) {
    case VK_DOWN: scrollTo(m_offset + 3 * kWheelStep); return true;
    case VK_UP: scrollTo(m_offset - 3 * kWheelStep); return true;
    case VK_NEXT:
    case VK_SPACE: scrollTo(m_offset + page); return true;
    case VK_PRIOR: scrollTo(m_offset - page); return true;
    case VK_HOME: scrollTo(0); return true;
    case VK_END: scrollTo(m_content); return true;
    default: return false;
    }
}

Cursor MarkdownView::cursor() const {
    return m_overLink ? Cursor::Hand : m_overText ? Cursor::IBeam : Cursor::Arrow;
}

void MarkdownView::selectAll() {
    int first = -1;
    int last = -1;
    for (std::size_t i = 0; i < m_runs.size(); ++i) {
        if (m_runs[i].section >= 0) {
            first = first < 0 ? static_cast<int>(i) : first;
            last = static_cast<int>(i);
        }
    }
    if (first < 0) {
        return;
    }
    m_anchor = {first, 0};
    m_focus = {last, static_cast<std::uint32_t>(m_runs[static_cast<std::size_t>(last)].text.size())};
    invalidate();
}

void MarkdownView::clearSelection() {
    m_anchor = m_focus = Caret{};
}

std::wstring MarkdownView::selectedText() const {
    const Caret from = std::min(m_anchor, m_focus);
    const Caret to = std::max(m_anchor, m_focus);
    if (from.run < 0 || from == to) {
        return {};
    }
    std::wstring out;
    for (int i = from.run; i <= to.run && i < static_cast<int>(m_runs.size()); ++i) {
        const Run& run = m_runs[static_cast<std::size_t>(i)];
        if (run.section < 0) {
            continue; // marks and tags
        }
        const std::size_t s = i == from.run ? from.pos : 0;
        const std::size_t e = i == to.run ? to.pos : run.text.size();
        if (e <= s) {
            continue;
        }
        if (!out.empty()) {
            out += L"\r\n";
        }
        out += run.text.substr(s, e - s);
    }
    return out;
}

} // namespace wl::ui
