#pragma once
// D-145: a small Markdown reader for WinLove's release notes (GitHub's flavour, the part release notes
// use). Pure text in, a block tree out: no drawing, no Windows images, no network. What it knows:
//   blocks  — ATX (#..######) and setext headings, paragraphs (soft wraps join, two trailing spaces /
//             a backslash / <br> break), fenced (``` ~~~) and indented code, block quotes (nested),
//             bullet and numbered lists (nested, loose or tight), thematic breaks, GitHub tables,
//             HTML comments (dropped); any other HTML line is kept as plain text
//   inline  — **bold** __bold__, *italic* _italic_, ***both***, ~~strike~~, `code`, [links](url "t"),
//             <autolinks>, bare https:// and www. addresses, ![images](src) (alt text only), backslash
//             escapes, the common HTML entities
// Anything else is text: the reader never fails and never recurses deeper than kMaxDepth (a quote
// inside a list inside a quote …), so a hostile or broken body is shown as it is.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wl::markdown {

inline constexpr int kMaxDepth = 12;            // nesting of quotes / lists; deeper is read as text
inline constexpr std::size_t kMaxInput = 512 * 1024; // characters read (GitHub caps a body at 125 000)

enum Style : std::uint8_t {
    kBold = 1,
    kItalic = 2,
    kCode = 4,
    kStrike = 8,
    kLink = 16,
    kImage = 32, // an image's alt text (never fetched); `link` is its address
};

// A run of Inline::text with one style. Spans are in order and never overlap; text outside every span
// is plain.
struct Span {
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    std::uint8_t style = 0;
    int link = -1; // into Inline::links (kLink / kImage)
    bool operator==(const Span&) const = default;
};

struct Inline {
    std::wstring text; // as shown; '\n' is a hard line break
    std::vector<Span> spans;
    std::vector<std::wstring> links;
    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
    // The style at `position` of text (0 when plain).
    [[nodiscard]] std::uint8_t styleAt(std::size_t position) const noexcept;
};

enum class BlockKind : std::uint8_t { Paragraph, Heading, Code, Quote, List, Rule, Table };
enum class Align : std::uint8_t { Default, Left, Center, Right };

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    int level = 0;               // Heading: 1..6
    Inline text;                 // Paragraph, Heading
    std::wstring code;           // Code: its lines joined with '\n'
    std::wstring info;           // Code: the fence's first word ("cpp"), else empty
    std::vector<Block> children; // Quote: the blocks inside it
    bool ordered = false;        // List
    int start = 1;               // List: the first number
    std::vector<std::vector<Block>> items; // List: the blocks of each item
    std::vector<Align> align;              // Table: per column
    std::vector<std::vector<Inline>> rows; // Table: the header row first, every row as wide as it
};

struct Document {
    std::vector<Block> blocks;
};

[[nodiscard]] Document parse(std::wstring_view markdown);
// One paragraph's inline text (soft wraps already as '\n' between its lines).
[[nodiscard]] Inline parseInline(std::wstring_view text);
// The text of `in` without its styles (links keep their text; images their alt text).
[[nodiscard]] std::wstring plainText(const Inline& in);
// The key a heading and a "#fragment" link meet on (GitHub's anchors, folded: "## Türkçe" and
// "#türkçe" / "#t%C3%BCrk%C3%A7e" give the same key).
[[nodiscard]] std::wstring anchorKey(std::wstring_view text);

} // namespace wl::markdown
