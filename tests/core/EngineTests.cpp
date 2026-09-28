// Engine unit tests: WIM XML parsing, header validation, UDF rejection, log ring buffer, tasks.
#include "base/Log.h"
#include "core/image/UdfImage.h"
#include "core/image/WimFile.h"
#include "core/tasks/TaskRunner.h"

#include <doctest.h>

#include <atomic>
#include <cstring>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<std::byte> data) : m_data(std::move(data)) {}
    std::uint64_t size() const override { return m_data.size(); }
    Result<void> read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset + out.size() > m_data.size()) {
            return fail(ErrorCode::IoError, L"past end");
        }
        std::memcpy(out.data(), m_data.data() + offset, out.size());
        return {};
    }

private:
    std::vector<std::byte> m_data;
};

// Shape of real install.wim metadata (trimmed).
constexpr char16_t kXml[] = u"﻿<WIM><TOTALBYTES>7217280579</TOTALBYTES>"
                            u"<IMAGE INDEX=\"1\"><DIRCOUNT>31000</DIRCOUNT><FILECOUNT>150000</FILECOUNT>"
                            u"<TOTALBYTES>23579823423</TOTALBYTES>"
                            u"<WINDOWS><ARCH>9</ARCH><EDITIONID>Core</EDITIONID><INSTALLATIONTYPE>Client</INSTALLATIONTYPE>"
                            u"<LANGUAGES><LANGUAGE>tr-TR</LANGUAGE><DEFAULT>tr-TR</DEFAULT></LANGUAGES>"
                            u"<VERSION><MAJOR>10</MAJOR><MINOR>0</MINOR><BUILD>26200</BUILD><SPBUILD>8037</SPBUILD></VERSION>"
                            u"</WINDOWS><NAME>Windows 11 Home</NAME><DISPLAYNAME>Windows 11 Home</DISPLAYNAME></IMAGE>"
                            u"<IMAGE INDEX=\"2\"><TOTALBYTES>0x10</TOTALBYTES><WINDOWS><ARCH>12</ARCH></WINDOWS>"
                            u"<NAME>İşçi Sürümü</NAME></IMAGE></WIM>";

} // namespace

TEST_CASE("WIM XML: editions, version, languages, sizes (decimal and hex)") {
    auto images = parseWimXml(kXml);
    REQUIRE(images.has_value());
    REQUIRE(images->size() == 2);
    const auto& home = (*images)[0];
    CHECK(home.index == 1);
    CHECK(home.name == L"Windows 11 Home");
    CHECK(home.editionId == L"Core");
    CHECK(home.architecture == Architecture::X64);
    CHECK(home.versionString() == L"10.0.26200.8037");
    CHECK(home.defaultLanguage == L"tr-TR");
    CHECK(home.languages.size() == 1);
    CHECK(home.totalBytes == 23579823423ull);
    CHECK(home.fileCount == 150000);
    const auto& second = (*images)[1];
    CHECK(second.architecture == Architecture::Arm64);
    CHECK(second.totalBytes == 16);
    CHECK(second.name == L"İşçi Sürümü"); // non-ASCII survives UTF-16 -> UTF-8 -> UTF-16
}

TEST_CASE("WIM XML: malformed documents are errors, not crashes") {
    CHECK(parseWimXml(u"<WIM><IMAGE").error().code == ErrorCode::ParseError);
    CHECK(parseWimXml(u"").error().code == ErrorCode::ParseError);
}

TEST_CASE("WIM header: rejects non-WIM data") {
    MemorySource junk(std::vector<std::byte>(4096, std::byte{0x41}));
    CHECK(readWimHeader(junk).error().code == ErrorCode::ParseError);
    MemorySource tiny(std::vector<std::byte>(16));
    CHECK(readWimHeader(tiny).error().code == ErrorCode::ParseError);
}

TEST_CASE("WIM: header + XML round trip from a synthetic file") {
    // 208-byte header, XML right after it.
    const std::size_t xmlBytes = (std::char_traits<char16_t>::length(kXml)) * 2;
    std::vector<std::byte> file(208 + xmlBytes);
    std::memcpy(file.data(), "MSWIM\0\0\0", 8);
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(file.data() + at, &v, 4); };
    auto put64 = [&](std::size_t at, std::uint64_t v) { std::memcpy(file.data() + at, &v, 8); };
    put32(12, 0x10D00);           // version
    put32(16, 0x2 | 0x40000);     // compressed, LZX
    put32(44, 2);                 // image count
    put64(72, xmlBytes);          // XML resource: size (flags 0 = uncompressed)
    put64(80, 208);               // offset
    put64(88, xmlBytes);          // original size
    std::memcpy(file.data() + 208, kXml, xmlBytes);

    MemorySource source(std::move(file));
    auto wim = readWim(source);
    REQUIRE_MESSAGE(wim.has_value(), describe(wim.error()).c_str());
    CHECK(wim->header.compression == WimCompression::Lzx);
    CHECK_FALSE(wim->header.solid);
    CHECK(wim->images.size() == 2);
}

TEST_CASE("UDF: a file without an anchor is reported as unsupported") {
    auto image = UdfImage::open(std::make_shared<MemorySource>(std::vector<std::byte>(300 * 2048)));
    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().code == ErrorCode::Unsupported);
}

TEST_CASE("log: ring buffer keeps the newest entries in order") {
    auto ring = std::make_shared<log::RingBufferSink>(3);
    log::addSink(ring);
    for (int i = 0; i < 5; ++i) {
        log::info("test", std::to_wstring(i));
    }
    log::removeSink(ring);
    const auto entries = ring->snapshot();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].message == L"2");
    CHECK(entries[2].message == L"4");
    CHECK(ring->version() == 5);
    CHECK(log::formatLine(entries[2]).find(L"INFO  test  4") != std::wstring::npos);
}

TEST_CASE("TaskRunner: FIFO on one engine thread, results and cancellation") {
    TaskRunner runner;
    std::vector<int> order;
    std::atomic<bool> onEngine{true};
    for (int i = 0; i < 5; ++i) {
        runner.run<int>(
            [&, i](const TaskContext&) -> Result<int> {
                onEngine = onEngine && runner.onEngineThread();
                return i;
            },
            [&](Result<int> r) { order.push_back(r.value()); });
    }
    runner.drain();
    CHECK(order == std::vector<int>{0, 1, 2, 3, 4});
    CHECK(onEngine);

    // Cancelled before it starts: `done` gets Cancelled, work never runs.
    bool ran = false;
    ErrorCode code = ErrorCode::Unknown;
    auto blocker = runner.run<int>([](const TaskContext&) -> Result<int> { Sleep(50); return 0; }, nullptr);
    auto token = runner.run<int>([&](const TaskContext&) -> Result<int> { ran = true; return 1; },
                                 [&](Result<int> r) { code = r ? ErrorCode::Unknown : r.error().code; });
    token.cancel();
    runner.drain();
    CHECK_FALSE(ran);
    CHECK(code == ErrorCode::Cancelled);
    (void)blocker;
}

TEST_CASE("CancelToken: shared state and Win32 event") {
    CancelToken token;
    CancelToken copy = token;
    CHECK_FALSE(copy.cancelled());
    CHECK(WaitForSingleObject(copy.event(), 0) == WAIT_TIMEOUT);
    token.cancel();
    CHECK(copy.cancelled());
    CHECK(WaitForSingleObject(copy.event(), 0) == WAIT_OBJECT_0);
    CHECK(copy.check(L"x").error().code == ErrorCode::Cancelled);
}
