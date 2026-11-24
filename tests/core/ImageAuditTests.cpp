// docs/AUDIT-2026-11-10.md, core/image: what its fixes must leave as it was, and what they changed.
#include "base/File.h"
#include "core/image/ComponentStore.h"
#include "core/image/HostsFile.h"
#include "core/image/RegistryEdit.h"

#include <doctest.h>

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path auditDir(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-audit" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeBytes(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Opened with no sharing: nobody else can read the file until the handle is closed.
HANDLE lockFile(const std::filesystem::path& file) {
    return CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

} // namespace

TEST_CASE("audit IMG-14: hosts lines split by hand read as they did through a stream") {
    const auto entries = parseHosts(L"0.0.0.0\ta.example.com   b.example.com # note\r\n"
                                    L"  127.0.0.1 localhost\r\n"
                                    L"not-an-address c.example.com\r\n"
                                    L"\r\n"
                                    L"::1 A.Example.com d.example.com\r\n"
                                    L"#0.0.0.0 e.example.com\r\n"
                                    L"0.0.0.0\r\n"
                                    L"0.0.0.0 f.example.com");
    const std::vector<HostEntry> expected = {{L"0.0.0.0", L"a.example.com"},
                                             {L"0.0.0.0", L"b.example.com"},
                                             {L"::1", L"d.example.com"}, // A.Example.com: a name once, the first wins
                                             {L"0.0.0.0", L"f.example.com"}};
    CHECK(entries == expected);
    CHECK(parseHosts(L"").empty());
    CHECK(parseHosts(L"   \t  \r\n#\r\n").empty());
}

TEST_CASE("audit IMG-13: a hosts file that cannot be read is not written over") {
    const auto mount = auditDir(L"hosts");
    const auto file = hostsPath(mount);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::string own = "127.0.0.1 keep.me\r\n";
    writeBytes(file, own);

    const HANDLE locked = lockFile(file);
    REQUIRE(locked != INVALID_HANDLE_VALUE);
    const auto refused = applyHostsSection(mount, L"test", L"0.0.0.0 a.example.com\n");
    CloseHandle(locked);
    CHECK_FALSE(refused.has_value());
    CHECK(readFileBytes(file).value_or(std::string()) == own);

    // Readable: the section goes in and the user's line stays.
    REQUIRE(applyHostsSection(mount, L"test", L"0.0.0.0 a.example.com\n").has_value());
    const std::string after = readFileBytes(file).value_or(std::string());
    CHECK(after.starts_with(own));
    CHECK(after.find("# >>> WinLove: test\r\n0.0.0.0 a.example.com\r\n# <<< WinLove: test\r\n") != std::string::npos);

    // No file yet: still not an error.
    std::filesystem::remove(file, ec);
    CHECK(applyHostsSection(mount, L"test", L"0.0.0.0 a.example.com\n").has_value());
    CHECK(readFileBytes(file).value_or(std::string()).find("0.0.0.0 a.example.com") != std::string::npos);
    std::filesystem::remove_all(mount, ec);
}

TEST_CASE("audit IMG-13: a SetupComplete.cmd that cannot be read is not written over") {
    const auto dir = auditDir(L"setupcomplete");
    const auto file = dir / L"SetupComplete.cmd";
    const std::string own = "@echo off\r\ncall oem.cmd\r\n";
    writeBytes(file, own);

    const HANDLE locked = lockFile(file);
    REQUIRE(locked != INVALID_HANDLE_VALUE);
    const auto refused = ensureSetupCompleteLine(file, "echo winlove", "test");
    CloseHandle(locked);
    CHECK_FALSE(refused.has_value());
    CHECK(readFileBytes(file).value_or(std::string()) == own);

    REQUIRE(ensureSetupCompleteLine(file, "echo winlove", "test").has_value());
    CHECK(readFileBytes(file).value_or(std::string()) == "@echo off\r\nrem test\r\necho winlove\r\ncall oem.cmd\r\n");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("audit IMG-06: the store is shared out as before: the first matching pattern by specificity, then by order") {
    ComponentStoreIndex index;
    const char* tail = ", Culture=neutral, Version=10.0.1, PublicKeyToken=31bf3856ad364e35, ProcessorArchitecture=";
    const auto add = [&](const std::string& name, const char* arch, std::uint64_t bytes) {
        index.addStoreComponent(name + tail + arch, bytes);
    };
    add("Alpha-One", "amd64", 100);
    add("Alpha-One", "wow64", 1);
    add("Alpha-Two", "amd64", 10);
    add("Beta", "amd64", 5);
    add("Gamma", "amd64", 7);
    add("Delta", "amd64", 3);
    const std::vector<std::vector<std::wstring>> patterns = {
        {L"alpha-*"},                        // 0: a glob with little literal text
        {L"Alpha-One"},                      // 1: an exact name (any case)
        {L"alpha-one|31bf3856ad364e35"},     // 2: exact with the token: longer, so it comes first
        {L"beta", L"beta"},                  // 3: the same exact pattern as 4, listed first
        {L"beta"},                           // 4
        {L"*|31bf3856ad364e35|10.0.1"},      // 5: a glob with more literal text than 0
        {L"", L"delta|31bf3856ad364e35|9*"}, // 6: an empty pattern is no pattern; the other does not match
        {L"delta|31bf3856ad364e35|10.0.1"},  // 7: exact, all three fields
    };
    const auto shares = index.shareStore(patterns);
    REQUIRE(shares.size() == patterns.size());
    CHECK(shares[0].empty());
    CHECK(shares[1].empty());
    CHECK(shares[2].names64 == std::vector<std::wstring>{L"alpha-one"});
    CHECK(shares[2].names32 == std::vector<std::wstring>{L"alpha-one"});
    CHECK(shares[2].bytes64 == 100);
    CHECK(shares[2].bytes32 == 1);
    CHECK(shares[3].names64 == std::vector<std::wstring>{L"beta"});
    CHECK(shares[3].bytes64 == 5);
    CHECK(shares[4].empty());
    CHECK(shares[5].names64 == std::vector<std::wstring>{L"alpha-two", L"gamma"});
    CHECK(shares[5].bytes64 == 17);
    CHECK(shares[6].empty());
    CHECK(shares[7].names64 == std::vector<std::wstring>{L"delta"});
    CHECK(shares[7].bytes64 == 3);

    // The answer of the shortcut is the answer of asking every pattern in order.
    struct Rule {
        int specificity;
        std::size_t order;
        std::size_t owner;
        std::wstring pattern;
    };
    std::vector<Rule> rules;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        for (const auto& pattern : patterns[i]) {
            if (!pattern.empty()) {
                rules.push_back({ComponentStoreIndex::storePatternSpecificity(pattern), rules.size(), i, pattern});
            }
        }
    }
    std::ranges::sort(rules, [](const Rule& a, const Rule& b) {
        return a.specificity != b.specificity ? a.specificity > b.specificity : a.order < b.order;
    });
    const auto ownerOf = [&](const std::wstring& match) -> std::size_t {
        for (const auto& rule : rules) {
            if (ComponentStoreIndex::storePatternMatches(rule.pattern, match)) {
                return rule.owner;
            }
        }
        return patterns.size();
    };
    CHECK(ownerOf(L"alpha-one|31bf3856ad364e35|10.0.1") == 2);
    CHECK(ownerOf(L"alpha-two|31bf3856ad364e35|10.0.1") == 5);
    CHECK(ownerOf(L"beta|31bf3856ad364e35|10.0.1") == 3);
    CHECK(ownerOf(L"gamma|31bf3856ad364e35|10.0.1") == 5);
    CHECK(ownerOf(L"delta|31bf3856ad364e35|10.0.1") == 7);
}
