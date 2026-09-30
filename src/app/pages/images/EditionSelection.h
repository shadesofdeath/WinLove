#pragma once
// Which rows of the editions table are marked (P02, several at once): the rules of a list with
// check boxes — a click takes one row, Ctrl+click or the row's box adds / removes it, Shift
// extends from where the last plain click was, Ctrl+A takes all. One marked row is the primary
// one (the inspector shows it). No widget in here: EditionTable feeds it rows.
#include <vector>

namespace wl::app {

struct EditionSelection {
    std::vector<int> rows; // marked rows, ascending
    int primary = -1;
    int anchor = -1;       // where a Shift range starts

    void only(int row);       // click, arrow keys
    void toggle(int row);     // Ctrl+click, the check box; the last marked row stays marked
    void extendTo(int row);   // Shift+click, Shift+arrow: anchor … row
    void all(int count);      // Ctrl+A
    [[nodiscard]] bool contains(int row) const;
};

} // namespace wl::app
