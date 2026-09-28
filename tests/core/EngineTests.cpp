// Engine unit tests: WIM XML parsing, header validation, UDF rejection, log ring buffer, tasks.
#include "base/Log.h"
#include "base/Path.h"
#include "core/image/UdfImage.h"
#include "core/image/WimFile.h"
#include "core/image/dism/DismErrors.h"
#include "core/image/dism/MountHealth.h"
#include "core/system/FileLocks.h"
#include "core/tasks/TaskRunner.h"

#include <doctest.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <optional>
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
                            u"<CREATIONTIME><HIGHPART>0x01DB0000</HIGHPART><LOWPART>0x00000010</LOWPART></CREATIONTIME>"
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
    CHECK(home.creationTime == ((0x01DB0000ull << 32) | 0x10));
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

TEST_CASE("mount health: DISM status + our checks → state → action") {
    using wl::core::DismMountStatus;
    using wl::core::MountAction;
    using wl::core::MountState;
    auto record = [](DismMountStatus status) {
        return std::optional<wl::core::MountInfo>(wl::core::MountInfo{L"C:/WinLove/mount", L"C:/x/install.wim", 1, false, status});
    };
    CHECK(wl::core::classifyMount(std::nullopt, false, false) == MountState::Free);
    CHECK(wl::core::classifyMount(std::nullopt, false, true) == MountState::Orphaned);
    CHECK(wl::core::classifyMount(record(DismMountStatus::Ok), true, true) == MountState::Ok);
    CHECK(wl::core::classifyMount(record(DismMountStatus::Ok), false, true) == MountState::ImageMissing);
    CHECK(wl::core::classifyMount(record(DismMountStatus::NeedsRemount), true, true) == MountState::NeedsRemount);
    CHECK(wl::core::classifyMount(record(DismMountStatus::NeedsRemount), false, true) == MountState::ImageMissing);
    CHECK(wl::core::classifyMount(record(DismMountStatus::Invalid), true, true) == MountState::Invalid);

    CHECK(wl::core::recommendedAction(MountState::Free) == MountAction::None);
    CHECK(wl::core::recommendedAction(MountState::Ok) == MountAction::None);
    CHECK(wl::core::recommendedAction(MountState::NeedsRemount) == MountAction::Remount);
    CHECK(wl::core::recommendedAction(MountState::Invalid) == MountAction::Discard);
    CHECK(wl::core::recommendedAction(MountState::ImageMissing) == MountAction::Discard);
    CHECK(wl::core::recommendedAction(MountState::Orphaned) == MountAction::ClearFolder);
}

TEST_CASE("mount health: no hives are loaded from a folder that is not mounted") {
    CHECK(wl::core::hivesLoadedFrom(L"C:/WinLoveLab/no-such-mount").empty());
}

TEST_CASE("DISM/WIM error catalog: codes from wimgapi/WimProvider map to a remedy") {
    using wl::core::Remedy;
    CHECK(wl::core::explainError(static_cast<std::int32_t>(0xC1420117)).remedy == Remedy::CloseOpenFiles);
    CHECK(wl::core::explainError(static_cast<std::int32_t>(0xC1420114)).remedy == Remedy::RepairFolder);
    CHECK(wl::core::explainError(static_cast<std::int32_t>(0xC142013C)).remedy == Remedy::ConvertEsd);
    CHECK(wl::core::explainError(static_cast<std::int32_t>(0xC1510114)).remedy == Remedy::Remount);
    CHECK(wl::core::explainError(static_cast<std::int32_t>(0x80070005)).remedy == Remedy::CloseOpenFiles);
    CHECK_FALSE(wl::core::explainError(0x12345).known);
    // Log-only text from the system message tables (may be empty on some installs): must not throw.
    CHECK_NOTHROW((void)wl::core::systemMessage(static_cast<std::int32_t>(0xC1420117)));
}

TEST_CASE("forceRemoveContents: clears leftovers, removes junctions without following them") {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / L"wl-tests" / L"locks";
    const fs::path folder = base / L"mount";
    const fs::path outside = base / L"outside";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(folder / L"Windows" / L"System32", ec);
    fs::create_directories(outside, ec);
    std::ofstream(outside / L"keep.txt") << "keep";
    const fs::path readOnly = folder / L"Windows" / L"System32" / L"ro.txt";
    std::ofstream(readOnly) << "x";
    SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
    // A junction inside the mount folder pointing outside it.
    const std::wstring command = L"cmd /c mklink /J \"" + (folder / L"link").wstring() + L"\" \"" + outside.wstring() +
                                 L"\" >nul";
    REQUIRE(_wsystem(command.c_str()) == 0);

    REQUIRE(wl::core::forceRemoveContents(folder));
    CHECK(fs::exists(folder));
    CHECK(fs::is_empty(folder));
    CHECK(fs::exists(outside / L"keep.txt")); // the junction target is untouched
    fs::remove_all(base, ec);
}

TEST_CASE("forceRemoveContents refuses folders near the drive root") {
    CHECK_FALSE(wl::core::forceRemoveContents(L"C:\\"));
    CHECK_FALSE(wl::core::forceRemoveContents(L"C:\\WinLove"));
    CHECK_FALSE(wl::core::forceRemoveContents(L"relative\\path"));
}

TEST_CASE("no Explorer window shows a folder that does not exist") {
    CHECK(wl::core::explorerWindowsIn(L"C:\\WinLoveLab\\no-such-folder\\x").empty());
}

TEST_CASE("nativePath: absolute, backslashes only (DISM rejects mixed separators)") {
    CHECK(wl::nativePath(L"C:/WinLoveLab/iso/sources/install.wim").wstring() == LR"(C:\WinLoveLab\iso\sources\install.wim)");
    CHECK(wl::nativePath(std::filesystem::path(L"C:/WinLoveLab/iso") / L"sources/install.wim").wstring() ==
          LR"(C:\WinLoveLab\iso\sources\install.wim)");
    CHECK(wl::nativePath(LR"(C:\WinLove\.\mount\)").wstring() == LR"(C:\WinLove\mount\)");
}
