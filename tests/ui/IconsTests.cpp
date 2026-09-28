#include "ui/generated/Icons.g.h"

#include <doctest.h>

#include <cstring>
#include <set>
#include <string>

using namespace wl::ui::icons;

TEST_CASE("icon table is complete and well-formed") {
    CHECK(kIconCount == static_cast<std::size_t>(Icon::Count));
    std::set<std::string> names;
    for (const auto& icon : kIcons) {
        CAPTURE(icon.name);
        CHECK(names.insert(icon.name).second);
        for (const auto* variant : {&icon.regular16, &icon.filled16, &icon.regular24}) {
            REQUIRE(variant->pathData != nullptr);
            CHECK(variant->pathData[0] == 'M'); // every path starts with a move-to
            CHECK(variant->strokeWidth > 0.0f);
        }
    }
}

TEST_CASE("icons the shell needs exist") {
    CHECK(std::strcmp(kIcons[static_cast<std::size_t>(Icon::CaptionClose)].name, "caption-close") == 0);
    CHECK(std::strcmp(kIcons[static_cast<std::size_t>(Icon::Search)].name, "search") == 0);
}
