// Real-ISO tests (read-only, no admin). Expected values: tests/integration/fixtures/win11_25h2_tr.json
// (edition names cross-checked against wimgapi.dll). Skipped when the ISO is not on this machine.
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/image/WorkCopy.h"
#include "core/iso/IsoBuilder.h"
#include "core/iso/UnsupportedUpgrade.h"
#include "core/iso/UnsupportedUpgrade.h"

#include <doctest.h>
#include <json.hpp>

#include <filesystem>
#include <algorithm>
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

TEST_CASE("ISO build: root files from memory are in the image; the source folder is not touched") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"iso-build";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto media = dir / L"media";
    std::filesystem::create_directories(media / L"boot");
    std::filesystem::create_directories(media / L"sources");
    auto write = [](const std::filesystem::path& file, const std::string& content) {
        std::ofstream out(file, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    };
    auto read = [](const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    write(media / L"boot" / L"etfsboot.com", std::string(4096, '\0')); // stands in for the BIOS boot sector
    write(media / L"sources" / L"marker.txt", "setup files");
    write(media / L"autounattend.xml", "the folder's own answer file");

    IsoOptions options;
    options.sourceFolder = media;
    options.output = dir / L"out.iso";
    options.volumeLabel = L"WL_TEST";
    options.boot = BootMode::BiosOnly;
    const std::string answer = "<unattend>from memory</unattend>";
    options.rootFiles = {{L"autounattend.xml", answer}, {L"extra.txt", "second"}};
    const auto built = buildIso(options, TaskContext{});
    REQUIRE_MESSAGE(built.has_value(), utf8::fromWide(describe(built.error())).c_str());
    CHECK(built->bytes > 0);

    auto iso = UdfImage::open(options.output);
    REQUIRE_MESSAGE(iso.has_value(), utf8::fromWide(describe(iso.error())).c_str());
    CHECK(iso->find(L"sources/marker.txt").has_value());
    auto extracted = [&](const wchar_t* name) {
        auto node = iso->find(name);
        REQUIRE(node.has_value());
        const auto target = dir / (std::wstring(L"out-") + name);
        REQUIRE(iso->extract(*node, target, TaskContext{}).has_value());
        return read(target);
    };
    CHECK(extracted(L"autounattend.xml") == answer);
    CHECK(extracted(L"extra.txt") == "second");
    CHECK(read(media / L"autounattend.xml") == "the folder's own answer file");
    CHECK_FALSE(std::filesystem::exists(media / L"extra.txt"));
    iso = std::unexpected(Error{}); // close the ISO before deleting it
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("work copy: an ISO opened again never overwrites what changed since, nor mixes two ISOs (audit A5)") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"work-copy";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    auto write = [](const std::filesystem::path& file, const std::string& content) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    };
    auto read = [](const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    auto build = [&](const std::filesystem::path& output, const std::string& wim) {
        const auto media = dir / L"media";
        std::filesystem::remove_all(media);
        write(media / L"boot" / L"etfsboot.com", std::string(4096, '\0'));
        write(media / L"sources" / L"install.wim", wim);
        write(media / L"sources" / L"lang.ini", "[Available UI Languages]");
        IsoOptions options;
        options.sourceFolder = media;
        options.output = output;
        options.volumeLabel = L"WL_TEST";
        options.boot = BootMode::BiosOnly;
        REQUIRE(buildIso(options, TaskContext{}).has_value());
    };
    const auto iso = dir / L"Win11.iso";
    const auto work = dir / L"work" / L"Win11";
    build(iso, "original image");

    CHECK(inspectWorkCopy(iso, work) == WorkCopyState::Missing);
    REQUIRE(extractWorkCopy(iso, work, /*fresh=*/false, TaskContext{}).has_value());
    CHECK(std::filesystem::exists(workCopyRecord(work)));
    CHECK_FALSE(std::filesystem::exists(work / L"Win11.source.json")); // next to the folder, not in the ISO's files
    CHECK(inspectWorkCopy(iso, work) == WorkCopyState::Pristine);

    // The image was mounted and saved: the copy is the user's work now.
    write(work / L"sources" / L"install.wim", "customised image, longer than before");
    write(work / L"sources" / L"lang.ini", "[Available UI Languages] en-US");
    CHECK(inspectWorkCopy(iso, work) == WorkCopyState::Modified);
    REQUIRE(extractWorkCopy(iso, work, /*fresh=*/false, TaskContext{}).has_value());
    CHECK(read(work / L"sources" / L"install.wim") == "customised image, longer than before"); // kept
    CHECK(read(work / L"sources" / L"lang.ini") == "[Available UI Languages] en-US");
    REQUIRE(extractWorkCopy(iso, work, /*fresh=*/true, TaskContext{}).has_value()); // "Baştan çıkar"
    CHECK(read(work / L"sources" / L"install.wim") == "original image");
    CHECK(inspectWorkCopy(iso, work) == WorkCopyState::Pristine);

    // Another ISO under the same file name: its copy is not this one's.
    const auto other = dir / L"other" / L"Win11.iso";
    std::filesystem::create_directories(other.parent_path());
    build(other, "a different edition set");
    CHECK(inspectWorkCopy(other, work) == WorkCopyState::OtherSource);

    // A folder from before the record existed.
    std::filesystem::remove(workCopyRecord(work));
    CHECK(inspectWorkCopy(iso, work) == WorkCopyState::Unknown);
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("in-place upgrade script: writes the host values Setup checks, then launches setup.exe (D-097)") {
    const std::string cmd = wl::core::unsupportedUpgradeCmd();
    CHECK(cmd.find("AllowUpgradesWithUnsupportedTPMOrCPU") != std::string::npos);
    CHECK(cmd.find("HwReqChkVars") != std::string::npos);
    CHECK(cmd.find("SQ_TpmVersion=2") != std::string::npos);
    CHECK(cmd.find("Start-Process -Verb RunAs") != std::string::npos); // self-elevates for HKLM
    CHECK(cmd.find("%~dp0setup.exe") != std::string::npos);
    // ASCII only (a .cmd read in the OEM codepage).
    CHECK(std::ranges::all_of(cmd, [](char c) { return static_cast<unsigned char>(c) < 128; }));
}
