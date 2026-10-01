#include "app/pages/images/EditionSelection.h"

#include "base/Text.h"

#include <algorithm>

namespace wl::app {

bool EditionFilter::matches(const core::ImageInfo& image) const {
    if (!arch.empty() && std::wstring_view(core::architectureName(image.architecture)) != arch) {
        return false;
    }
    const std::wstring needle = wl::text::lower(text);
    if (needle.find_first_not_of(L' ') == std::wstring::npos) {
        return true;
    }
    const std::wstring hay = wl::text::lower(image.name + L"\n" + image.displayName + L"\n" + image.editionId + L"\n" + image.description +
                                     L"\n" + std::to_wstring(image.index));
    return hay.find(needle) != std::wstring::npos;
}

bool EditionSelection::contains(int row) const {
    return std::ranges::binary_search(rows, row);
}

void EditionSelection::only(int row) {
    rows = {row};
    primary = row;
    anchor = row;
}

void EditionSelection::toggle(int row) {
    const auto at = std::ranges::lower_bound(rows, row);
    if (at == rows.end() || *at != row) {
        rows.insert(at, row);
        primary = row;
        anchor = row;
        return;
    }
    if (rows.size() == 1) {
        return;
    }
    const auto next = rows.erase(at);
    if (primary == row) {
        primary = next != rows.end() ? *next : rows.back(); // the one after it, else the last
    }
    anchor = primary;
}

void EditionSelection::extendTo(int row) {
    const int from = anchor < 0 ? row : anchor;
    rows.clear();
    for (int i = std::min(from, row); i <= std::max(from, row); ++i) {
        rows.push_back(i);
    }
    primary = row;
    anchor = from;
}

void EditionSelection::all(int count) {
    rows.clear();
    for (int i = 0; i < count; ++i) {
        rows.push_back(i);
    }
    if (primary < 0 || primary >= count) {
        primary = count > 0 ? 0 : -1;
    }
    anchor = primary;
}

} // namespace wl::app
