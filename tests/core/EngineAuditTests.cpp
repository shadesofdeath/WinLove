// docs/AUDIT-2026-11-10.md, engine section (ENG-02 … ENG-16): the behaviour its fixes changed, pinned.
// No admin, no image, no network: Windows' own cmd.exe / ping.exe and hand-made files in memory.
#include "core/image/icons/IconGroups.h"
#include "core/image/icons/PeResources.h"
#include "core/system/Process.h"
#include "core/tasks/Task.h"
#include "core/tasks/TaskRunner.h"

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

template <class T>
void poke(std::string& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

constexpr std::uint32_t kNt = 0x40;
constexpr std::uint32_t kOptional = kNt + 24;
constexpr std::uint32_t kSections = kOptional + 240;
constexpr std::uint32_t kRaw = 0x200;
constexpr std::uint32_t kSectionSize = 0x400;
constexpr std::uint32_t kVa = 0x1000;

// A PE32+ file of one section, ".rsrc", holding one resource (type 10, name 1) in `languages`
// languages. All of them name the same data entry: `dataSize` bytes from `dataAt` in the section.
std::string tinyPe(std::uint16_t languages, std::uint32_t dataAt, std::uint32_t dataSize) {
    std::string pe(kRaw + kSectionSize, '\0');
    poke<std::uint16_t>(pe, 0, 0x5A4D);
    poke<std::uint32_t>(pe, 0x3C, kNt);
    poke<std::uint32_t>(pe, kNt, 0x00004550);
    poke<std::uint16_t>(pe, kNt + 4, 0x8664);
    poke<std::uint16_t>(pe, kNt + 6, 1);     // sections
    poke<std::uint16_t>(pe, kNt + 20, 240);  // size of the optional header
    poke<std::uint16_t>(pe, kOptional, 0x20B);
    poke<std::uint32_t>(pe, kOptional + 32, 0x1000); // section alignment
    poke<std::uint32_t>(pe, kOptional + 36, 0x200);  // file alignment
    poke<std::uint32_t>(pe, kOptional + 56, kVa + 0x1000);
    poke<std::uint32_t>(pe, kOptional + 60, kRaw);   // size of headers
    poke<std::uint32_t>(pe, kOptional + 108, 16);    // data directories
    poke<std::uint32_t>(pe, kOptional + 112 + 2 * 8, kVa);
    poke<std::uint32_t>(pe, kOptional + 112 + 2 * 8 + 4, kSectionSize);
    std::memcpy(pe.data() + kSections, ".rsrc", 5);
    poke<std::uint32_t>(pe, kSections + 8, kSectionSize);
    poke<std::uint32_t>(pe, kSections + 12, kVa);
    poke<std::uint32_t>(pe, kSections + 16, kSectionSize);
    poke<std::uint32_t>(pe, kSections + 20, kRaw);
    poke<std::uint32_t>(pe, kSections + 36, 0x40000040u);
    // Root → type 10 (at 0x18) → name 1 (at 0x30) → the languages (entries from 0x40).
    poke<std::uint16_t>(pe, kRaw + 14, 1);
    poke<std::uint32_t>(pe, kRaw + 16, 10);
    poke<std::uint32_t>(pe, kRaw + 20, 0x80000018u);
    poke<std::uint16_t>(pe, kRaw + 0x18 + 14, 1);
    poke<std::uint32_t>(pe, kRaw + 0x28, 1);
    poke<std::uint32_t>(pe, kRaw + 0x2C, 0x80000030u);
    poke<std::uint16_t>(pe, kRaw + 0x30 + 14, languages);
    const std::uint32_t entry = 0x40 + 8u * languages;
    for (std::uint32_t i = 0; i < languages; ++i) {
        poke<std::uint32_t>(pe, kRaw + 0x40 + 8 * i, 0x409u + i);
        poke<std::uint32_t>(pe, kRaw + 0x44 + 8 * i, entry);
    }
    poke<std::uint32_t>(pe, kRaw + entry, kVa + dataAt);
    poke<std::uint32_t>(pe, kRaw + entry + 4, dataSize);
    return pe;
}

std::wstring quoted(const std::wstring& path) {
    return L"\"" + path + L"\"";
}

} // namespace

TEST_CASE("audit ENG-02: a tool's output arrives whole and its exit code comes back") {
    const auto cmd = systemTool(L"cmd.exe");
    REQUIRE(cmd);
    std::string output;
    const auto exit = runProcess(quoted(*cmd) + L" /d /c \"echo hello& exit 7\"", [&](std::string_view text) { output += text; });
    REQUIRE(exit);
    CHECK(*exit == 7u);
    CHECK(output.find("hello") != std::string::npos);

    // Outside the kill-on-close job (a tool that services an image or writes a disk): the same.
    std::string outside;
    const auto outlived = runProcess(
        quoted(*cmd) + L" /d /c \"echo hello& exit 7\"", [&](std::string_view text) { outside += text; }, nullptr,
        ProcessLife::Outlives);
    REQUIRE(outlived);
    CHECK(*outlived == 7u);
    CHECK(outside.find("hello") != std::string::npos);
    // Such a tool is never cut short: a token handed with it is ignored.
    const CancelToken stopped;
    stopped.cancel();
    const auto uncut = runProcess(quoted(*cmd) + L" /d /c \"exit 7\"", {}, &stopped, ProcessLife::Outlives);
    REQUIRE(uncut);
    CHECK(*uncut == 7u);

    // Far more than a pipe holds: the tool must never wait on a reader that waits on it. With a
    // token the pipe is polled instead of read blocking; both ways get every line.
    const std::wstring chatty =
        quoted(*cmd) + L" /d /c \"for /L %i in (1,1,3000) do @echo 0123456789012345678901234567890123456789\"";
    for (const bool withToken : {false, true}) {
        const CancelToken token;
        std::string lines;
        const auto done = runProcess(chatty, [&](std::string_view text) { lines += text; }, withToken ? &token : nullptr);
        REQUIRE(done);
        CHECK(*done == 0u);
        CHECK(lines.size() >= 3000u * 40u);
    }
}

TEST_CASE("audit ENG-02: a cancelled token ends the tool and the result says Cancelled") {
    const auto ping = systemTool(L"ping.exe");
    REQUIRE(ping);
    const std::wstring minute = quoted(*ping) + L" -n 60 127.0.0.1";

    const CancelToken before;
    before.cancel();
    const auto notStarted = runProcess(minute, {}, &before);
    REQUIRE_FALSE(notStarted);
    CHECK(notStarted.error().code == ErrorCode::Cancelled);

    const CancelToken token;
    const auto begun = std::chrono::steady_clock::now();
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        token.cancel();
    });
    const auto stopped = runProcess(minute, {}, &token);
    canceller.join();
    const auto took = std::chrono::steady_clock::now() - begun;
    REQUIRE_FALSE(stopped);
    CHECK(stopped.error().code == ErrorCode::Cancelled);
    CHECK(took < std::chrono::seconds(20)); // the ping alone runs a minute
}

TEST_CASE("audit ENG-16: the runner outlives anything `done` throws, and drain() from a job returns") {
    TaskRunner runner;
    std::atomic<bool> second{false};
    std::atomic<bool> drained{false};
    runner.run<int>([](const TaskContext&) -> Result<int> { return 1; }, [](Result<int>) { throw 42; });
    runner.run<int>(
        [&](const TaskContext&) -> Result<int> {
            runner.drain(); // waiting for its own end: it used to never wake
            drained = true;
            return 2;
        },
        [&](Result<int>) { second = true; });
    runner.drain();
    CHECK(drained);
    CHECK(second);
}

TEST_CASE("audit ENG-04: a hand-made PE is rebuilt, and headers that point outside the file are refused") {
    auto pe = PeImage::parse(tinyPe(4, 0x80, 16));
    REQUIRE(pe);
    CHECK(pe->resources().leafCount() == 4);
    const auto rebuilt = pe->build();
    REQUIRE(rebuilt);
    const auto again = PeImage::parse(*rebuilt);
    REQUIRE(again);
    CHECK(again->resources().leafCount() == 4);

    // SizeOfHeaders past the end of the file.
    std::string tall = tinyPe(4, 0x80, 16);
    poke<std::uint32_t>(tall, kOptional + 60, 0x10000);
    const auto tallPe = PeImage::parse(tall);
    REQUIRE(tallPe);
    CHECK_FALSE(tallPe->build());

    // Bytes after the last section and a signature said to start far past the end: the check of
    // what lies between the two used to read to that offset.
    std::string cut = tinyPe(4, 0x80, 16);
    cut.append(16, '\0');
    poke<std::uint32_t>(cut, kOptional + 112 + 4 * 8, 0x100000);
    poke<std::uint32_t>(cut, kOptional + 112 + 4 * 8 + 4, 8);
    const auto cutPe = PeImage::parse(cut);
    REQUIRE(cutPe);
    CHECK_FALSE(cutPe->build());
}

TEST_CASE("audit ENG-05: entries that all name one large block are refused while reading") {
    // Four languages, each the whole section: 4 × 0x400 bytes out of a 0x600 byte file.
    const auto bomb = PeImage::parse(tinyPe(4, 0, kSectionSize));
    REQUIRE_FALSE(bomb);
    CHECK(bomb.error().code == ErrorCode::ParseError);
    // The same four sharing a small block is an ordinary file.
    CHECK(PeImage::parse(tinyPe(4, 0x80, 16)));
}

TEST_CASE("audit ENG-09: the icon groups as views are the groups listIconGroups copies") {
    ResourceTree tree;
    auto& icons = tree.ensureType(kRtIcon);
    ensureName(icons, 1).languages.push_back({1033, 0, std::string("the first picture")});
    ensureName(icons, 2).languages.push_back({1033, 0, std::string("the second")});
    auto& groups = tree.ensureType(kRtGroupIcon);
    auto dir = [](std::vector<std::uint16_t> ids) {
        std::string d(6 + ids.size() * 14, '\0');
        d[2] = 1;
        d[4] = static_cast<char>(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            d[6 + i * 14] = 16;
            d[6 + i * 14 + 1] = 16;
            d[6 + i * 14 + 4] = 1;
            d[6 + i * 14 + 6] = 32;
            std::memcpy(d.data() + 6 + i * 14 + 12, &ids[i], 2);
        }
        return d;
    };
    ensureName(groups, 10).languages.push_back({1033, 0, dir({1, 2})});
    ensureName(groups, 11).languages.push_back({1033, 0, dir({2})});
    ensureName(groups, 12).languages.push_back({1033, 0, dir({9})}); // its picture is missing

    const auto copies = listIconGroups(tree);
    const auto views = listIconGroupData(tree);
    REQUIRE(views.size() == 3);
    REQUIRE(copies.size() == views.size());
    for (std::size_t i = 0; i < views.size(); ++i) {
        CHECK((views[i].key == copies[i].key));
        REQUIRE(views[i].images.size() == copies[i].images.size());
        for (std::size_t j = 0; j < views[i].images.size(); ++j) {
            CHECK((views[i].images[j] == std::string_view(copies[i].images[j].data)));
        }
    }
    CHECK(views[0].images.size() == 2);
    CHECK(views[2].images.empty());
}
