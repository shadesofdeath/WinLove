// D-095: this PC's programs → winget ids. The names are what this PC's Uninstall keys said on
// 2026-10-09; the normalized forms are winget's (its index's norm_names2 / norm_publishers2).
#include "core/programs/InstalledPrograms.h"

#include <doctest.h>

using namespace wl::core;

TEST_CASE("installed programs: names normalized the way winget's index keeps them") {
    CHECK(normalizeProgramName(L"7-Zip 26.02 (x64)") == L"7zip");
    CHECK(normalizeProgramName(L"VLC media player") == L"vlcmediaplayer");
    CHECK(normalizeProgramName(L"Notepad++ (64-bit x64)") == L"notepad");
    CHECK(normalizeProgramName(L"Mozilla Firefox (x64 tr)") == L"mozillafirefox");
    CHECK(normalizeProgramName(L"Python 3.14.6 (64-bit)") == L"python");
    CHECK(normalizeProgramName(L"foobar2000 v2.25.10 (x64)") == L"foobar2000");
    CHECK(normalizeProgramName(L"Google Chrome") == L"googlechrome");
    CHECK(nameArchitecture(L"7-Zip 26.02 (x64)") == L"X64");
    CHECK(nameArchitecture(L"Microsoft Visual C++ 2013 x86 Additional Runtime") == L"X86");
    CHECK(nameArchitecture(L"VLC media player").empty());
}

TEST_CASE("installed programs: publishers without their legal form") {
    CHECK(normalizePublisher(L"Microsoft Corporation") == L"microsoft");
    CHECK(normalizePublisher(L"Igor Pavlov") == L"igorpavlov");
    CHECK(normalizePublisher(L"Notepad++ Team") == L"notepad");
    CHECK(normalizePublisher(L"Mozilla") == L"mozilla");
    CHECK(normalizePublisher(L"Valve Corporation") == L"valve");
    CHECK(normalizePublisher(L"VideoLAN") == L"videolan");
}

TEST_CASE("installed programs: Windows' own entries are not offered") {
    CHECK(listedProgram(L"7-Zip 26.02 (x64)", 0, L"", L""));
    CHECK_FALSE(listedProgram(L"", 0, L"", L""));
    CHECK_FALSE(listedProgram(L"Microsoft Update Health Tools", 1, L"", L""));     // SystemComponent
    CHECK_FALSE(listedProgram(L"Security Update for Office", 0, L"Office16", L"")); // a parent
    CHECK_FALSE(listedProgram(L"Hotfix for X", 0, L"", L"Hotfix"));
    CHECK_FALSE(listedProgram(L"Update for Y", 0, L"", L"Update"));
}
