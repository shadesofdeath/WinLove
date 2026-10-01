#pragma once
// Which rows of the editions table are marked (P02, several at once): the rules of a list with
// check boxes — a click takes one row, Ctrl+click or the row's box adds / removes it, Shift
// extends from where the last plain click was, Ctrl+A takes all. One marked row is the primary
// one (the inspector shows it). No widget in here: EditionTable feeds it rows.
#include "core/image/ImageInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

// D-058: the Images page search box and architecture filter. The text is matched without case
// against the name, display name, edition ID, description and index; `arch` is "", "x64", "arm64"
// or "x86" ("" = all).
struct EditionFilter {
    std::wstring text;
    std::wstring_view arch;

    [[nodiscard]] bool matches(const core::ImageInfo& image) const;
};

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
