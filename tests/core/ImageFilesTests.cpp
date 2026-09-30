// WriteFile operations (core/image/ImageFiles): where a file may go, and that it gets there.
#include "core/image/ImageFiles.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path image(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-files" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / L"Users" / L"Default" / L"AppData" / L"Local");
    std::filesystem::create_directories(dir / L"Windows" / L"System32");
    return dir;
}

std::string bytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

constexpr const wchar_t* kLayout = L"Users\\Default\\AppData\\Local\\Microsoft\\Windows\\Shell\\LayoutModification.xml";

} // namespace

TEST_CASE("image files: only configuration places, never out of them") {
    CHECK(validateImageFile(kLayout, 100));
    CHECK(validateImageFile(L"ProgramData\\WinLove\\note.txt", 1));
    CHECK(validateImageFile(L"users/default/AppData/x.json", 1)); // case and separators do not matter

    CHECK_FALSE(validateImageFile(L"Windows\\System32\\evil.dll", 1));
    CHECK_FALSE(validateImageFile(L"Users\\Public\\Desktop\\x.lnk", 1));
    CHECK_FALSE(validateImageFile(L"Users\\Defaults\\x.txt", 1));          // not the folder, a name that starts like it
    CHECK_FALSE(validateImageFile(L"Users\\Default\\..\\..\\Windows\\x", 1));
    CHECK_FALSE(validateImageFile(L"Users\\Default\\x.txt:stream", 1));
    CHECK_FALSE(validateImageFile(L"C:\\Users\\Default\\x.txt", 1));
    CHECK_FALSE(validateImageFile(L"\\Users\\Default\\x.txt", 1));
    CHECK_FALSE(validateImageFile(L"Users\\Default", 1));                   // the folder itself
    CHECK_FALSE(validateImageFile(L"", 1));
    CHECK_FALSE(validateImageFile(kLayout, kImageFileLimit + 1));
}

TEST_CASE("image files: written as given, folders made, an old file replaced") {
    const auto root = image(L"write");
    const std::string xml = "<LayoutModificationTemplate>\r\n  \xC3\xA7\r\n</LayoutModificationTemplate>\r\n";
    REQUIRE(writeImageFile(root, kLayout, xml));
    const auto file = root / kLayout;
    CHECK(bytesOf(file) == xml); // byte for byte: no BOM, no newline translation

    // A read-only file of the image in its place.
    std::filesystem::permissions(file, std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                           std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);
    REQUIRE(writeImageFile(root, kLayout, "shorter"));
    CHECK(bytesOf(file) == "shorter");

    // Refused paths leave nothing behind.
    CHECK_FALSE(writeImageFile(root, L"Windows\\System32\\evil.dll", "x"));
    CHECK_FALSE(std::filesystem::exists(root / L"Windows" / L"System32" / L"evil.dll"));
    // A folder that is not an image (no ProgramData in it) gets nothing.
    const auto missing = writeImageFile(root, L"ProgramData\\WinLove\\note.txt", "x");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_FALSE(std::filesystem::exists(root / L"ProgramData"));
    // A folder where the file should be is not deleted for it.
    std::filesystem::create_directories(root / L"Users" / L"Default" / L"AppData" / L"Local" / L"taken.txt");
    CHECK_FALSE(writeImageFile(root, L"Users\\Default\\AppData\\Local\\taken.txt", "x"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("image files: a WriteFile operation is a settings step and survives a preset file") {
    ops::ChangeSet changes;
    changes.add(ops::Operation{ops::OpKind::WriteFile, kLayout, L"<a b=\"c\">\r\n</a>"});
    changes.add(ops::Operation{ops::OpKind::RemoveAppx, L"App_1.0_x64__abc"});
    const auto back = ops::ChangeSet::fromJson(changes.toJson());
    REQUIRE(back);
    const auto* file = back->find(ops::OpKind::WriteFile, kLayout);
    REQUIRE(file);
    CHECK(file->value == L"<a b=\"c\">\r\n</a>");

    const auto plan = ops::plan(*back);
    REQUIRE(plan.steps.size() == 2);
    CHECK(plan.steps.back().operation.kind == ops::OpKind::WriteFile); // after the removals
    CHECK(plan.steps.back().phase == ops::Phase::Settings);
}
