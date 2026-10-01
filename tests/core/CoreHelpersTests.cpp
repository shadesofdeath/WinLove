// Shared engine helpers that replaced several local copies (code review 2026-10-02): file sizes,
// swapping a new file into place, encodings, service names.
#include "base/Encoding.h"
#include "core/image/Services.h"
#include "core/system/Files.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"core-helpers" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("treeBytes: a file, a folder, and nothing that is not there") {
    const auto dir = scratch(L"tree");
    write(dir / L"a.bin", "12345");
    std::filesystem::create_directories(dir / L"sub");
    write(dir / L"sub" / L"b.bin", "123");
    CHECK(treeBytes(dir / L"a.bin") == 5);
    CHECK(treeBytes(dir) == 8);
    CHECK(treeBytes(dir / L"missing") == 0); // not uintmax_t(-1)
}

TEST_CASE("swapIntoPlace: the original goes only once the new file is in place") {
    const auto dir = scratch(L"swap");
    write(dir / L"install.wim", "old");
    write(dir / L"install.esd.new", "new");
    REQUIRE(swapIntoPlace(dir / L"install.wim", dir / L"install.esd.new", dir / L"install.esd"));
    CHECK(read(dir / L"install.esd") == "new");
    CHECK_FALSE(std::filesystem::exists(dir / L"install.wim"));
    CHECK_FALSE(std::filesystem::exists(dir / L"install.wim.old"));

    // The new file cannot be moved (it is not there): the original comes back.
    write(dir / L"install.wim", "old");
    CHECK_FALSE(swapIntoPlace(dir / L"install.wim", dir / L"nothing.new", dir / L"install.wim"));
    CHECK(read(dir / L"install.wim") == "old");
}

TEST_CASE("encodings: base64, hex, XML text") {
    const auto bytes = base64Decode("AAEC/w==");
    CHECK(bytes == std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0xFF});
    CHECK(base64Decode("AA!=").empty()); // not the alphabet
    CHECK(hexLower(bytes) == L"000102ff");
    CHECK(xmlEscape(L"a&b<c>\"d'") == L"a&amp;b&lt;c&gt;&quot;d&apos;");
    CHECK(xmlEscape(L"it's", /*apostrophe=*/false) == L"it's");
}

TEST_CASE("service names from a preset stay one key under Services") {
    CHECK(validServiceName(L"DiagTrack"));
    CHECK_FALSE(validServiceName(L""));
    CHECK_FALSE(validServiceName(L".."));
    CHECK_FALSE(validServiceName(L"Svc\\Parameters"));
    CHECK_FALSE(validServiceName(L"Svc/Parameters"));
}
