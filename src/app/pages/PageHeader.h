#pragma once
// D-139: the header zone every working page has at the top of its card — the filter / search at the
// left (the page's own controls), the page's live summary at the right, on one control-high row:
//   "1.312 bileşen · 9,80 GB · 13 kaldırılacak · 1,05 GB"
// What the list has is quiet (text.tertiary); what Uygula will change is in the readable accent
// (accent.text, as every "değiştirilecek" word since D-137); a problem in the warning colour. The
// parts are set when the page's AppState notifications come (never in paint); their widths are
// measured in layout(), so paint only draws. When the row is short the quiet facts go first, then the
// rest takes its short form, then is cut with "…".
#include "ui/widget/Widget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wl::app {

// The header row's height and the room under it before the table (the same on every page).
inline constexpr float kHeaderRow = ui::tokens::size::control;
inline constexpr float kHeaderGap = ui::tokens::spacing::s5;
inline constexpr float kHeaderControlGap = ui::tokens::spacing::s4 + ui::tokens::spacing::s1; // between two filter controls

struct SummaryPart {
    enum class Tone : std::uint8_t { Quiet, Change, Warning };
    std::wstring text;
    Tone tone = Tone::Quiet;
    std::wstring shortText; // what it says when the row is short (beside the detail card); empty: text
    bool operator==(const SummaryPart&) const = default;
};

class SummaryLine : public ui::Widget {
public:
    SummaryLine();
    void set(std::vector<SummaryPart> parts);
    [[nodiscard]] const std::vector<SummaryPart>& parts() const noexcept { return m_parts; }
    // The whole line as it reads ("a · b · c"; tests, accessibility).
    [[nodiscard]] std::wstring text() const;
    // What layout() placed: the parts that fit, in their short form where they took it (tests).
    [[nodiscard]] std::wstring shownText() const;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    std::vector<SummaryPart> m_parts;
    struct Placed {
        float x = 0;
        float width = 0;
        bool shown = false;
        bool brief = false; // its short form
    };
    std::vector<Placed> m_placed; // per part (layout)
    float m_dotWidth = 0;         // " · "
};

} // namespace wl::app
