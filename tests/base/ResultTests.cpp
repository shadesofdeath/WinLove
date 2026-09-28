#include "base/Log.h"
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

TEST_CASE("RingBufferSink::since returns only new entries, oldest first, also after wrap-around") {
    wl::log::RingBufferSink sink(4);
    auto entry = [](int i) {
        return wl::log::Entry{std::chrono::system_clock::now(), wl::log::Level::Info, "t", std::to_wstring(i), 0};
    };
    std::uint64_t version = 0;
    sink.write(entry(1));
    sink.write(entry(2));
    auto first = sink.since(version);
    REQUIRE(first.size() == 2);
    CHECK(first[0].message == L"1");
    CHECK(sink.since(version).empty());
    for (int i = 3; i <= 7; ++i) {
        sink.write(entry(i));
    }
    // 5 new entries but only 4 fit: 4..7 survive, in order.
    auto second = sink.since(version);
    REQUIRE(second.size() == 4);
    CHECK(second.front().message == L"4");
    CHECK(second.back().message == L"7");
    sink.write(entry(8));
    auto third = sink.since(version);
    REQUIRE(third.size() == 1);
    CHECK(third[0].message == L"8");
}
