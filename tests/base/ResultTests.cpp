#include "base/Result.h"
#include "base/Utf8.h"

#include <doctest.h>

namespace {

wl::Result<int> parsePositive(int value) {
    if (value <= 0) {
        return wl::fail(wl::ErrorCode::InvalidArgument, L"must be positive", L"parsePositive");
    }
    return value;
}

} // namespace

TEST_CASE("Result carries a value or an error") {
    CHECK(parsePositive(3).value() == 3);

    const auto bad = parsePositive(-1);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == wl::ErrorCode::InvalidArgument);
    CHECK(bad.error().context == L"parsePositive");
}

TEST_CASE("describe formats code, message, context and HRESULT") {
    const wl::Error error{wl::ErrorCode::NotFound, L"install.wim missing", L"opening x.iso",
                          static_cast<std::int32_t>(0x80070002)};
    CHECK(wl::describe(error) == L"NotFound: install.wim missing (opening x.iso) [0x80070002]");
    CHECK(wl::describe(wl::Error{}) == L"Unknown");
}

TEST_CASE("UTF-8 round trip keeps Turkish characters") {
    const std::wstring wide = L"Windows imaj özelleştirme · ğĞıİşŞ";
    CHECK(wl::utf8::toWide(wl::utf8::fromWide(wide)) == wide);
    CHECK(wl::utf8::toWide("") == L"");
}
