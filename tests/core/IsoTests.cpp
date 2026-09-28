// Real-ISO tests (read-only, no admin). Expected values: tests/integration/fixtures/win11_25h2_tr.json
// (edition names cross-checked against wimgapi.dll). Skipped when the ISO is not on this machine.
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"

#include <doctest.h>
#include <json.hpp>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::core;

namespace {

nlohmann::json fixture() {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"tests/integration/fixtures/win11_25h2_tr.json");
    return nlohmann::json::parse(file);
}

std::filesystem::path isoPath() {
    return utf8::toWide(fixture()["iso"].get<std::string>());
}

bool haveIso() {
    std::error_code ec;
    return std::filesystem::exists(isoPath(), ec);
}

} // namespace

TEST_CASE("ISO: editions and metadata read in place" * doctest::skip(!haveIso())) {
    const auto fx = fixture();
    auto source = openSource(isoPath());
    REQUIRE_MESSAGE(source.has_value(), describe(source.error()).c_str());
    CHECK(source->format == ImageFormat::Iso);
    CHECK(utf8::fromWide(source->volumeLabel) == fx["volumeLabel"].get<std::string>());
    CHECK(utf8::fromWide(source->installImage) == fx["installImage"].get<std::string>());
    CHECK(source->installImageSize == fx["installImageSize"].get<std::uint64_t>());
    CHECK(utf8::fromWide(compressionName(source->install.header.compression)) == fx["compression"].get<std::string>());
    const auto& editions = fx["editions"];
    REQUIRE(source->install.images.size() == editions.size());
    for (std::size_t i = 0; i < editions.size(); ++i) {
        const auto& image = source->install.images[i];
        CAPTURE(i);
        CHECK(utf8::fromWide(image.name) == editions[i].get<std::string>());
        CHECK(utf8::fromWide(image.versionString()) == fx["version"].get<std::string>());
        CHECK(utf8::fromWide(image.defaultLanguage) == fx["language"].get<std::string>());
        CHECK(image.architecture == Architecture::X64);
    }
    REQUIRE(source->boot.has_value());
    CHECK(source->boot->images.size() == fx["bootImages"].get<std::size_t>());
}

TEST_CASE("ISO: directory listing, case-insensitive lookup, small file extraction" * doctest::skip(!haveIso())) {
    auto iso = UdfImage::open(isoPath());
    REQUIRE(iso.has_value());
    auto sources = iso->list(L"sources");
    REQUIRE(sources.has_value());
    CHECK(sources->size() > 100);
    CHECK(iso->find(L"SOURCES\\INSTALL.WIM").has_value());
    CHECK(iso->find(L"sources/does-not-exist.bin").error().code == ErrorCode::NotFound);

    auto node = iso->find(L"sources/cversion.ini");
    REQUIRE(node.has_value());
    const auto target = std::filesystem::temp_directory_path() / L"winlove-test-cversion.ini";
    REQUIRE(iso->extract(*node, target, TaskContext{}).has_value());
    std::ifstream file(target);
    std::string first;
    std::getline(file, first);
    file.close();
    std::filesystem::remove(target);
    CHECK(first.rfind("[HostBuild]", 0) == 0);
}

TEST_CASE("ISO: extraction honours cancellation and leaves no partial file" * doctest::skip(!haveIso())) {
    auto iso = UdfImage::open(isoPath());
    REQUIRE(iso.has_value());
    auto node = iso->find(L"sources/install.wim");
    REQUIRE(node.has_value());
    const auto target = std::filesystem::temp_directory_path() / L"winlove-test-cancel.wim";
    TaskContext task;
    task.cancel.cancel();
    auto result = iso->extract(*node, target, task);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::Cancelled);
    CHECK_FALSE(std::filesystem::exists(target));
    CHECK_FALSE(std::filesystem::exists(target.wstring() + L".partial"));
}
