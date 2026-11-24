// docs/AUDIT-2026-11-10.md, the core/ops … store section: what its second round of fixes changed, pinned.
#include "core/ops/Compat.h"
#include "core/uup/PsfPackage.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path opsAuditFolder(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"ops-audit" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

TEST_CASE("audit OPS-10: a component's recipe is read once for all passes, with the same conflicts") {
    using namespace wl::core::ops;
    // "printing" keeps the spooler; the first component takes it along, the second does not.
    const std::vector<CompatGuard> guards{{L"printing", {}, {}, {L"Spooler"}}};
    ChangeSet queue;
    queue.add({OpKind::RemoveComponent, L"print-stack", LR"({"services":["spooler"]})", Risk::Medium, 0});
    queue.add({OpKind::RemoveComponent, L"wordpad", LR"({"services":["SomethingElse"]})", Risk::Low, 0});
    queue.add({OpKind::RemoveComponent, L"broken", L"not json", Risk::Low, 0});
    queue.add({OpKind::SetServiceStart, L"Spooler", L"disabled", Risk::Low, 0});

    const auto conflicts = compatConflicts(queue, guards, {});
    REQUIRE(conflicts.size() == 2);
    CHECK(conflicts[0].op.target == L"print-stack");
    CHECK(conflicts[0].block.guards == std::vector<std::wstring>{L"printing"});
    CHECK(conflicts[1].op.target == L"Spooler");
    // The one-operation question gives the same answer as the queue-wide one.
    CHECK(compatBlock(queue.operations()[0], guards, {}, queue).guards == conflicts[0].block.guards);
    CHECK(compatBlock(queue.operations()[1], guards, {}, queue).empty());
    CHECK(compatBlock(queue.operations()[2], guards, {}, queue).empty());
    CHECK(compatConflicts(queue, {}, {}).empty());
}

TEST_CASE("audit OPS-14: an index entry past the end of the payload is refused before anything is read") {
    const auto dir = opsAuditFolder(L"psf");
    const auto psf = dir / L"payload.psf";
    {
        std::ofstream out(psf, std::ios::binary | std::ios::trunc);
        out << "0123456789";
    }
    const auto folder = dir / L"out";

    uup::PsfEntry inside;
    inside.name = L"a\\inside.txt";
    inside.offset = 2;
    inside.length = 4;
    uup::PsfEntry tooLong = inside;
    tooLong.name = L"too-long.bin";
    tooLong.offset = 8;
    tooLong.length = 0xFFFFFFF0u; // what a damaged index can say: each worker would allocate it
    uup::PsfEntry beyond = inside;
    beyond.name = L"beyond.bin";
    beyond.offset = 11;
    beyond.length = 0;

    for (const auto& bad : {tooLong, beyond}) {
        const auto refused = uup::expandPsf(psf, {inside, bad}, folder, TaskContext{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::ParseError);
        CHECK(refused.error().context == bad.name);
    }
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists(folder / L"a" / L"inside.txt", ec)); // refused as a whole

    // The entries that fit are written as before (RAW: the bytes themselves), up to the last byte.
    uup::PsfEntry tail = inside;
    tail.name = L"tail.txt";
    tail.offset = 6;
    tail.length = 4;
    const auto written = uup::expandPsf(psf, {inside, tail}, folder, TaskContext{});
    REQUIRE(written.has_value());
    std::ifstream in(folder / L"a" / L"inside.txt", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "2345");
    std::ifstream inTail(folder / L"tail.txt", std::ios::binary);
    const std::string textTail((std::istreambuf_iterator<char>(inTail)), std::istreambuf_iterator<char>());
    CHECK(textTail == "6789");
    std::filesystem::remove_all(dir, ec);
}
