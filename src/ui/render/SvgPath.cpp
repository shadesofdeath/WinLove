#include "ui/render/SvgPath.h"

#include "base/Hresult.h"
#include "base/Utf8.h"

#include <cctype>
#include <charconv>
#include <optional>

namespace wl::ui {

namespace {

class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) : m_text(text) {}

    void skipSeparators() {
        while (m_pos < m_text.size() && (std::isspace(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == ',')) {
            ++m_pos;
        }
    }
    [[nodiscard]] bool atEnd() {
        skipSeparators();
        return m_pos >= m_text.size();
    }
    // A command letter, if the next token is one.
    [[nodiscard]] std::optional<char> command() {
        skipSeparators();
        if (m_pos < m_text.size() && std::isalpha(static_cast<unsigned char>(m_text[m_pos])) &&
            m_text[m_pos] != 'e' && m_text[m_pos] != 'E') {
            return m_text[m_pos++];
        }
        return std::nullopt;
    }
    [[nodiscard]] std::optional<float> number() {
        skipSeparators();
        const char* begin = m_text.data() + m_pos;
        const char* end = m_text.data() + m_text.size();
        // from_chars does not accept a leading '+'.
        if (begin < end && *begin == '+') {
            ++begin;
        }
        // SVG allows "1.5.5" == "1.5 .5": stop at a second '.' before parsing.
        const char* scan = begin;
        if (scan < end && *scan == '-') {
            ++scan;
        }
        bool seenDot = false;
        bool seenExp = false;
        for (; scan < end; ++scan) {
            const char c = *scan;
            if (std::isdigit(static_cast<unsigned char>(c))) {
                continue;
            }
            if (c == '.' && !seenDot && !seenExp) {
                seenDot = true;
                continue;
            }
            if ((c == 'e' || c == 'E') && !seenExp) {
                seenExp = true;
                if (scan + 1 < end && (scan[1] == '-' || scan[1] == '+')) {
                    ++scan;
                }
                continue;
            }
            break;
        }
        float value = 0;
        const auto [ptr, ec] = std::from_chars(begin, scan, value);
        if (ec != std::errc{} || ptr == begin) {
            return std::nullopt;
        }
        m_pos = static_cast<std::size_t>(ptr - m_text.data());
        return value;
    }
    // Arc flags may be written without separators ("a6 6 0 1 0 0 12" or "a6 6 0 100 12").
    [[nodiscard]] std::optional<bool> flag() {
        skipSeparators();
        if (m_pos < m_text.size() && (m_text[m_pos] == '0' || m_text[m_pos] == '1')) {
            return m_text[m_pos++] == '1';
        }
        return std::nullopt;
    }
    [[nodiscard]] std::size_t position() const noexcept { return m_pos; }

private:
    std::string_view m_text;
    std::size_t m_pos = 0;
};

D2D1_POINT_2F reflect(D2D1_POINT_2F control, D2D1_POINT_2F around) {
    return {2 * around.x - control.x, 2 * around.y - control.y};
}

} // namespace

Result<ComPtr<ID2D1PathGeometry>> buildSvgPath(ID2D1Factory* factory, std::string_view data, bool fillEvenOdd) {
    constexpr auto kCode = ErrorCode::ParseError;
    ComPtr<ID2D1PathGeometry> geometry;
    WL_TRY_HR(factory->CreatePathGeometry(&geometry), ErrorCode::RenderFailure, L"creating path geometry");
    ComPtr<ID2D1GeometrySink> sink;
    WL_TRY_HR(geometry->Open(&sink), ErrorCode::RenderFailure, L"opening geometry sink");
    sink->SetFillMode(fillEvenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);

    Tokenizer tok(data);
    D2D1_POINT_2F current{};
    D2D1_POINT_2F start{};
    D2D1_POINT_2F lastCubicControl{};
    D2D1_POINT_2F lastQuadControl{};
    char previous = 0;
    bool figureOpen = false;
    char cmd = 0;

    auto error = [&](const wchar_t* what) {
        return fail(kCode, what, utf8::toWide(data.substr(0, 60)) + L" @" + std::to_wstring(tok.position()));
    };
    auto beginFigure = [&](D2D1_POINT_2F at) {
        if (figureOpen) {
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        }
        sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED);
        figureOpen = true;
        start = at;
    };
    auto ensureFigure = [&] {
        if (!figureOpen) {
            beginFigure(current);
        }
    };

    while (!tok.atEnd()) {
        if (auto c = tok.command()) {
            cmd = *c;
        } else if (cmd == 0) {
            return error(L"path data must start with a command");
        } else if (cmd == 'M') {
            cmd = 'L'; // implicit line-to after a move-to
        } else if (cmd == 'm') {
            cmd = 'l';
        }
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        auto read = [&](float& v) { if (auto n = tok.number()) { v = *n; return true; } return false; };
        auto readPoint = [&](D2D1_POINT_2F& p) {
            if (!read(p.x) || !read(p.y)) {
                return false;
            }
            if (rel) {
                p.x += current.x;
                p.y += current.y;
            }
            return true;
        };

        switch (op) {
        case 'Z':
            if (figureOpen) {
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                figureOpen = false;
            }
            current = start;
            cmd = 0; // a number may not follow Z
            break;
        case 'M': {
            D2D1_POINT_2F p{};
            if (!readPoint(p)) return error(L"bad move-to");
            current = p;
            beginFigure(p);
            break;
        }
        case 'L': {
            D2D1_POINT_2F p{};
            if (!readPoint(p)) return error(L"bad line-to");
            ensureFigure();
            sink->AddLine(p);
            current = p;
            break;
        }
        case 'H': {
            float x = 0;
            if (!read(x)) return error(L"bad horizontal line");
            ensureFigure();
            current.x = rel ? current.x + x : x;
            sink->AddLine(current);
            break;
        }
        case 'V': {
            float y = 0;
            if (!read(y)) return error(L"bad vertical line");
            ensureFigure();
            current.y = rel ? current.y + y : y;
            sink->AddLine(current);
            break;
        }
        case 'C':
        case 'S': {
            D2D1_POINT_2F c1{};
            D2D1_POINT_2F c2{};
            D2D1_POINT_2F p{};
            if (op == 'C') {
                if (!readPoint(c1)) return error(L"bad cubic");
            } else {
                const char prevOp = static_cast<char>(std::toupper(static_cast<unsigned char>(previous)));
                c1 = (prevOp == 'C' || prevOp == 'S') ? reflect(lastCubicControl, current) : current;
            }
            if (!readPoint(c2) || !readPoint(p)) return error(L"bad cubic");
            ensureFigure();
            sink->AddBezier({c1, c2, p});
            lastCubicControl = c2;
            current = p;
            break;
        }
        case 'Q':
        case 'T': {
            D2D1_POINT_2F c{};
            D2D1_POINT_2F p{};
            if (op == 'Q') {
                if (!readPoint(c)) return error(L"bad quadratic");
            } else {
                const char prevOp = static_cast<char>(std::toupper(static_cast<unsigned char>(previous)));
                c = (prevOp == 'Q' || prevOp == 'T') ? reflect(lastQuadControl, current) : current;
            }
            if (!readPoint(p)) return error(L"bad quadratic");
            ensureFigure();
            sink->AddQuadraticBezier({c, p});
            lastQuadControl = c;
            current = p;
            break;
        }
        case 'A': {
            float rx = 0;
            float ry = 0;
            float rotation = 0;
            if (!read(rx) || !read(ry) || !read(rotation)) return error(L"bad arc");
            const auto large = tok.flag();
            const auto sweep = tok.flag();
            D2D1_POINT_2F p{};
            if (!large || !sweep || !readPoint(p)) return error(L"bad arc");
            ensureFigure();
            if (rx == 0 || ry == 0) {
                sink->AddLine(p);
            } else {
                // y-down coordinates: SVG sweep-flag 1 is clockwise on screen.
                sink->AddArc({p, {std::abs(rx), std::abs(ry)}, rotation,
                              *sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                              *large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
            }
            current = p;
            break;
        }
        default:
            return error(L"unsupported path command");
        }
        previous = cmd;
    }
    if (figureOpen) {
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    WL_TRY_HR(sink->Close(), kCode, L"closing geometry sink");
    return geometry;
}

} // namespace wl::ui
