// D-145: the release notes' Markdown reader (base/Markdown) — each construct, nesting, malformed and
// hostile input, CRLF / CR, very long lines, emphasis edge cases, the heading anchors.
#include "base/Markdown.h"

#include <doctest.h>

#include <chrono>
#include <string>

using namespace wl;
using namespace wl::markdown;

namespace {

// A copy: the documents in these tests are temporaries.
Block only(const Document& doc) {
    REQUIRE(doc.blocks.size() == 1);
    return doc.blocks.front();
}

// The text of `in` with each styled run marked: bold [b:…], italic [i:…], code [c:…], strike [s:…],
// link [l:…|url], image [img:…|src] (several flags: in that order, nested).
std::wstring marked(const Inline& in) {
    std::wstring out;
    std::size_t done = 0;
    for (const auto& span : in.spans) {
        out += in.text.substr(done, span.start - done);
        std::wstring text = in.text.substr(span.start, span.length);
        if (span.style & kCode) text = L"[c:" + text + L"]";
        if (span.style & kStrike) text = L"[s:" + text + L"]";
        if (span.style & kItalic) text = L"[i:" + text + L"]";
        if (span.style & kBold) text = L"[b:" + text + L"]";
        if (span.style & kImage) text = L"[img:" + text + L"|" + in.links[static_cast<std::size_t>(span.link)] + L"]";
        if (span.style & kLink) text = L"[l:" + text + L"|" + in.links[static_cast<std::size_t>(span.link)] + L"]";
        out += text;
        done = span.start + span.length;
    }
    out += in.text.substr(done);
    return out;
}

std::wstring inl(std::wstring_view text) {
    return marked(parseInline(text));
}

} // namespace

TEST_CASE("markdown: headings") {
    const auto doc = parse(L"# One\n## Two ##\n### Three\n#### Four\n##### Five\n###### Six\n####### Seven\n\n#hashtag");
    REQUIRE(doc.blocks.size() == 8);
    for (int i = 0; i < 6; ++i) {
        CHECK(doc.blocks[static_cast<std::size_t>(i)].kind == BlockKind::Heading);
        CHECK(doc.blocks[static_cast<std::size_t>(i)].level == i + 1);
    }
    CHECK(doc.blocks[1].text.text == L"Two");
    CHECK(doc.blocks[6].kind == BlockKind::Paragraph); // seven: text
    CHECK(doc.blocks[6].text.text == L"####### Seven");
    CHECK(doc.blocks[7].kind == BlockKind::Paragraph);
    CHECK(doc.blocks[7].text.text == L"#hashtag");

    // Setext: "===" and "---" under a paragraph; "---" alone is a rule.
    const auto setext = parse(L"Title\n=====\n\nSub *title*\n---\n\n---\n");
    REQUIRE(setext.blocks.size() == 3);
    CHECK(setext.blocks[0].kind == BlockKind::Heading);
    CHECK(setext.blocks[0].level == 1);
    CHECK(setext.blocks[1].level == 2);
    CHECK(marked(setext.blocks[1].text) == L"Sub [i:title]");
    CHECK(setext.blocks[2].kind == BlockKind::Rule);

    // Inline marks in a heading; an empty heading.
    CHECK(marked(only(parse(L"## **Yeni** `x`")).text) == L"[b:Yeni] [c:x]");
    CHECK(only(parse(L"##")).text.text.empty());
    CHECK(only(parse(L"## # ")).kind == BlockKind::Heading);
}

TEST_CASE("markdown: paragraphs, soft wraps and hard breaks") {
    const auto& p = only(parse(L"one\ntwo\n   three"));
    CHECK(p.kind == BlockKind::Paragraph);
    CHECK(p.text.text == L"one two three");
    CHECK(only(parse(L"one  \ntwo")).text.text == L"one\ntwo");    // two trailing spaces
    CHECK(only(parse(L"one\\\ntwo")).text.text == L"one\ntwo");    // a backslash
    CHECK(only(parse(L"one<br>two<BR/>three<br />four")).text.text == L"one\ntwo\nthree\nfour");
    CHECK(only(parse(L"one \ntwo")).text.text == L"one two");      // one space is no break
    const auto two = parse(L"first\n\n\n\nsecond\n");
    REQUIRE(two.blocks.size() == 2);
    CHECK(two.blocks[1].text.text == L"second");
    CHECK(only(parse(L"trailing   ")).text.text == L"trailing");
}

TEST_CASE("markdown: emphasis") {
    CHECK(inl(L"**bold** and __bold__") == L"[b:bold] and [b:bold]");
    CHECK(inl(L"*it* and _it_") == L"[i:it] and [i:it]");
    CHECK(inl(L"***both***") == L"[b:[i:both]]");
    CHECK(inl(L"**bold *italic* bold**") == L"[b:bold ][b:[i:italic]][b: bold]");
    CHECK(inl(L"~~gone~~ ~one~ ~~~three~~~") == L"[s:gone] ~one~ ~~~three~~~");
    // Inside words: "_" never, "*" does.
    CHECK(inl(L"snake_case_name") == L"snake_case_name");
    CHECK(inl(L"un*frigging*believable") == L"un[i:frigging]believable");
    // Spaces around a run: not emphasis.
    CHECK(inl(L"2 * 3 * 4") == L"2 * 3 * 4");
    CHECK(inl(L"a * b*") == L"a * b*");
    // Unmatched runs stay as they were.
    CHECK(inl(L"**open") == L"**open");
    CHECK(inl(L"close**") == L"close**");
    CHECK(inl(L"***a**") == L"*[b:a]");
    CHECK(inl(L"**a***") == L"[b:a]*");
    CHECK(inl(L"*a **b** c*") == L"[i:a ][b:[i:b]][i: c]");
    // The rule of 3.
    CHECK(inl(L"*foo**bar*") == L"[i:foo**bar]");
    // Escapes.
    CHECK(inl(L"\\*not\\* \\_this\\_ \\\\ \\a") == L"*not* _this_ \\ \\a");
    // Punctuation next to the run (the real notes: "**Beta:**").
    CHECK(inl(L"**Beta:** Değiştir") == L"[b:Beta:] Değiştir");
    CHECK(inl(L"(**not** this)") == L"([b:not] this)");
    CHECK(inl(L"**31 uyumluluk koruması:** Windows") == L"[b:31 uyumluluk koruması:] Windows");
}

TEST_CASE("markdown: inline code") {
    CHECK(inl(L"run `wlcli apply` now") == L"run [c:wlcli apply] now");
    CHECK(inl(L"``a ` b``") == L"[c:a ` b]");
    CHECK(inl(L"` x `") == L"[c:x]");
    CHECK(inl(L"`  `") == L"[c:  ]");
    CHECK(inl(L"`*not* [a](b)`") == L"[c:*not* [a](b)]");
    CHECK(inl(L"`unclosed") == L"`unclosed");
    CHECK(inl(L"``mismatch`") == L"``mismatch`");
    CHECK(only(parse(L"`one\ntwo`")).text.text == L"one two");
}

TEST_CASE("markdown: links, autolinks and images") {
    CHECK(inl(L"[text](https://a.b/c)") == L"[l:text|https://a.b/c]");
    CHECK(inl(L"[t](https://a.b \"title\")") == L"[l:t|https://a.b]");
    CHECK(inl(L"[t](<https://a.b/x y>)") == L"[l:t|https://a.b/x y]");
    CHECK(inl(L"[w](https://en.wikipedia.org/wiki/Foo_(bar))") == L"[l:w|https://en.wikipedia.org/wiki/Foo_(bar)]");
    CHECK(inl(L"[**bold** link](u)") == L"[l:[b:bold]|u][l: link|u]");
    CHECK(inl(L"**[x](y)**") == L"[l:[b:x]|y]");
    CHECK(inl(L"[Türkçe](#türkçe) · [English](#english)") == L"[l:Türkçe|#türkçe] · [l:English|#english]");
    CHECK(inl(L"<https://github.com/x>") == L"[l:https://github.com/x|https://github.com/x]");
    CHECK(inl(L"see https://example.com/a, then") == L"see [l:https://example.com/a|https://example.com/a], then");
    CHECK(inl(L"(www.example.com)") == L"([l:www.example.com|https://www.example.com])");
    CHECK(inl(L"https://") == L"https://");
    CHECK(inl(L"xhttps://no") == L"xhttps://no");
    CHECK(inl(L"![logo](https://x/logo.png) after") == L"[img:logo|https://x/logo.png] after");
    CHECK(inl(L"![](x.png)") == L"");
    CHECK(inl(L"[](https://a)") == L"[l:https://a|https://a]");
    // Broken ones are text.
    CHECK(inl(L"[open") == L"[open");
    CHECK(inl(L"[a](") == L"[a](");
    CHECK(inl(L"[a] (b)") == L"[a] (b)");
    CHECK(inl(L"![") == L"![");
    CHECK(inl(L"[a](b \"unclosed)") == L"[a](b \"unclosed)");
    CHECK(inl(L"<notalink> <a b>") == L"<notalink> <a b>");
    // Entities.
    CHECK(inl(L"a &amp; b &lt;c&gt; &#228; &#x1F600; &bogus; &") == L"a & b <c> \u00e4 \U0001F600 &bogus; &");
}

TEST_CASE("markdown: code blocks") {
    const auto& fenced = only(parse(L"```cpp extra\nint x = 1;\n\n  y();\n```\n"));
    CHECK(fenced.kind == BlockKind::Code);
    CHECK(fenced.info == L"cpp");
    CHECK(fenced.code == L"int x = 1;\n\n  y();");
    const auto& tilde = only(parse(L"~~~~\n```\nnot closed by backticks\n~~~~"));
    CHECK(tilde.code == L"```\nnot closed by backticks");
    const auto& open = only(parse(L"```\nruns\nto the end"));
    CHECK(open.code == L"runs\nto the end");
    const auto& indented = only(parse(L"    code line\n      deeper\n\n    after blank"));
    CHECK(indented.kind == BlockKind::Code);
    CHECK(indented.code == L"code line\n  deeper\n\nafter blank");
    // An indented fence: its indent goes from the lines.
    CHECK(only(parse(L"  ```\n  a\n    b\n  ```")).code == L"a\n  b");
    // "``` x `y`" is no fence.
    CHECK(only(parse(L"``` x `y`")).kind == BlockKind::Paragraph);
    // Inside a code block nothing is read.
    CHECK(only(parse(L"```\n# not a heading\n**no**\n```")).code == L"# not a heading\n**no**");
}

TEST_CASE("markdown: lists") {
    const auto& bullets = only(parse(L"- one\n- two\n- three"));
    CHECK(bullets.kind == BlockKind::List);
    CHECK_FALSE(bullets.ordered);
    REQUIRE(bullets.items.size() == 3);
    CHECK(bullets.items[1].front().text.text == L"two");

    // Another delimiter starts another list.
    const auto doc = parse(L"3. three\n4. four\n9) other delimiter");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(doc.blocks[0].ordered);
    CHECK(doc.blocks[0].start == 3);
    CHECK(doc.blocks[0].items.size() == 2);
    CHECK(doc.blocks[1].start == 9);

    // Nested the way the release notes write them (two spaces), a paragraph after the inner list.
    const auto& nested = only(parse(L"- **Yeni bileşen modeli:**\n  - kategori ağacı;\n  - kaldırma modu;\n- **Etki raporu**\n\n  Her seçim canlı.\n"));
    REQUIRE(nested.items.size() == 2);
    REQUIRE(nested.items[0].size() == 2);
    CHECK(marked(nested.items[0][0].text) == L"[b:Yeni bileşen modeli:]");
    CHECK(nested.items[0][1].kind == BlockKind::List);
    CHECK(nested.items[0][1].items.size() == 2);
    REQUIRE(nested.items[1].size() == 2);
    CHECK(nested.items[1][1].text.text == L"Her seçim canlı.");

    // Three levels, mixed kinds.
    const auto& deep = only(parse(L"1. a\n   - b\n     1. c\n2. d"));
    REQUIRE(deep.items.size() == 2);
    const auto& level2 = deep.items[0][1];
    CHECK_FALSE(level2.ordered);
    CHECK(level2.items[0][1].ordered);
    CHECK(level2.items[0][1].items[0][0].text.text == L"c");

    // Loose: blank lines between items keep one list.
    CHECK(only(parse(L"- a\n\n- b\n\n\n- c")).items.size() == 3);
    // A lazy line continues the item's paragraph.
    CHECK(only(parse(L"- one\ncontinued")).items[0][0].text.text == L"one continued");
    // An empty item; a marker without a space is text; "* * *" is a rule.
    CHECK(only(parse(L"-\n- x")).items.size() == 2);
    CHECK(only(parse(L"-x")).kind == BlockKind::Paragraph);
    CHECK(only(parse(L"* * *")).kind == BlockKind::Rule);
    // A list right after a paragraph; "2." does not interrupt one, "1." does.
    CHECK(parse(L"text\n- item").blocks.size() == 2);
    CHECK(parse(L"text\n2. not a list").blocks.size() == 1);
    CHECK(parse(L"text\n1. a list").blocks.size() == 2);
    // A code fence inside an item.
    const auto& withCode = only(parse(L"- step:\n  ```\n  wlcli help\n  ```"));
    REQUIRE(withCode.items[0].size() == 2);
    CHECK(withCode.items[0][1].code == L"wlcli help");
}

TEST_CASE("markdown: block quotes") {
    const auto doc = parse(L"> \u26a0\ufe0f **Beta:** first\n> second line\nlazy line\n\nafter");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(doc.blocks[0].kind == BlockKind::Quote);
    REQUIRE(doc.blocks[0].children.size() == 1);
    CHECK(marked(doc.blocks[0].children[0].text) == L"\u26a0\ufe0f [b:Beta:] first second line lazy line");
    // Nested quotes, a list inside a quote.
    const auto& nested = only(parse(L"> a\n>> b\n> - c\n> - d"));
    REQUIRE(nested.children.size() >= 2);
    CHECK(nested.children[1].kind == BlockKind::Quote);
    const auto& list = only(parse(L"> - c\n> - d"));
    REQUIRE(list.children.size() == 1);
    CHECK(list.children[0].kind == BlockKind::List);
    CHECK(only(parse(L">")).children.empty());
}

TEST_CASE("markdown: rules, tables, HTML") {
    for (const wchar_t* rule : {L"---", L"***", L"___", L" - - -", L"*****"}) {
        CHECK(only(parse(rule)).kind == BlockKind::Rule);
    }
    CHECK(only(parse(L"--")).kind == BlockKind::Paragraph);

    const auto& table = only(parse(L"| Name | Size | Note |\n|:-----|-----:|:----:|\n| `a\\|b` | 1 | x |\n| short |\n| a | b | c | d |"));
    CHECK(table.kind == BlockKind::Table);
    REQUIRE(table.align.size() == 3);
    CHECK(table.align[0] == Align::Left);
    CHECK(table.align[1] == Align::Right);
    CHECK(table.align[2] == Align::Center);
    REQUIRE(table.rows.size() == 4);
    CHECK(table.rows[0][1].text == L"Size");
    CHECK(marked(table.rows[1][0]) == L"[c:a|b]");
    CHECK(table.rows[2].size() == 3); // padded
    CHECK(table.rows[2][1].text.empty());
    CHECK(table.rows[3].size() == 3); // cut
    // Without the pipes at the ends; the header must match the delimiter row.
    CHECK(only(parse(L"a | b\n--|--\n1 | 2")).kind == BlockKind::Table);
    CHECK(only(parse(L"| a | b |\n|---|\n| 1 |")).kind == BlockKind::Paragraph);
    // A table after a paragraph line.
    CHECK(parse(L"intro\n| a | b |\n|---|---|\n| 1 | 2 |").blocks.size() == 2);

    // Comments go; other HTML is text.
    const auto html = parse(L"<!-- hidden\nstill hidden -->\n<details><summary>More</summary>\n\ntext");
    REQUIRE(html.blocks.size() == 2);
    CHECK(html.blocks[0].text.text == L"<details><summary>More</summary>");
    CHECK(parse(L"<!-- never closed\nline").blocks.empty());
}

TEST_CASE("markdown: line endings, empty and odd input") {
    const std::wstring lf = L"# T\n\n- a\n- b\n\n> q\n\n```\nx\n```\n";
    std::wstring crlf;
    std::wstring cr;
    for (const wchar_t c : lf) {
        crlf += c == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, c);
        cr += c == L'\n' ? L'\r' : c;
    }
    const auto a = parse(lf);
    for (const auto& other : {parse(crlf), parse(cr)}) {
        REQUIRE(other.blocks.size() == a.blocks.size());
        for (std::size_t i = 0; i < a.blocks.size(); ++i) {
            CHECK(other.blocks[i].kind == a.blocks[i].kind);
        }
        CHECK(other.blocks[3].code == L"x");
    }
    CHECK(parse(L"").blocks.empty());
    CHECK(parse(L"   \n\t\n \r\n").blocks.empty());
    CHECK(only(parse(L"\ufeffBOM")).text.text == L"BOM");
    // Tabs count as four columns.
    CHECK(only(parse(L"\tcode")).kind == BlockKind::Code);
    CHECK(only(parse(L"-\tone")).items.size() == 1);
    // Nothing here may throw or loop: stray marks of every kind.
    for (const wchar_t* odd : {L"*", L"**", L"_", L"`", L"[", L"]", L"(", L"![", L"<", L"&", L"|", L"|---|", L"> ", L"1.", L"#",
                               L"\\", L"~~", L"***___***", L"[[[[]]]]", L"[a]([b](c))", L"<br", L"&#;", L"&#x110000;"}) {
        const auto doc = parse(odd);
        CHECK(doc.blocks.size() <= 2);
    }
    CHECK(inl(L"&#x110000;") == L"\ufffd");
}

TEST_CASE("markdown: long and hostile input stays fast and flat") {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    // A very long line of words with marks in it.
    std::wstring line;
    for (int i = 0; i < 20000; ++i) {
        line += (i % 7 == 0) ? L"**w** " : (i % 11 == 0) ? L"`c` " : L"word ";
    }
    const auto& p = only(parse(line));
    CHECK(p.text.text.size() > 80000);
    CHECK(p.text.spans.size() > 4000);
    // 5000 nested quotes and list markers: read to kMaxDepth, the rest as text.
    const auto quotes = parse(std::wstring(5000, L'>') + L" deep");
    CHECK(quotes.blocks.size() == 1);
    std::wstring lists;
    for (int i = 0; i < 400; ++i) {
        lists += std::wstring(static_cast<std::size_t>(i * 2), L' ') + L"- level\n";
    }
    CHECK(parse(lists).blocks.size() == 1);
    // Thousands of unmatched openers.
    CHECK(parse(std::wstring(20000, L'[')).blocks.size() == 1);
    CHECK(parse(std::wstring(3000, L'*') + L"a").blocks.size() <= 2);
    std::wstring stars;
    for (int i = 0; i < 3000; ++i) {
        stars += L"*a ";
    }
    CHECK(parse(stars).blocks.size() == 1);
    // More than the reader takes: cut, never refused.
    CHECK_FALSE(parse(std::wstring(kMaxInput + 1000, L'x')).blocks.empty());
    CHECK(clock::now() - started < std::chrono::seconds(20));
}

TEST_CASE("markdown: heading anchors") {
    CHECK(anchorKey(L"Türkçe") == anchorKey(L"#türkçe"));
    CHECK(anchorKey(L"Türkçe") == anchorKey(L"#t%C3%BCrk%C3%A7e"));
    CHECK(anchorKey(L"English") == L"english");
    CHECK(anchorKey(L"Yeni özellikler: v1.4!") == L"yeni-ozellikler-v14");
    CHECK(anchorKey(L"#Düzeltmeler") == anchorKey(L"Düzeltmeler"));
    CHECK(anchorKey(L"") == L"");
}
