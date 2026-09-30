// The repository's PowerShell scripts must be plain ASCII. Windows PowerShell 5.1 reads a file
// without a BOM in the ANSI code page: the UTF-8 bytes of an em dash end in 0x94, which that code
// page maps to a typographic quote — and PowerShell accepts that as a string terminator. The
// script then fails to parse on the user's machine (tools\lab_components.ps1, 2026-09-30).
#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("PowerShell scripts are ASCII (Windows PowerShell reads BOM-less files as ANSI)") {
    const fs::path root = WL_SOURCE_DIR;
    std::vector<fs::path> scripts{root / L"build.ps1"};
    for (const auto& entry : fs::directory_iterator(root / L"tools")) {
        if (entry.path().extension() == L".ps1") {
            scripts.push_back(entry.path());
        }
    }
    REQUIRE(scripts.size() > 1);
    for (const auto& script : scripts) {
        std::ifstream in(script, std::ios::binary);
        REQUIRE(in);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::size_t line = 1;
        for (const char c : bytes) {
            if (c == '\n') {
                ++line;
            }
            if (static_cast<unsigned char>(c) >= 0x80) {
                FAIL_CHECK(script.filename().string() << ": non-ASCII byte on line " << line);
                break;
            }
        }
    }
}
