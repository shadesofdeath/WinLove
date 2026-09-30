// Removing editions from a WIM: the index arithmetic and the refusals. The rewrite itself needs
// a real image (capturing a WIM takes admin): tools/lab_editions.ps1 runs it on the lab copy.
#include "core/image/wim/WimGapi.h"

#include <doctest.h>

#include <filesystem>
#include <vector>

using namespace wl;
using namespace wl::core;

TEST_CASE("indexAfterRemoval: what stays is renumbered from 1") {
    // Six editions, Home Single Language (2) goes: Pro moves from 4 to 3.
    const std::vector<int> one{2};
    CHECK(indexAfterRemoval(1, one) == 1);
    CHECK(indexAfterRemoval(2, one) == std::nullopt);
    CHECK(indexAfterRemoval(4, one) == 3);
    CHECK(indexAfterRemoval(6, one) == 5);

    // "Keep only Pro": everything but 4 goes, in any order, duplicates ignored.
    const std::vector<int> others{6, 1, 2, 3, 5, 5};
    CHECK(indexAfterRemoval(4, others) == 1);
    CHECK(indexAfterRemoval(5, others) == std::nullopt);

    CHECK(indexAfterRemoval(3, {}) == 3);
}

TEST_CASE("removeImages: a missing file is an error, not a silent success") {
    const auto missing = std::filesystem::temp_directory_path() / L"wl-tests" / L"no-such-image.wim";
    const std::vector<int> indexes{1};
    const auto removed = removeImages(missing, indexes, TaskContext{});
    CHECK_FALSE(removed.has_value());
}
