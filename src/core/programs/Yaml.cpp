#include "core/programs/Yaml.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace wl::core {

namespace {

using Json = nlohmann::json;

bool isBlank(std::string_view line) {
    return line.find_first_not_of(' ') == std::string_view::npos;
}

int indentOf(std::string_view line) {
    const auto at = line.find_first_not_of(' ');
    return at == std::string_view::npos ? static_cast<int>(line.size()) : static_cast<int>(at);
}

std::string_view trimRight(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view trimLeft(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    return text;
}

std::wstring widen(std::string_view text) {
    return std::wstring(text.begin(), text.end()); // diagnostics only: bytes as Latin-1
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool isDash(std::string_view text) {
    return text == "-" || text.starts_with("- ");
}

// Where a mapping key ends — its ": " or final ":" outside quotes; npos when the text is no entry.
std::size_t keyEnd(std::string_view text) {
    if (text.empty()) {
        return std::string_view::npos;
    }
    if (text.front() == '"' || text.front() == '\'') {
        const char quote = text.front();
        std::size_t i = 1;
        while (i < text.size()) {
            if (quote == '"' && text[i] == '\\') {
                i += 2;
                continue;
            }
            if (text[i] == quote) {
                if (quote == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
                    i += 2;
                    continue;
                }
                break;
            }
            ++i;
        }
        if (i + 1 < text.size() && text[i + 1] == ':' && (i + 2 == text.size() || text[i + 2] == ' ')) {
            return i + 1;
        }
        return std::string_view::npos;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ':' && (i + 1 == text.size() || text[i + 1] == ' ')) {
            return i;
        }
        if (text[i] == ' ' && i + 1 < text.size() && text[i + 1] == '#') {
            return std::string_view::npos; // a comment before any colon
        }
    }
    return std::string_view::npos;
}

class Parser {
public:
    explicit Parser(std::string_view text) {
        std::size_t start = 0;
        while (true) {
            auto end = text.find('\n', start);
            if (end == std::string_view::npos) {
                end = text.size();
            }
            std::string_view line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            m_lines.push_back(line);
            if (end == text.size()) {
                break;
            }
            start = end + 1;
        }
        // A UTF-8 byte order mark is no content.
        if (m_lines.front().starts_with("\xEF\xBB\xBF")) {
            m_lines.front().remove_prefix(3);
        }
    }

    Result<Json> document() {
        for (std::size_t i = 0; i < m_lines.size(); ++i) {
            const auto line = m_lines[i];
            const auto content = line.find_first_not_of(" \t");
            if (content != std::string_view::npos && line.find('\t') < content) {
                m_pos = i;
                setError("a tab in the indentation");
                return std::unexpected(*m_error);
            }
        }
        skipIgnorable();
        if (m_pos < m_lines.size() && trimRight(m_lines[m_pos]) == "---") {
            ++m_pos;
        }
        Json root = block(0);
        if (!m_error) {
            skipIgnorable();
            if (m_pos < m_lines.size() && trimRight(m_lines[m_pos]) != "...") {
                setError("unexpected content");
            }
        }
        if (m_error) {
            return std::unexpected(*m_error);
        }
        return root.is_null() ? Json::object() : root;
    }

private:
    std::vector<std::string_view> m_lines;
    std::size_t m_pos = 0;
    std::optional<Error> m_error;

    void setError(const char* what) {
        if (!m_error) {
            const std::string_view line = m_pos < m_lines.size() ? m_lines[m_pos] : std::string_view{};
            m_error = Error{ErrorCode::ParseError, L"not the YAML winget writes",
                            std::format(L"line {}: {} ({})", m_pos + 1, widen(what), widen(line))};
        }
    }

    [[nodiscard]] static bool ignorable(std::string_view line) {
        return isBlank(line) || trimLeft(line).starts_with('#');
    }
    void skipIgnorable() {
        while (m_pos < m_lines.size() && ignorable(m_lines[m_pos])) {
            ++m_pos;
        }
    }

    Json block(int minIndent) {
        skipIgnorable();
        if (m_pos >= m_lines.size() || m_error) {
            return nullptr;
        }
        const std::string_view line = m_lines[m_pos];
        const int ind = indentOf(line);
        if (ind < minIndent) {
            return nullptr;
        }
        const std::string_view text = trimRight(line.substr(static_cast<std::size_t>(ind)));
        if (isDash(text)) {
            return sequence(ind);
        }
        if (keyEnd(text) != std::string_view::npos) {
            return mapping(ind, std::nullopt);
        }
        ++m_pos; // a lone scalar as the whole block
        return scalar(text, ind - 1);
    }

    Json sequence(int ind) {
        Json items = Json::array();
        while (!m_error) {
            skipIgnorable();
            if (m_pos >= m_lines.size()) {
                break;
            }
            const std::string_view line = m_lines[m_pos];
            if (indentOf(line) != ind) {
                if (indentOf(line) > ind) {
                    setError("unexpected indentation");
                }
                break;
            }
            const std::string_view text = trimRight(line.substr(static_cast<std::size_t>(ind)));
            if (!isDash(text)) {
                break;
            }
            const std::string_view afterDash = text.substr(1);
            const std::string_view rest = trimLeft(afterDash);
            const int column = ind + 1 + static_cast<int>(afterDash.size() - rest.size());
            if (rest.empty()) {
                ++m_pos;
                items.push_back(block(ind + 1));
            } else if (isDash(rest)) {
                setError("a sequence directly inside a sequence item");
            } else if (keyEnd(rest) != std::string_view::npos) {
                items.push_back(mapping(column, rest));
            } else {
                ++m_pos;
                items.push_back(scalar(rest, ind));
            }
        }
        return items;
    }

    // `first`: the first entry when it starts after a sequence dash on the current line.
    Json mapping(int ind, std::optional<std::string_view> first) {
        Json object = Json::object();
        while (!m_error) {
            std::string_view text;
            if (first) {
                text = *first;
                first.reset();
            } else {
                skipIgnorable();
                if (m_pos >= m_lines.size()) {
                    break;
                }
                const std::string_view line = m_lines[m_pos];
                const int lineIndent = indentOf(line);
                if (lineIndent != ind) {
                    if (lineIndent > ind) {
                        setError("unexpected indentation");
                    }
                    break;
                }
                text = trimRight(line.substr(static_cast<std::size_t>(ind)));
                if (isDash(text)) {
                    break; // the parent's: "key:" then "- item" at the key's own column
                }
            }
            const std::size_t colon = keyEnd(text);
            if (colon == std::string_view::npos) {
                setError("a mapping entry without a key");
                break;
            }
            const std::string key = keyText(trimRight(text.substr(0, colon)));
            const std::string_view value = trimLeft(text.substr(colon + 1));
            ++m_pos;
            if (!value.empty() && !value.starts_with('#')) {
                object[key] = scalar(value, ind);
                continue;
            }
            skipIgnorable();
            if (m_pos < m_lines.size()) {
                const std::string_view next = m_lines[m_pos];
                const int nextIndent = indentOf(next);
                if (nextIndent > ind) {
                    object[key] = block(nextIndent);
                    continue;
                }
                if (nextIndent == ind && isDash(trimRight(next.substr(static_cast<std::size_t>(nextIndent))))) {
                    object[key] = sequence(ind);
                    continue;
                }
            }
            object[key] = nullptr;
        }
        return object;
    }

    std::string keyText(std::string_view key) {
        if (!key.empty() && (key.front() == '"' || key.front() == '\'')) {
            const Json decoded = quoted(key, -1);
            return decoded.is_string() ? decoded.get<std::string>() : std::string(key);
        }
        return std::string(key);
    }

    // A scalar whose text starts with `start` on the line just consumed; its continuation lines
    // are indented more than `parent`.
    Json scalar(std::string_view start, int parent) {
        if (start == "[]") {
            return Json::array();
        }
        if (start == "{}") {
            return Json::object();
        }
        switch (start.front()) {
        case '[':
        case '{': setError("a flow collection"); return nullptr;
        case '&':
        case '*':
        case '!': setError("anchors, aliases and tags"); return nullptr;
        case '"':
        case '\'': return quoted(start, parent);
        case '|':
        case '>': return blockScalar(start, parent);
        default: return plain(start, parent);
        }
    }

    // Between two lines of a flow scalar: one break is a space, n empty lines are n "\n".
    static void fold(std::string& out, int emptyLines) {
        if (emptyLines == 0) {
            out.push_back(' ');
        } else {
            out.append(static_cast<std::size_t>(emptyLines), '\n');
        }
    }

    static void dropTrailingSpaces(std::string& out) {
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
    }

    Json plain(std::string_view start, int parent) {
        auto withoutComment = [](std::string_view text) {
            const auto at = text.find(" #");
            return trimRight(at == std::string_view::npos ? text : text.substr(0, at));
        };
        std::string out(withoutComment(start));
        int empty = 0;
        while (m_pos < m_lines.size()) {
            const std::string_view line = m_lines[m_pos];
            if (isBlank(line)) {
                ++empty;
                ++m_pos;
                continue;
            }
            const int ind = indentOf(line);
            const std::string_view text = trimRight(line.substr(static_cast<std::size_t>(ind)));
            if (ind <= parent || text.starts_with('#')) {
                break; // empty lines before it were the end of the scalar: ignorable for the structure
            }
            fold(out, empty);
            empty = 0;
            out.append(withoutComment(text));
            ++m_pos;
        }
        return out;
    }

    // The escapes of one line of a double-quoted scalar. Stops at the closing quote (`closed`,
    // `rest` = what follows). `continued`: the line ends in "\" (an escaped line break).
    static bool decodeDouble(std::string_view text, std::string& out, bool& closed, std::string_view& rest, bool& continued) {
        closed = false;
        continued = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '"') {
                closed = true;
                rest = text.substr(i + 1);
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i + 1 >= text.size()) {
                continued = true;
                return true;
            }
            const char e = text[++i];
            auto hex = [&](std::size_t digits) {
                if (i + digits >= text.size()) {
                    return false;
                }
                std::uint32_t cp = 0;
                for (std::size_t d = 1; d <= digits; ++d) {
                    const char h = text[i + d];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp |= static_cast<std::uint32_t>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp |= static_cast<std::uint32_t>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp |= static_cast<std::uint32_t>(h - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                i += digits;
                appendUtf8(out, cp);
                return true;
            };
            switch (e) {
            case '0': out.push_back('\0'); break;
            case 'a': out.push_back('\a'); break;
            case 'b': out.push_back('\b'); break;
            case 't':
            case '\t': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'v': out.push_back('\v'); break;
            case 'f': out.push_back('\f'); break;
            case 'r': out.push_back('\r'); break;
            case 'e': out.push_back('\x1B'); break;
            case ' ': out.push_back(' '); break;
            case '"': out.push_back('"'); break;
            case '/': out.push_back('/'); break;
            case '\\': out.push_back('\\'); break;
            case 'N': appendUtf8(out, 0x85); break;
            case '_': appendUtf8(out, 0xA0); break;
            case 'L': appendUtf8(out, 0x2028); break;
            case 'P': appendUtf8(out, 0x2029); break;
            case 'x':
                if (!hex(2)) {
                    return false;
                }
                break;
            case 'u':
                if (!hex(4)) {
                    return false;
                }
                break;
            case 'U':
                if (!hex(8)) {
                    return false;
                }
                break;
            default: return false;
            }
        }
        return true;
    }

    // `start` begins with the quote. `parent` < 0: a key, all on its line.
    Json quoted(std::string_view start, int parent) {
        const char quote = start.front();
        std::string out;
        std::string_view text = start.substr(1);
        int empty = 0;
        bool firstLine = true;
        bool joinNoSpace = false;
        while (true) {
            if (!firstLine) {
                if (m_pos >= m_lines.size() || parent < 0) {
                    setError("a quoted scalar that does not end");
                    return nullptr;
                }
                const std::string_view line = m_lines[m_pos];
                ++m_pos;
                if (isBlank(line)) {
                    ++empty;
                    continue;
                }
                text = trimLeft(trimRight(line));
                if (joinNoSpace) {
                    out.append(static_cast<std::size_t>(empty), '\n');
                } else {
                    fold(out, empty);
                }
                joinNoSpace = false;
                empty = 0;
            }
            firstLine = false;
            bool closed = false;
            std::string_view rest;
            if (quote == '"') {
                bool continued = false;
                if (!decodeDouble(text, out, closed, rest, continued)) {
                    setError("a bad escape in a double-quoted scalar");
                    return nullptr;
                }
                if (!closed && !continued) {
                    dropTrailingSpaces(out); // the line break folds; spaces before it do not count
                }
                joinNoSpace = continued;
            } else {
                for (std::size_t i = 0; i < text.size(); ++i) {
                    if (text[i] == '\'') {
                        if (i + 1 < text.size() && text[i + 1] == '\'') {
                            out.push_back('\'');
                            ++i;
                            continue;
                        }
                        closed = true;
                        rest = text.substr(i + 1);
                        break;
                    }
                    out.push_back(text[i]);
                }
                if (!closed) {
                    dropTrailingSpaces(out);
                }
            }
            if (closed) {
                rest = trimLeft(rest);
                if (!rest.empty() && !rest.starts_with('#')) {
                    setError("content after a quoted scalar");
                    return nullptr;
                }
                return out;
            }
        }
    }

    Json blockScalar(std::string_view header, int parent) {
        const bool literal = header.front() == '|';
        char chomp = 'c'; // clip: one final line break
        int explicitIndent = 0;
        for (const char c : trimRight(header.substr(1))) {
            if (c == '-' || c == '+') {
                chomp = c;
            } else if (c >= '1' && c <= '9') {
                explicitIndent = c - '0';
            } else if (c == ' ' || c == '#') {
                break;
            } else {
                setError("a bad block scalar header");
                return nullptr;
            }
        }
        // The lines indented more than the parent, and the empty lines among them.
        int contentIndent = explicitIndent > 0 ? std::max(parent, 0) + explicitIndent : -1;
        std::vector<std::string_view> lines;
        while (m_pos < m_lines.size()) {
            const std::string_view line = m_lines[m_pos];
            if (!isBlank(line)) {
                const int ind = indentOf(line);
                if (ind <= parent) {
                    break;
                }
                if (contentIndent < 0) {
                    contentIndent = ind;
                }
                if (ind < contentIndent) {
                    break;
                }
            }
            lines.push_back(line);
            ++m_pos;
        }
        std::size_t trailing = 0;
        while (!lines.empty() && isBlank(lines.back())) {
            lines.pop_back();
            ++trailing;
        }
        const auto cut = static_cast<std::size_t>(std::max(contentIndent, 0));
        std::string out;
        std::size_t empty = 0; // empty lines since the last content line
        bool first = true;
        bool previousMoreIndented = false;
        for (const std::string_view line : lines) {
            if (isBlank(line)) {
                ++empty;
                continue;
            }
            const std::string_view content = line.substr(std::min(line.size(), cut));
            const bool moreIndented = content.front() == ' ';
            if (first) {
                out.append(empty, '\n');
            } else if (literal) {
                out.append(empty + 1, '\n');
            } else if (empty == 0 && !moreIndented && !previousMoreIndented) {
                out.push_back(' '); // folded: one line break is a space
            } else {
                // n empty lines are n line breaks; a more indented line keeps its own break too
                out.append(empty + (moreIndented || previousMoreIndented ? 1 : 0), '\n');
            }
            out.append(content);
            first = false;
            empty = 0;
            previousMoreIndented = moreIndented;
        }
        if (!lines.empty()) {
            if (chomp == 'c') {
                out.push_back('\n');
            } else if (chomp == '+') {
                out.append(trailing + 1, '\n');
            }
        }
        return out;
    }
};

} // namespace

Result<nlohmann::json> parseYaml(std::string_view text) {
    Parser parser(text);
    return parser.document();
}

} // namespace wl::core
