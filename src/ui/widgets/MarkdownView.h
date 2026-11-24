#pragma once
// D-145: Markdown documents drawn, scrolled and selectable — the changelog window's body. Sections
// (one release each: a title, a line of facts, small tags, a Markdown body) sit on raised cards, a
// labelled line can come before a section ("Önceki sürümler"). What base/Markdown reads is drawn:
// headings, paragraphs (bold, italic, strike, `code`, links), code blocks, quotes, nested lists,
// rules, tables; an image shows its alt text only (nothing is fetched).
// Text can be selected with the mouse (drag; a double click takes the paragraph), Ctrl+A and Ctrl+C
// copy it. A link calls onLink with its address; a "#fragment" link scrolls to the heading of the same
// name in the same section instead. The owner decides what an address may open (the app: https only).
// Every text layout is made in layout() for the width; paint() draws what is there and never measures.
#include "base/Markdown.h"
#include "ui/render/Canvas.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::ui {

class ScrollBar;

class MarkdownView : public Widget {
public:
    struct Tag {
        std::wstring text;
        tokens::Color ink = tokens::Color::AccentText;
        tokens::Color fill = tokens::Color::AccentSubtle;
    };
    struct Section {
        std::wstring divider;   // non-empty: a labelled line above this section
        std::wstring title;     // "WinLove 1.4.0 Beta"
        std::wstring meta;      // "v1.4.0-beta · 2 gün önce"
        std::vector<Tag> tags;  // "Yeni", "Ön sürüm"
        markdown::Document body;
        std::wstring emptyBody; // said when the body has no blocks
    };

    MarkdownView();
    ~MarkdownView() override;

    std::function<void(const std::wstring& url)> onLink;
    void setSections(std::vector<Section> sections);
    void setPlaceholder(std::wstring text); // nothing to show (no sections)

    [[nodiscard]] float contentHeight() const noexcept { return m_content; }
    [[nodiscard]] float scrollOffset() const noexcept { return m_offset; }
    void scrollTo(float offset);
    void selectAll();
    void clearSelection();
    [[nodiscard]] std::wstring selectedText() const;
    // Tests: the runs of text as laid out, in reading order, and the link under a point.
    [[nodiscard]] std::size_t runCount() const noexcept { return m_runs.size(); }
    [[nodiscard]] const std::wstring& runText(std::size_t run) const { return m_runs[run].text; }
    [[nodiscard]] RectF runRect(std::size_t run) const; // in the widget's coordinates (scrolled)
    [[nodiscard]] std::optional<std::wstring> linkAt(PointF p) const;

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    bool onWheel(PointF p, float lines) override;
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onPointerUp(PointF p) override;
    void onDoubleClick() override;
    bool onKeyDown(const KeyEvent& key) override;
    [[nodiscard]] Cursor cursor() const override;

private:
    struct Link {
        std::uint32_t start = 0;
        std::uint32_t length = 0;
        std::wstring url;
    };
    struct Run {
        ComPtr<IDWriteTextLayout> layout;
        RectF rect; // content coordinates: x from the widget's left, y from the content's top
        tokens::Color ink = tokens::Color::TextPrimary;
        std::vector<Canvas::InkRange> inks; // links, images
        std::vector<std::pair<std::uint32_t, std::uint32_t>> code; // inline code: a fill behind each
        std::vector<Link> links;
        std::wstring text;
        int section = -1;
    };
    enum class BoxKind : std::uint8_t { Card, Fill, Line, Tag };
    struct Box {
        BoxKind kind = BoxKind::Fill;
        RectF rect;
        tokens::Color color = tokens::Color::LineSubtle;
        float radius = 0;
    };
    struct Anchor {
        int section = -1;
        std::wstring key;
        float y = 0;
    };
    struct Caret {
        int run = -1;
        std::uint32_t pos = 0;
        [[nodiscard]] bool operator<(const Caret& o) const noexcept { return run != o.run ? run < o.run : pos < o.pos; }
        [[nodiscard]] bool operator==(const Caret& o) const noexcept = default;
    };
    struct Context {
        tokens::Color ink = tokens::Color::TextPrimary;
        int listDepth = 0;
        bool tight = false; // inside a list item: smaller gaps
    };

    void rebuild(float width);
    float layoutBlocks(const std::vector<markdown::Block>& blocks, float x, float width, float y, const Context& ctx);
    float layoutTable(const markdown::Block& table, float x, float width, float y, const Context& ctx);
    // A run of `in` at (x, y), `width` wide; returns its height (0: nothing to draw).
    float addRun(const markdown::Inline& in, tokens::TypeStyle style, tokens::Color ink, float x, float y, float width,
                 TextAlign align = TextAlign::Leading, bool code = false);
    float addPlain(const std::wstring& text, tokens::TypeStyle style, tokens::Color ink, float x, float y, float width,
                   TextAlign align = TextAlign::Leading, bool code = false);
    [[nodiscard]] float viewportHeight() const noexcept;
    [[nodiscard]] PointF toContent(PointF p) const noexcept;
    [[nodiscard]] std::optional<Caret> caretAt(PointF p, bool nearest) const;
    [[nodiscard]] const Link* linkOf(const Caret& caret) const;
    void openLink(const std::wstring& url, int section);
    void syncBar();

    std::vector<Section> m_sections;
    std::wstring m_placeholder;
    std::vector<Run> m_runs;
    std::vector<Box> m_boxes;
    std::vector<Anchor> m_anchors;
    int m_section = -1; // the section being laid out
    float m_laidWidth = -1;
    float m_content = 0;
    float m_offset = 0;
    ScrollBar* m_bar = nullptr;
    // Selection.
    Caret m_anchor;
    Caret m_focus;
    bool m_dragging = false;
    PointF m_pressAt{};
    std::optional<std::wstring> m_pressLink;
    int m_pressSection = -1;
    Caret m_pressCaret;
    bool m_overLink = false;
    bool m_overText = false;
};

} // namespace wl::ui
