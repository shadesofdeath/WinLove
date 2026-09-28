#include "app/Localization.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using wl::app::Localization;
using wl::app::Str;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::filesystem::path stringsDir() {
    return std::filesystem::path(WL_SOURCE_DIR) / L"resources" / L"strings";
}

} // namespace

TEST_CASE("both language files load with every key") {
    for (const auto* file : {L"tr.json", L"en.json"}) {
        CAPTURE(std::filesystem::path(file).string());
        const auto loc = Localization::fromJson(readFile(stringsDir() / file));
        REQUIRE_MESSAGE(loc.has_value(), wl::describe(loc.error()).c_str());
        CHECK_FALSE(loc->get(Str::AppName).empty());
    }
}

TEST_CASE("Turkish text survives UTF-8 decoding") {
    const auto tr = Localization::fromJson(readFile(stringsDir() / L"tr.json"));
    REQUIRE(tr.has_value());
    CHECK(tr->get(Str::NavImages) == L"İmajlar");
}

TEST_CASE("format replaces placeholders") {
    const auto en = Localization::fromJson(readFile(stringsDir() / L"en.json"));
    REQUIRE(en.has_value());
    CHECK(en->format(Str::CommonItems, {{L"n", L"12"}}) == L"12 items");
    CHECK(en->format(Str::ImagesMounting, {{L"edition", L"Pro"}}) == L"Mounting Pro");
    CHECK(en->format(Str::CommonItems, {}) == L"{n} items");
}

TEST_CASE("fromJson reports missing and unknown keys") {
    CHECK(Localization::fromJson("not json").error().code == wl::ErrorCode::ParseError);
    CHECK(Localization::fromJson(R"({"app":{"name":"x"}})").error().code == wl::ErrorCode::NotFound);
}
