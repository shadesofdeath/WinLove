// P13: answer file options in the app state — editing, save / import, what goes into the ISO.
#include "app/controllers/IsoController.h"
#include "app/controllers/UnattendController.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"unattend";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

core::SourceInfo armSource() {
    core::SourceInfo source;
    source.path = L"C:\\lab\\media";
    source.format = core::ImageFormat::Folder;
    core::ImageInfo image;
    image.index = 1;
    image.name = L"Windows 11 Pro";
    image.architecture = core::Architecture::Arm64;
    image.languages = {L"tr-TR", L"xx-YY"};
    source.install.images.push_back(image);
    return source;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    UnattendController controller{state};
    int notified = 0;
    Fixture() {
        state.subscribe([this](AppState::Change change) { notified += change == AppState::Change::Unattend ? 1 : 0; });
    }
};

} // namespace

TEST_CASE("unattend controller: edits notify once, no-op edits do not; the source sets the architecture") {
    Fixture f;
    f.controller.edit([](core::UnattendOptions& o) { o.accountName = L"admin"; });
    CHECK(f.notified == 1);
    f.controller.edit([](core::UnattendOptions& o) { o.accountName = L"admin"; });
    CHECK(f.notified == 1);
    CHECK(f.controller.options().architecture == core::Architecture::X64);
    CHECK(f.controller.xml().find(L"processorArchitecture=\"amd64\"") != std::wstring::npos);

    f.state.setSource(armSource());
    CHECK(f.controller.options().architecture == core::Architecture::Arm64);
    CHECK(f.controller.xml().find(L"processorArchitecture=\"arm64\"") != std::wstring::npos);
    // Languages and editions offered by the form come from the source.
    const auto languages = f.controller.languages();
    REQUIRE(languages.size() == 2);
    CHECK(languages[0].value == L"tr-TR");
    CHECK(languages[0].label != L"tr-TR"); // a known locale gets its name
    CHECK(languages[1].label == L"xx-YY");
    REQUIRE(f.controller.editions().size() == 1);
    CHECK(f.controller.editions()[0].value == L"1");
}

TEST_CASE("unattend controller: save writes UTF-8 with CRLF; import reads it back") {
    Fixture f;
    f.controller.edit([](core::UnattendOptions& o) {
        o.accountName = L"\u00d6mer";
        o.password = L"gizli";
        o.bypassTpm = true;
        o.timeZone = L"Turkey Standard Time";
    });
    const auto file = scratch(L"autounattend.xml");
    REQUIRE(f.controller.save(file));
    const std::string bytes = readFile(file);
    CHECK(bytes.starts_with("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"));
    CHECK(bytes.find("\xC3\x96mer") != std::string::npos); // Ö as UTF-8
    CHECK(bytes.find("gizli") == std::string::npos);
    CHECK(bytes.find("\r\n") != std::string::npos);
    CHECK(bytes.find("\r\r\n") == std::string::npos);

    const core::UnattendOptions saved = f.controller.options();
    f.controller.edit([](core::UnattendOptions& o) { o = {}; });
    CHECK(f.controller.options() == core::UnattendOptions{});
    REQUIRE(f.controller.import(file));
    CHECK(f.controller.options() == saved);

    const auto bad = scratch(L"not-an-answer-file.xml");
    {
        std::ofstream out(bad, std::ios::binary);
        out << "<html></html>";
    }
    const auto failed = f.controller.import(bad);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().context.starts_with(L"not-an-answer-file.xml"));
    CHECK(f.controller.options() == saved); // a failed import changes nothing
    CHECK_FALSE(f.controller.import(scratch(L"missing.xml")));
}

TEST_CASE("unattend controller: the ISO gets the file only when asked, and never an invalid one") {
    Fixture f;
    f.state.setSource(armSource());
    f.controller.edit([](core::UnattendOptions& o) { o.computerName = L"LAB-01"; });
    CHECK(UnattendController::isoFile(f.state).empty());

    f.controller.setIncludeInIso(true);
    const std::string bytes = UnattendController::isoFile(f.state);
    CHECK(bytes.find("<ComputerName>LAB-01</ComputerName>") != std::string::npos);
    CHECK(bytes.find("processorArchitecture=\"arm64\"") != std::string::npos);

    IsoController iso{f.state, IsoController::Events{[](std::function<void()> fn) { fn(); }, {}, {}}};
    CHECK_FALSE(iso.blocker());
    f.controller.edit([](core::UnattendOptions& o) { o.computerName = L"THIS NAME IS NOT VALID"; });
    REQUIRE(iso.blocker());
    CHECK(*iso.blocker() == IsoController::Blocker::UnattendInvalid);
    f.controller.setIncludeInIso(false); // not going into the ISO: not the ISO's problem
    CHECK_FALSE(iso.blocker());
}
