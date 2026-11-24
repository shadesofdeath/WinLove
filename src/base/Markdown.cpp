#include "base/Markdown.h"

#include "base/Text.h"
#include "base/Utf8.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace wl::markdown {

namespace {

using Lines = std::vector<std::wstring>;

// ---- characters -------------------------------------------------------------------------------

bool isWs(wchar_t c) noexcept {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v' || c == 0x00A0 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool isAsciiPunct(wchar_t c) noexcept {
    return (c >= L'!' && c <= L'/') || (c >= L':' && c <= L'@') || (c >= L'[' && c <= L'`') || (c >= L'{' && c <= L'~');
}

bool isPunct(wchar_t c) noexcept {
    if (c < 0x80) {
        return isAsciiPunct(c);
    }
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_PUNCT) != 0;
}

bool isWordChar(wchar_t c) noexcept {
    if (c < 0x80) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
    }
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & (C1_ALPHA | C1_DIGIT)) != 0;
}

bool isBlank(std::wstring_view s) noexcept {
    return std::ranges::all_of(s, [](wchar_t c) { return c == L' ' || c == L'\t'; });
}

int indentOf(std::wstring_view s) noexcept {
    int n = 0;
    while (static_cast<std::size_t>(n) < s.size() && s[static_cast<std::size_t>(n)] == L' ') {
        ++n;
    }
    return n;
}

std::wstring_view dropIndent(std::wstring_view s, int n) noexcept {
    std::size_t i = 0;
    while (i < s.size() && static_cast<int>(i) < n && s[i] == L' ') {
        ++i;
    }
    return s.substr(i);
}

std::wstring_view trimLeft(std::wstring_view s) noexcept {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) {
        s.remove_prefix(1);
    }
    return s;
}

std::wstring_view trimRight(std::wstring_view s) noexcept {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) {
        s.remove_suffix(1);
    }
    return s;
}

std::wstring_view trimBoth(std::wstring_view s) noexcept {
    return trimRight(trimLeft(s));
}

// Tabs to spaces (a tab stop every 4 columns): the structure is read by columns.
std::wstring expandTabs(std::wstring_view line) {
    std::wstring out;
    out.reserve(line.size());
    for (const wchar_t c : line) {
        if (c == L'\t') {
            out.append(4 - out.size() % 4, L' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

Lines splitLines(std::wstring_view text) {
    Lines lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n' || text[i] == L'\r') {
            lines.push_back(expandTabs(text.substr(start, i - start)));
            if (i < text.size() && text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i; // CRLF is one break
            }
            start = i + 1;
        }
    }
    return lines;
}

// ---- block starts -----------------------------------------------------------------------------

bool isRule(std::wstring_view line) {
    if (indentOf(line) > 3) {
        return false;
    }
    wchar_t mark = 0;
    int count = 0;
    for (const wchar_t c : line) {
        if (c == L' ') {
            continue;
        }
        if (c != L'-' && c != L'*' && c != L'_') {
            return false;
        }
        if (mark != 0 && c != mark) {
            return false;
        }
        mark = c;
        ++count;
    }
    return count >= 3;
}

// "## Title ##" → 2 and "Title"; 0 when the line is no ATX heading.
int atxLevel(std::wstring_view line, std::wstring_view& content) {
    if (indentOf(line) > 3) {
        return 0;
    }
    std::wstring_view s = trimLeft(line);
    int level = 0;
    while (static_cast<std::size_t>(level) < s.size() && s[static_cast<std::size_t>(level)] == L'#') {
        ++level;
    }
    if (level < 1 || level > 6) {
        return 0;
    }
    const auto rest = s.substr(static_cast<std::size_t>(level));
    if (!rest.empty() && rest.front() != L' ') {
        return 0; // "#hashtag"
    }
    std::wstring_view text = trimBoth(rest);
    // A closing run of '#' after a space (or the whole text) goes.
    std::size_t end = text.size();
    while (end > 0 && text[end - 1] == L'#') {
        --end;
    }
    if (end == 0) {
        text = {};
    } else if (end < text.size() && text[end - 1] == L' ') {
        text = trimRight(text.substr(0, end));
    }
    content = text;
    return level;
}

struct Fence {
    wchar_t ch = 0;
    int length = 0;
    int indent = 0;
    std::wstring info;
};

std::optional<Fence> fenceOpen(std::wstring_view line) {
    const int indent = indentOf(line);
    if (indent > 3) {
        return std::nullopt;
    }
    const std::wstring_view s = line.substr(static_cast<std::size_t>(indent));
    if (s.empty() || (s.front() != L'`' && s.front() != L'~')) {
        return std::nullopt;
    }
    Fence f{s.front(), 0, indent, {}};
    while (static_cast<std::size_t>(f.length) < s.size() && s[static_cast<std::size_t>(f.length)] == f.ch) {
        ++f.length;
    }
    if (f.length < 3) {
        return std::nullopt;
    }
    const std::wstring_view info = trimBoth(s.substr(static_cast<std::size_t>(f.length)));
    if (f.ch == L'`' && info.find(L'`') != std::wstring_view::npos) {
        return std::nullopt; // "``` x `y`": inline code, not a fence
    }
    f.info = std::wstring(info.substr(0, info.find(L' ')));
    return f;
}

bool fenceCloses(std::wstring_view line, const Fence& f) {
    if (indentOf(line) > 3) {
        return false;
    }
    const std::wstring_view s = trimBoth(line);
    if (static_cast<int>(s.size()) < f.length) {
        return false;
    }
    return std::ranges::all_of(s, [&](wchar_t c) { return c == f.ch; });
}

struct Marker {
    bool ordered = false;
    wchar_t ch = 0;   // '-' '*' '+' or the ordered delimiter '.' ')'
    int number = 1;
    int width = 0;    // the item's content column
    bool empty = false;
};

std::optional<Marker> listMarker(std::wstring_view line) {
    const int indent = indentOf(line);
    if (indent > 3) {
        return std::nullopt;
    }
    std::size_t p = static_cast<std::size_t>(indent);
    Marker m;
    if (p < line.size() && (line[p] == L'-' || line[p] == L'*' || line[p] == L'+')) {
        m.ch = line[p];
        ++p;
    } else {
        const std::size_t digits = p;
        int number = 0;
        while (p < line.size() && line[p] >= L'0' && line[p] <= L'9' && p - digits < 9) {
            number = number * 10 + (line[p] - L'0');
            ++p;
        }
        if (p == digits || p >= line.size() || (line[p] != L'.' && line[p] != L')')) {
            return std::nullopt;
        }
        m.ordered = true;
        m.number = number;
        m.ch = line[p];
        ++p;
    }
    if (p == line.size() || isBlank(line.substr(p))) {
        m.empty = true;
        m.width = static_cast<int>(p) + 1;
        return m;
    }
    if (line[p] != L' ') {
        return std::nullopt; // "-x", "1.5"
    }
    std::size_t spaces = 0;
    while (p + spaces < line.size() && line[p + spaces] == L' ') {
        ++spaces;
    }
    m.width = static_cast<int>(p + (spaces > 4 ? 1 : spaces));
    return m;
}

bool sameList(const Marker& a, const Marker& b) noexcept {
    return a.ordered == b.ordered && a.ch == b.ch;
}

bool quoteLine(std::wstring_view line, std::wstring& content) {
    const int indent = indentOf(line);
    if (indent > 3 || static_cast<std::size_t>(indent) >= line.size() || line[static_cast<std::size_t>(indent)] != L'>') {
        return false;
    }
    std::wstring_view rest = line.substr(static_cast<std::size_t>(indent) + 1);
    if (!rest.empty() && rest.front() == L' ') {
        rest.remove_prefix(1);
    }
    content = std::wstring(rest);
    return true;
}

bool setextUnderline(std::wstring_view line, int& level) {
    if (indentOf(line) > 3) {
        return false;
    }
    const std::wstring_view s = trimBoth(line);
    if (s.empty() || (s.front() != L'=' && s.front() != L'-')) {
        return false;
    }
    if (!std::ranges::all_of(s, [&](wchar_t c) { return c == s.front(); })) {
        return false;
    }
    level = s.front() == L'=' ? 1 : 2;
    return true;
}

bool htmlComment(std::wstring_view line) {
    return indentOf(line) <= 3 && trimLeft(line).starts_with(L"<!--");
}

// A table row's cells: outer pipes dropped, "\|" kept as a pipe, a pipe inside `code` kept too.
std::vector<std::wstring> splitRow(std::wstring_view line) {
    std::wstring_view s = trimBoth(line);
    if (!s.empty() && s.front() == L'|') {
        s.remove_prefix(1);
    }
    if (!s.empty() && s.back() == L'|' && !(s.size() >= 2 && s[s.size() - 2] == L'\\')) {
        s.remove_suffix(1);
    }
    std::vector<std::wstring> cells;
    std::wstring cell;
    bool code = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'\\' && i + 1 < s.size() && s[i + 1] == L'|') {
            cell.push_back(L'|');
            ++i;
            continue;
        }
        if (c == L'`') {
            code = !code;
        }
        if (c == L'|' && !code) {
            cells.emplace_back(trimBoth(cell));
            cell.clear();
            continue;
        }
        cell.push_back(c);
    }
    cells.emplace_back(trimBoth(cell));
    return cells;
}

bool delimiterRow(std::wstring_view line, std::vector<Align>& align) {
    if (line.find(L'|') == std::wstring_view::npos && line.find(L'-') == std::wstring_view::npos) {
        return false;
    }
    if (indentOf(line) > 3) {
        return false;
    }
    align.clear();
    for (const auto& cell : splitRow(line)) {
        const std::wstring_view c = trimBoth(cell);
        if (c.empty()) {
            return false;
        }
        const bool left = c.front() == L':';
        const bool right = c.back() == L':';
        const std::wstring_view dashes = c.substr(left ? 1 : 0, c.size() - (left ? 1 : 0) - (right && c.size() > 1 ? 1 : 0));
        if (dashes.empty() || !std::ranges::all_of(dashes, [](wchar_t d) { return d == L'-'; })) {
            return false;
        }
        align.push_back(left && right ? Align::Center : right ? Align::Right : left ? Align::Left : Align::Default);
    }
    return !align.empty();
}

bool startsTable(const Lines& lines, std::size_t i, std::vector<Align>& align) {
    if (i + 1 >= lines.size() || lines[i].find(L'|') == std::wstring::npos || indentOf(lines[i]) > 3) {
        return false;
    }
    return delimiterRow(lines[i + 1], align) && splitRow(lines[i]).size() == align.size();
}

// A line that ends a paragraph without a blank line before it.
bool interruptsParagraph(std::wstring_view line) {
    std::wstring_view heading;
    std::wstring quote;
    if (isBlank(line) || atxLevel(line, heading) > 0 || fenceOpen(line) || quoteLine(line, quote) || isRule(line) ||
        htmlComment(line)) {
        return true;
    }
    // A list interrupts it unless the item is empty or numbered other than 1 (CommonMark).
    if (const auto m = listMarker(line)) {
        return !m->empty && (!m->ordered || m->number == 1);
    }
    return false;
}

Block paragraphOf(const Lines& lines, std::size_t from = 0) {
    std::wstring text;
    for (std::size_t i = from; i < lines.size(); ++i) {
        if (!text.empty()) {
            text.push_back(L'\n');
        }
        text += trimLeft(lines[i]);
    }
    Block b;
    b.text = parseInline(text);
    return b;
}

std::vector<Block> parseBlocks(const Lines& lines, int depth);

std::vector<Block> nested(const Lines& lines, int depth) {
    if (depth >= kMaxDepth) {
        // Deeper than any release note goes: the rest as one paragraph of text.
        Lines kept;
        for (const auto& l : lines) {
            if (!isBlank(l)) {
                kept.push_back(l);
            }
        }
        if (kept.empty()) {
            return {};
        }
        return {paragraphOf(kept)};
    }
    return parseBlocks(lines, depth + 1);
}

std::vector<Block> parseBlocks(const Lines& lines, int depth) {
    std::vector<Block> blocks;
    const std::size_t n = lines.size();
    std::size_t i = 0;
    while (i < n) {
        const std::wstring& line = lines[i];
        if (isBlank(line)) {
            ++i;
            continue;
        }
        // Indented code (four spaces; never in the middle of a paragraph: that case is handled there).
        if (indentOf(line) >= 4) {
            std::vector<std::wstring> code;
            while (i < n && (isBlank(lines[i]) || indentOf(lines[i]) >= 4)) {
                code.emplace_back(dropIndent(lines[i], 4));
                ++i;
            }
            while (!code.empty() && isBlank(code.back())) {
                code.pop_back();
            }
            Block b;
            b.kind = BlockKind::Code;
            for (std::size_t k = 0; k < code.size(); ++k) {
                b.code += (k ? L"\n" : L"") + code[k];
            }
            blocks.push_back(std::move(b));
            continue;
        }
        if (const auto fence = fenceOpen(line)) {
            Block b;
            b.kind = BlockKind::Code;
            b.info = fence->info;
            ++i;
            bool first = true;
            while (i < n && !fenceCloses(lines[i], *fence)) {
                b.code += (first ? L"" : L"\n") + std::wstring(dropIndent(lines[i], fence->indent));
                first = false;
                ++i;
            }
            if (i < n) {
                ++i; // the closing fence (an unclosed one runs to the end)
            }
            blocks.push_back(std::move(b));
            continue;
        }
        if (htmlComment(line)) {
            // Hidden in the rendered notes, as on GitHub.
            while (i < n && lines[i].find(L"-->") == std::wstring::npos) {
                ++i;
            }
            if (i < n) {
                ++i;
            }
            continue;
        }
        std::wstring_view headingText;
        if (const int level = atxLevel(line, headingText); level > 0) {
            Block b;
            b.kind = BlockKind::Heading;
            b.level = level;
            b.text = parseInline(headingText);
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }
        if (isRule(line)) {
            Block b;
            b.kind = BlockKind::Rule;
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }
        std::wstring quoted;
        if (quoteLine(line, quoted)) {
            Lines inner;
            bool paragraphOpen = false;
            while (i < n) {
                std::wstring content;
                if (quoteLine(lines[i], content)) {
                    paragraphOpen = !isBlank(content) && !fenceOpen(content);
                    inner.push_back(std::move(content));
                    ++i;
                } else if (paragraphOpen && !isBlank(lines[i]) && !interruptsParagraph(lines[i])) {
                    inner.emplace_back(trimLeft(lines[i])); // a lazy continuation of the quoted paragraph
                    ++i;
                } else {
                    break;
                }
            }
            Block b;
            b.kind = BlockKind::Quote;
            b.children = nested(inner, depth);
            blocks.push_back(std::move(b));
            continue;
        }
        if (const auto first = listMarker(line)) {
            Block list;
            list.kind = BlockKind::List;
            list.ordered = first->ordered;
            list.start = first->number;
            while (i < n) {
                const auto m = listMarker(lines[i]);
                if (!m || !sameList(*m, *first) || isRule(lines[i])) {
                    break;
                }
                Lines item;
                item.emplace_back(m->empty ? std::wstring() : lines[i].substr(std::min<std::size_t>(m->width, lines[i].size())));
                bool lastBlank = m->empty;
                std::size_t j = i + 1;
                while (j < n) {
                    const std::wstring& l = lines[j];
                    if (isBlank(l)) {
                        item.emplace_back();
                        lastBlank = true;
                        ++j;
                        continue;
                    }
                    if (indentOf(l) >= m->width) {
                        item.emplace_back(l.substr(static_cast<std::size_t>(m->width)));
                        lastBlank = false;
                        ++j;
                        continue;
                    }
                    std::wstring_view h;
                    std::wstring q;
                    if (lastBlank || listMarker(l) || atxLevel(l, h) > 0 || fenceOpen(l) || quoteLine(l, q) || isRule(l) ||
                        htmlComment(l)) {
                        break;
                    }
                    item.emplace_back(trimLeft(l)); // a lazy continuation of the item's paragraph
                    ++j;
                }
                while (!item.empty() && isBlank(item.back())) {
                    item.pop_back();
                }
                list.items.push_back(nested(item, depth));
                // Blank lines between items keep the list going; anything else after them ends it.
                std::size_t k = j;
                while (k < n && isBlank(lines[k])) {
                    ++k;
                }
                const auto next = k < n ? listMarker(lines[k]) : std::nullopt;
                if (next && sameList(*next, *first) && !isRule(lines[k])) {
                    i = k;
                } else {
                    i = j;
                    break;
                }
            }
            blocks.push_back(std::move(list));
            continue;
        }
        std::vector<Align> align;
        if (startsTable(lines, i, align)) {
            Block table;
            table.kind = BlockKind::Table;
            table.align = align;
            const std::size_t columns = align.size();
            auto row = [&](std::wstring_view l) {
                std::vector<Inline> cells;
                auto raw = splitRow(l);
                raw.resize(columns); // short rows are padded, long ones cut (GitHub does the same)
                for (const auto& cell : raw) {
                    cells.push_back(parseInline(cell));
                }
                table.rows.push_back(std::move(cells));
            };
            row(lines[i]);
            i += 2;
            while (i < n && !isBlank(lines[i]) && !interruptsParagraph(lines[i])) {
                row(lines[i]);
                ++i;
            }
            blocks.push_back(std::move(table));
            continue;
        }
        // A paragraph, or a setext heading when an underline follows it.
        Lines para;
        int setext = 0;
        while (i < n) {
            const std::wstring& l = lines[i];
            if (isBlank(l)) {
                break;
            }
            if (!para.empty()) {
                int level = 0;
                if (setextUnderline(l, level)) {
                    setext = level;
                    ++i;
                    break;
                }
                std::vector<Align> tableAlign;
                if (interruptsParagraph(l) || startsTable(lines, i, tableAlign)) {
                    break;
                }
            }
            para.push_back(l);
            ++i;
        }
        Block b = paragraphOf(para);
        if (setext > 0) {
            b.kind = BlockKind::Heading;
            b.level = setext;
        }
        if (!b.text.empty()) {
            blocks.push_back(std::move(b));
        }
    }
    return blocks;
}

// ---- inline -----------------------------------------------------------------------------------

struct Entity {
    const wchar_t* name;
    wchar_t ch;
};
constexpr std::array<Entity, 22> kEntities = {{
    {L"amp", L'&'},      {L"lt", L'<'},       {L"gt", L'>'},       {L"quot", L'"'},     {L"apos", L'\''},
    {L"nbsp", 0x00A0},   {L"copy", 0x00A9},   {L"reg", 0x00AE},    {L"trade", 0x2122},  {L"hellip", 0x2026},
    {L"mdash", 0x2014},  {L"ndash", 0x2013},  {L"laquo", 0x00AB},  {L"raquo", 0x00BB},  {L"bull", 0x2022},
    {L"middot", 0x00B7}, {L"rarr", 0x2192},   {L"larr", 0x2190},   {L"times", 0x00D7},  {L"deg", 0x00B0},
    {L"lsquo", 0x2018},  {L"rsquo", 0x2019},
}};

class InlineParser {
public:
    explicit InlineParser(std::wstring_view source, int depth = 0) : m_s(source), m_depth(depth) {}

    Inline run() {
        scan();
        resolveEmphasis();
        return flatten();
    }

private:
    struct Node {
        std::wstring text;
        std::uint8_t style = 0;
        int link = -1;
        wchar_t delim = 0; // a run of '*', '_' or '~' still to be matched
        int count = 0;
        int original = 0;
        bool canOpen = false;
        bool canClose = false;
        bool active = true;
    };

    void add(std::wstring_view text, std::uint8_t style = 0, int link = -1) {
        if (text.empty()) {
            return;
        }
        if (!m_nodes.empty() && m_nodes.back().delim == 0 && m_nodes.back().style == style && m_nodes.back().link == link) {
            m_nodes.back().text += text;
            return;
        }
        Node node;
        node.text = std::wstring(text);
        node.style = style;
        node.link = link;
        m_nodes.push_back(std::move(node));
    }

    int addLink(std::wstring url) {
        m_links.push_back(std::move(url));
        return static_cast<int>(m_links.size()) - 1;
    }

    [[nodiscard]] wchar_t at(std::size_t i) const noexcept { return i < m_s.size() ? m_s[i] : L'\0'; }

    // The trailing spaces of the text so far (they go at a line break).
    std::size_t trimTrailingSpaces() {
        std::size_t removed = 0;
        while (!m_nodes.empty() && m_nodes.back().delim == 0) {
            auto& t = m_nodes.back().text;
            while (!t.empty() && t.back() == L' ') {
                t.pop_back();
                ++removed;
            }
            if (!t.empty()) {
                break;
            }
            m_nodes.pop_back();
        }
        return removed;
    }

    void skipSpaces(std::size_t& i) const noexcept {
        while (i < m_s.size() && (m_s[i] == L' ' || m_s[i] == L'\t')) {
            ++i;
        }
    }

    // `pos` is at '[' (after '!' for an image). On success `end` is past the ')'.
    bool tryLink(std::size_t pos, bool image, std::size_t& end) {
        // The matching ']' (brackets nest; code spans and escapes are skipped).
        int depth = 0;
        std::size_t close = std::wstring_view::npos;
        // A label is short (CommonMark: 999 characters): thousands of '[' must not each read to the end.
        constexpr std::size_t kMaxLabel = 2000;
        const std::size_t limit = std::min(m_s.size(), pos + kMaxLabel);
        for (std::size_t k = pos; k < limit; ++k) {
            const wchar_t c = m_s[k];
            if (c == L'\\') {
                ++k;
                continue;
            }
            if (c == L'`') {
                std::size_t run = 0;
                while (k + run < m_s.size() && m_s[k + run] == L'`') {
                    ++run;
                }
                const std::size_t closing = findBackticks(k + run, run);
                if (closing != std::wstring_view::npos) {
                    k = closing + run - 1;
                    continue;
                }
                k += run - 1;
                continue;
            }
            if (c == L'[') {
                ++depth;
            } else if (c == L']') {
                if (--depth == 0) {
                    close = k;
                    break;
                }
            }
        }
        if (close == std::wstring_view::npos || at(close + 1) != L'(') {
            return false;
        }
        std::size_t p = close + 2;
        skipSpaces(p);
        std::wstring url;
        if (at(p) == L'<') {
            const std::size_t gt = m_s.find(L'>', p + 1);
            if (gt == std::wstring_view::npos || m_s.substr(p + 1, gt - p - 1).find(L'\n') != std::wstring_view::npos) {
                return false;
            }
            url = std::wstring(m_s.substr(p + 1, gt - p - 1));
            p = gt + 1;
        } else {
            int parens = 0;
            const std::size_t start = p;
            while (p < m_s.size()) {
                const wchar_t c = m_s[p];
                if (c == L'\\' && p + 1 < m_s.size() && isAsciiPunct(m_s[p + 1])) {
                    url.push_back(m_s[p + 1]);
                    p += 2;
                    continue;
                }
                if (isWs(c)) {
                    break;
                }
                if (c == L'(') {
                    ++parens;
                } else if (c == L')') {
                    if (parens == 0) {
                        break;
                    }
                    --parens;
                }
                url.push_back(c);
                ++p;
            }
            if (p == start && at(p) != L')') {
                return false;
            }
        }
        skipSpaces(p);
        if (const wchar_t q = at(p); q == L'"' || q == L'\'' || q == L'(') {
            const wchar_t closeQuote = q == L'(' ? L')' : q;
            const std::size_t endQuote = m_s.find(closeQuote, p + 1);
            if (endQuote == std::wstring_view::npos) {
                return false;
            }
            p = endQuote + 1; // the title is not shown
            skipSpaces(p);
        }
        if (at(p) != L')') {
            return false;
        }
        end = p + 1;
        const std::wstring_view label = m_s.substr(pos + 1, close - pos - 1);
        const int link = addLink(std::move(url));
        if (image) {
            Inline alt = m_depth < kMaxDepth ? InlineParser(label, m_depth + 1).run() : Inline{std::wstring(label), {}, {}};
            add(alt.text, kImage, link);
            return true;
        }
        if (m_depth >= kMaxDepth) {
            add(label, kLink, link);
            return true;
        }
        // The label's own emphasis is resolved inside it; every piece of it is the link.
        const Inline inner = InlineParser(label, m_depth + 1).run();
        std::size_t done = 0;
        for (const auto& span : inner.spans) {
            if (span.start > done) {
                add(std::wstring_view(inner.text).substr(done, span.start - done), kLink, link);
            }
            add(std::wstring_view(inner.text).substr(span.start, span.length),
                static_cast<std::uint8_t>((span.style & ~kImage) | kLink), link);
            done = span.start + span.length;
        }
        if (done < inner.text.size()) {
            add(std::wstring_view(inner.text).substr(done), kLink, link);
        }
        if (inner.text.empty()) {
            add(m_links[static_cast<std::size_t>(link)], kLink, link); // "[](url)": the address itself
        }
        return true;
    }

    // The next run of exactly `run` backticks from `from`; npos when none. A run length that found no
    // closer from some point finds none from any later point either: remembered, so a line of stray
    // backticks is read once, not once per backtick.
    std::size_t findBackticks(std::size_t from, std::size_t run) const noexcept {
        const std::size_t slot = std::min<std::size_t>(run, m_noCloser.size() - 1);
        if (run < m_noCloser.size() && from >= m_noCloser[slot]) {
            return std::wstring_view::npos;
        }
        for (std::size_t k = from; k < m_s.size(); ++k) {
            if (m_s[k] != L'`') {
                continue;
            }
            std::size_t r = 0;
            while (k + r < m_s.size() && m_s[k + r] == L'`') {
                ++r;
            }
            if (r == run) {
                return k;
            }
            k += r - 1;
        }
        if (run < m_noCloser.size()) {
            m_noCloser[slot] = std::min(m_noCloser[slot], from);
        }
        return std::wstring_view::npos;
    }
    mutable std::array<std::size_t, 33> m_noCloser = [] {
        std::array<std::size_t, 33> a{};
        a.fill(std::wstring_view::npos);
        return a;
    }();

    bool tryAngle(std::size_t pos, std::size_t& end) {
        // <br>, <br/>, <br />: a line break.
        static constexpr std::array<std::wstring_view, 3> kBreaks = {L"<br>", L"<br/>", L"<br />"};
        for (const auto br : kBreaks) {
            if (m_s.size() - pos >= br.size() && text::iequals(m_s.substr(pos, br.size()), br)) {
                trimTrailingSpaces();
                add(L"\n");
                end = pos + br.size();
                return true;
            }
        }
        // <https://…>, <mailto:…>: an autolink.
        const std::size_t gt = m_s.find(L'>', pos + 1);
        if (gt == std::wstring_view::npos) {
            return false;
        }
        const std::wstring_view inside = m_s.substr(pos + 1, gt - pos - 1);
        const std::size_t colon = inside.find(L':');
        const bool scheme = colon != std::wstring_view::npos && colon >= 2 &&
                            std::ranges::all_of(inside.substr(0, colon), [](wchar_t c) {
                                return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                                       c == L'+' || c == L'.' || c == L'-';
                            });
        const bool clean = std::ranges::none_of(inside, [](wchar_t c) { return isWs(c) || c == L'<'; });
        if (scheme && clean) {
            const int link = addLink(std::wstring(inside));
            add(inside, kLink, link);
            end = gt + 1;
            return true;
        }
        return false;
    }

    bool tryEntity(std::size_t pos, std::size_t& end) {
        const std::size_t semi = m_s.find(L';', pos + 1);
        if (semi == std::wstring_view::npos || semi - pos > 10) {
            return false;
        }
        const std::wstring_view name = m_s.substr(pos + 1, semi - pos - 1);
        if (name.size() >= 2 && name.front() == L'#') {
            const bool hex = name[1] == L'x' || name[1] == L'X';
            const std::wstring digits(name.substr(hex ? 2 : 1));
            if (digits.empty() || digits.size() > 7) {
                return false;
            }
            wchar_t* stop = nullptr;
            unsigned long code = std::wcstoul(digits.c_str(), &stop, hex ? 16 : 10);
            if (stop != digits.c_str() + digits.size()) {
                return false;
            }
            if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
                code = 0xFFFD;
            }
            std::wstring out;
            if (code > 0xFFFF) {
                code -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
            } else {
                out.push_back(static_cast<wchar_t>(code));
            }
            add(out);
            end = semi + 1;
            return true;
        }
        for (const auto& e : kEntities) {
            if (name == e.name) {
                add(std::wstring(1, e.ch));
                end = semi + 1;
                return true;
            }
        }
        return false;
    }

    // A bare address ("https://…", "www.…") at `pos`, GitHub's autolink extension.
    bool tryBareUrl(std::size_t pos, std::size_t& end) {
        const std::wstring_view rest = m_s.substr(pos);
        const bool www = text::istartsWith(rest, L"www.");
        if (!text::istartsWith(rest, L"https://") && !text::istartsWith(rest, L"http://") && !www) {
            return false;
        }
        if (pos > 0) {
            const wchar_t before = m_s[pos - 1];
            if (isWordChar(before) || before == L'/' || before == L'@' || before == L'.') {
                return false;
            }
        }
        std::size_t k = pos;
        while (k < m_s.size() && !isWs(m_s[k]) && m_s[k] != L'<') {
            ++k;
        }
        // Trailing punctuation is the sentence's, and an unbalanced ')' the parenthesis' around it.
        while (k > pos) {
            const wchar_t c = m_s[k - 1];
            if (c == L'.' || c == L',' || c == L':' || c == L';' || c == L'!' || c == L'?' || c == L'"' || c == L'\'' ||
                c == L'*' || c == L'_' || c == L'~') {
                --k;
                continue;
            }
            if (c == L')') {
                const auto url = m_s.substr(pos, k - pos);
                if (std::ranges::count(url, L')') > std::ranges::count(url, L'(')) {
                    --k;
                    continue;
                }
            }
            break;
        }
        const std::wstring_view shown = m_s.substr(pos, k - pos);
        if (shown.size() <= (www ? 4u : 8u)) {
            return false; // "https://" alone
        }
        const int link = addLink(www ? L"https://" + std::wstring(shown) : std::wstring(shown));
        add(shown, kLink, link);
        end = k;
        return true;
    }

    void delimiterRun(std::size_t& i) {
        const wchar_t c = m_s[i];
        std::size_t len = 0;
        while (i + len < m_s.size() && m_s[i + len] == c) {
            ++len;
        }
        if (c == L'~' && len != 2) {
            add(m_s.substr(i, len)); // GitHub's strikethrough is "~~": "~" and "~~~" are text
            i += len;
            return;
        }
        const wchar_t before = i == 0 ? L' ' : m_s[i - 1];
        const wchar_t after = i + len >= m_s.size() ? L' ' : m_s[i + len];
        const bool left = !isWs(after) && (!isPunct(after) || isWs(before) || isPunct(before));
        const bool right = !isWs(before) && (!isPunct(before) || isWs(after) || isPunct(after));
        Node node;
        node.delim = c;
        node.count = static_cast<int>(std::min<std::size_t>(len, 64));
        node.original = node.count;
        if (c == L'_') {
            node.canOpen = left && (!right || isPunct(before));
            node.canClose = right && (!left || isPunct(after));
        } else {
            node.canOpen = left;
            node.canClose = right;
        }
        if (len > 64) {
            add(m_s.substr(i, len - 64)); // a silly run: the rest is text
        }
        m_nodes.push_back(std::move(node));
        i += len;
    }

    void scan() {
        std::size_t i = 0;
        const std::size_t n = m_s.size();
        while (i < n) {
            const wchar_t c = m_s[i];
            std::size_t end = 0;
            switch (c) {
            case L'\\':
                if (i + 1 < n && m_s[i + 1] == L'\n') {
                    trimTrailingSpaces();
                    add(L"\n"); // a hard break
                    i += 2;
                    skipSpaces(i);
                    continue;
                }
                if (i + 1 < n && isAsciiPunct(m_s[i + 1])) {
                    add(m_s.substr(i + 1, 1));
                    i += 2;
                    continue;
                }
                add(L"\\");
                ++i;
                continue;
            case L'\n': {
                // Two spaces before the line's end break it; otherwise the lines join with a space.
                const std::size_t spaces = trimTrailingSpaces();
                add(spaces >= 2 ? L"\n" : L" ");
                ++i;
                skipSpaces(i);
                continue;
            }
            case L'`': {
                std::size_t run = 0;
                while (i + run < n && m_s[i + run] == L'`') {
                    ++run;
                }
                const std::size_t closing = findBackticks(i + run, run);
                if (closing == std::wstring_view::npos) {
                    add(m_s.substr(i, run));
                    i += run;
                    continue;
                }
                std::wstring code(m_s.substr(i + run, closing - i - run));
                std::ranges::replace(code, L'\n', L' ');
                if (code.size() >= 2 && code.front() == L' ' && code.back() == L' ' && !isBlank(code)) {
                    code = code.substr(1, code.size() - 2);
                }
                add(code, kCode);
                i = closing + run;
                continue;
            }
            case L'!':
                if (at(i + 1) == L'[' && tryLink(i + 1, /*image=*/true, end)) {
                    i = end;
                    continue;
                }
                add(L"!");
                ++i;
                continue;
            case L'[':
                if (tryLink(i, /*image=*/false, end)) {
                    i = end;
                    continue;
                }
                add(L"[");
                ++i;
                continue;
            case L'<':
                if (tryAngle(i, end)) {
                    i = end;
                    continue;
                }
                add(L"<");
                ++i;
                continue;
            case L'&':
                if (tryEntity(i, end)) {
                    i = end;
                    continue;
                }
                add(L"&");
                ++i;
                continue;
            case L'*':
            case L'_':
            case L'~': delimiterRun(i); continue;
            case L'h':
            case L'H':
            case L'w':
            case L'W':
                if (tryBareUrl(i, end)) {
                    i = end;
                    continue;
                }
                break;
            default: break;
            }
            // Plain text up to the next character that may mean something.
            std::size_t k = i + 1;
            while (k < n) {
                const wchar_t d = m_s[k];
                if (d == L'\\' || d == L'\n' || d == L'`' || d == L'!' || d == L'[' || d == L'<' || d == L'&' || d == L'*' ||
                    d == L'_' || d == L'~') {
                    break;
                }
                if ((d == L'h' || d == L'H' || d == L'w' || d == L'W') &&
                    (text::istartsWith(m_s.substr(k), L"http") || text::istartsWith(m_s.substr(k), L"www."))) {
                    break; // maybe a bare address
                }
                ++k;
            }
            add(m_s.substr(i, k - i));
            i = k;
        }
    }

    // CommonMark's "process emphasis", without the precision nobody's release notes need.
    void resolveEmphasis() {
        for (std::size_t closer = 0; closer < m_nodes.size(); ++closer) {
            if (m_nodes[closer].delim == 0 || !m_nodes[closer].canClose || m_nodes[closer].count == 0 || !m_nodes[closer].active) {
                continue;
            }
            while (m_nodes[closer].count > 0) {
                const Node& c = m_nodes[closer];
                std::size_t opener = std::wstring::npos;
                for (std::size_t k = closer; k-- > 0;) {
                    const Node& o = m_nodes[k];
                    if (o.delim != c.delim || !o.canOpen || o.count == 0 || !o.active) {
                        continue;
                    }
                    // The rule of 3: "*foo**bar*" is not foo in italics.
                    if ((o.canClose || c.canOpen) && (o.original + c.original) % 3 == 0 && !(o.original % 3 == 0 && c.original % 3 == 0)) {
                        continue;
                    }
                    if (c.delim == L'~' && o.count != c.count) {
                        continue;
                    }
                    opener = k;
                    break;
                }
                if (opener == std::wstring::npos) {
                    break;
                }
                Node& o = m_nodes[opener];
                Node& cl = m_nodes[closer];
                const int use = cl.delim == L'~' ? 2 : (o.count >= 2 && cl.count >= 2 ? 2 : 1);
                const std::uint8_t flag = cl.delim == L'~' ? kStrike : use == 2 ? kBold : kItalic;
                for (std::size_t k = opener + 1; k < closer; ++k) {
                    m_nodes[k].style |= flag;
                    if (m_nodes[k].delim != 0) {
                        m_nodes[k].active = false; // what lies inside a match stays text
                    }
                }
                o.count -= use;
                cl.count -= use;
            }
            if (m_nodes[closer].count > 0 && !m_nodes[closer].canOpen) {
                m_nodes[closer].active = false;
            }
        }
    }

    Inline flatten() {
        Inline out;
        out.links = std::move(m_links);
        for (auto& node : m_nodes) {
            std::wstring text = node.delim != 0 ? std::wstring(static_cast<std::size_t>(std::max(node.count, 0)), node.delim)
                                                : std::move(node.text);
            if (text.empty()) {
                continue;
            }
            const auto start = static_cast<std::uint32_t>(out.text.size());
            out.text += text;
            if (node.style == 0) {
                continue;
            }
            if (!out.spans.empty()) {
                Span& last = out.spans.back();
                if (last.style == node.style && last.link == node.link && last.start + last.length == start) {
                    last.length += static_cast<std::uint32_t>(text.size());
                    continue;
                }
            }
            out.spans.push_back({start, static_cast<std::uint32_t>(text.size()), node.style, node.link});
        }
        // Spaces a line break left at the end (a code span keeps its own).
        while (!out.text.empty() && (out.text.back() == L' ' || out.text.back() == L'\n') &&
               (out.styleAt(out.text.size() - 1) & kCode) == 0) {
            out.text.pop_back();
        }
        const auto size = static_cast<std::uint32_t>(out.text.size());
        while (!out.spans.empty() && out.spans.back().start >= size) {
            out.spans.pop_back();
        }
        if (!out.spans.empty() && out.spans.back().start + out.spans.back().length > size) {
            out.spans.back().length = size - out.spans.back().start;
        }
        return out;
    }

    std::wstring_view m_s;
    int m_depth = 0;
    std::vector<Node> m_nodes;
    std::vector<std::wstring> m_links;
};

std::wstring percentDecode(std::wstring_view s) {
    std::string bytes;
    bool any = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        if (s[i] == L'%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            bytes.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
            any = true;
        } else {
            bytes += utf8::fromWide(s.substr(i, 1));
        }
    }
    return any ? utf8::toWide(bytes) : std::wstring(s);
}

} // namespace

std::uint8_t Inline::styleAt(std::size_t position) const noexcept {
    for (const auto& span : spans) {
        if (position >= span.start && position < span.start + span.length) {
            return span.style;
        }
    }
    return 0;
}

Inline parseInline(std::wstring_view text) {
    return InlineParser(text).run();
}

std::wstring plainText(const Inline& in) {
    return in.text;
}

Document parse(std::wstring_view markdown) {
    if (markdown.size() > kMaxInput) {
        markdown = markdown.substr(0, kMaxInput);
    }
    // A byte order mark copied in front of a body.
    if (!markdown.empty() && markdown.front() == 0xFEFF) {
        markdown.remove_prefix(1);
    }
    Document doc;
    doc.blocks = parseBlocks(splitLines(markdown), 0);
    return doc;
}

std::wstring anchorKey(std::wstring_view input) {
    std::wstring_view s = input;
    if (!s.empty() && s.front() == L'#') {
        s.remove_prefix(1);
    }
    const std::wstring folded = text::fold(percentDecode(s));
    std::wstring out;
    for (const wchar_t c : folded) {
        if (c == L' ' || c == L'-') {
            out.push_back(L'-');
        } else if (c == L'_' || isWordChar(c)) {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace wl::markdown
