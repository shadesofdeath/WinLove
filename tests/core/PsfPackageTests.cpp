// An LCU's express.psf.cix.xml and its .psf made into package files (RAW and PA30 deltas against
// nothing, msdelta.dll), as for a hotpatch-capable MSU the agent refuses offline.
#include "core/uup/PsfPackage.h"

#include <doctest.h>

#include <windows.h>

#include <msdelta.h>

#pragma comment(lib, "msdelta.lib") // CreateDeltaB: the test makes its own PA30 delta

#include <filesystem>
#include <fstream>
#include <string>

using namespace wl::core::uup;

namespace {

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("PSF index: the files, where their bytes are and how they are stored") {
    const std::string xml = R"(<?xml version="1.0"?>
<Container name="Windows11.0-KB5124008-arm64" type="PSF" version="2">
  <Files>
    <File id="1" name="\update.cat" time="134000000000000000" attr="32">
      <Hash alg="SHA256" value="6d6bfadbcf3fccf2205bbaff863c3c368497c47682feb1ef91d53147272537ed"/>
      <Delta><Source type="RAW" offset="0" length="5"><Hash alg="SHA256" value="00"/></Source></Delta>
    </File>
    <File id="2" name="arm64_microsoft-windows-ntdll_31bf3856ad364e35_10.0.26100.9445_none_1\f\ntdll.dll" time="0">
      <Delta><Source type="PA30" offset="5" length="77"/></Delta>
    </File>
    <File id="3" name="x\r\a.dll"><Delta><Source type="pa31" offset="82" length="9"/></Delta></File>
  </Files>
</Container>)";
    auto entries = parsePsfIndex(xml);
    REQUIRE(entries);
    REQUIRE(entries->size() == 3);
    CHECK((*entries)[0].name == L"update.cat"); // the leading separator goes
    CHECK((*entries)[0].kind == PsfEntry::Kind::Raw);
    CHECK((*entries)[0].length == 5);
    CHECK((*entries)[0].time == 134000000000000000ull);
    CHECK((*entries)[0].sha256 == "6D6BFADBCF3FCCF2205BBAFF863C3C368497C47682FEB1EF91D53147272537ED"); // the file's, not its source's
    CHECK((*entries)[1].sha256.empty());
    CHECK((*entries)[1].name == L"arm64_microsoft-windows-ntdll_31bf3856ad364e35_10.0.26100.9445_none_1\\f\\ntdll.dll");
    CHECK((*entries)[1].kind == PsfEntry::Kind::Pa30);
    CHECK((*entries)[1].offset == 5);
    CHECK((*entries)[2].kind == PsfEntry::Kind::Pa31); // any case

    CHECK_FALSE(parsePsfIndex("<Container><Files><File name=\"..\\..\\evil.dll\"><Delta><Source type=\"RAW\" "
                              "offset=\"0\" length=\"1\"/></Delta></File></Files></Container>"));
    CHECK_FALSE(parsePsfIndex("<Container><Files><File name=\"C:\\x.dll\"><Delta><Source/></Delta></File></Files></Container>"));
    CHECK_FALSE(parsePsfIndex("<Package/>"));
    CHECK_FALSE(parsePsfIndex("not xml"));

    CHECK(psfKind("", "PA30\x01") == PsfEntry::Kind::Pa30); // no type: the delta's signature
    CHECK(psfKind("XYZ", "MZ\x90") == PsfEntry::Kind::Raw);
    CHECK(psfKind("PA19", "") == PsfEntry::Kind::Pa19);
}

TEST_CASE("MSU: a hotpatch inside; the checkpoints beside it, oldest first") {
    CHECK(hotpatchInside({L"SSU-26100.9441-x64.cab", L"Windows11.0-KB5124008-x64.wim", L"Windows11.0-KB5129241-Hotpatch-x64.cab"}));
    CHECK_FALSE(hotpatchInside({L"SSU-26100.1738-x64.cab", L"Windows11.0-KB5043080-x64.wim", L"Windows11.0-KB5043080-x64.psf"}));
    CHECK_FALSE(hotpatchInside({}));

    const auto dir = std::filesystem::temp_directory_path() / L"wl-msu-beside";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    for (const wchar_t* name : {L"Windows11.0-KB5122055-x64.msu", L"Windows11.0-KB5043080-x64.msu", L"Windows11.0-KB5129241-x64.msu",
                                L"Windows11.0-KB5068181-x64.msu", L"Windows11.0-KB5043080-x64.cab", L"notes.txt"}) {
        std::ofstream(dir / name).put('x');
    }
    const auto found = checkpointsBeside(dir / L"Windows11.0-KB5129241-x64.msu");
    REQUIRE(found.size() == 3);
    CHECK(found[0].filename() == L"Windows11.0-KB5043080-x64.msu");
    CHECK(found[1].filename() == L"Windows11.0-KB5068181-x64.msu");
    CHECK(found[2].filename() == L"Windows11.0-KB5122055-x64.msu");
    // A checkpoint given itself is not its own prerequisite.
    CHECK(checkpointsBeside(dir / L"Windows11.0-KB5043080-x64.msu").size() == 2);
    CHECK(msuNames(dir / L"Windows11.0-KB5129241-x64.msu").empty()); // not a WIM: an older, cabinet .msu
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("PSF expand: RAW bytes copied, a PA30 delta against nothing applied, the time kept") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-psf-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);

    // A PA30 delta from nothing to `wanted`, the way the .psf stores a file.
    std::string wanted;
    for (int i = 0; i < 4000; ++i) {
        wanted += "WinLove " + std::to_string(i % 97) + "\n";
    }
    DELTA_INPUT none{};
    DELTA_INPUT target{};
    target.lpcStart = wanted.data();
    target.uSize = wanted.size();
    DELTA_OUTPUT delta{};
    REQUIRE(CreateDeltaB(DELTA_FILE_TYPE_RAW, DELTA_FLAG_NONE, DELTA_FLAG_NONE, none, target, {}, {}, {}, nullptr, 0, &delta));
    const std::string deltaBytes(static_cast<const char*>(delta.lpStart), delta.uSize);
    DeltaFree(delta.lpStart);
    CHECK(deltaBytes.starts_with("PA30"));

    const std::string raw = "<assembly/>";
    {
        std::ofstream psf(dir / L"KB1.psf", std::ios::binary);
        psf << "junk" << raw << deltaBytes;
    }
    std::vector<PsfEntry> entries(2);
    entries[0].name = L"update.mum";
    entries[0].offset = 4;
    entries[0].length = static_cast<std::uint32_t>(raw.size());
    entries[0].time = 133900000000000000ull;
    entries[0].sha256 = "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"; // not "<assembly/>": refused
    CHECK_FALSE(expandPsf(dir / L"KB1.psf", entries, dir / L"wrong-hash", {}));
    entries[0].sha256.clear();
    entries[1].name = L"amd64_x_31bf3856ad364e35_10.0.26100.1_none_0\\f\\big.txt";
    entries[1].offset = 4 + raw.size();
    entries[1].length = static_cast<std::uint32_t>(deltaBytes.size());
    entries[1].kind = PsfEntry::Kind::Pa30;

    const auto out = dir / L"expanded";
    REQUIRE(expandPsf(dir / L"KB1.psf", entries, out, {}));
    CHECK(readAll(out / L"update.mum") == raw);
    CHECK(readAll(out / L"amd64_x_31bf3856ad364e35_10.0.26100.1_none_0" / L"f" / L"big.txt") == wanted);
    const auto written = std::filesystem::last_write_time(out / L"update.mum").time_since_epoch().count();
    CHECK(static_cast<std::uint64_t>(written) == 133900000000000000ull); // file_time_type counts FILETIME ticks

    // Bytes the payload does not have: an error, not a short file.
    entries[1].offset = 1u << 30;
    CHECK_FALSE(expandPsf(dir / L"KB1.psf", entries, dir / L"bad", {}));
    std::filesystem::remove_all(dir, ec);
}
