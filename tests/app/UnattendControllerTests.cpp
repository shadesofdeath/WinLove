// P13: answer file options in the app state — editing, save / import, what goes into the ISO.
#include "app/controllers/IsoController.h"
#include "app/controllers/UnattendController.h"
#include "core/image/UdfImage.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <mutex>

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

TEST_CASE("unattend controller: the first answer puts the file into the ISO; the box opts out") {
    Fixture f;
    CHECK_FALSE(f.controller.includeInIso());
    CHECK(UnattendController::isoFile(f.state).empty()); // nothing answered: no file

    // A real case: every answer given, the box never noticed, the ISO built without the file.
    f.controller.edit([](core::UnattendOptions& o) { o.bypassTpm = true; });
    CHECK(f.controller.includeInIso());
    CHECK(UnattendController::isoFile(f.state).find("BypassTPMCheck") != std::string::npos);

    // Unchecked by hand: later edits leave it alone.
    f.controller.setIncludeInIso(false);
    f.controller.edit([](core::UnattendOptions& o) { o.bypassRam = true; });
    CHECK_FALSE(f.controller.includeInIso());
    CHECK(UnattendController::isoFile(f.state).empty());

    // An imported file is one to use.
    const auto file = scratch(L"imported.xml");
    REQUIRE(f.controller.save(file));
    REQUIRE(f.controller.import(file));
    CHECK(f.controller.includeInIso());
}

TEST_CASE("unattend → ISO: answers given on the page are autounattend.xml in the ISO that gets built") {
    const auto dir = scratch(L"iso-flow");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto media = dir / L"media";
    std::filesystem::create_directories(media / L"boot");
    std::filesystem::create_directories(media / L"sources");
    {
        std::ofstream boot(media / L"boot" / L"etfsboot.com", std::ios::binary); // stands in for the boot sector
        boot << std::string(4096, '\0');
        std::ofstream marker(media / L"sources" / L"marker.txt", std::ios::binary);
        marker << "setup files";
    }

    Fixture f;
    core::SourceInfo source = armSource();
    source.path = media;
    source.install.images.front().architecture = core::Architecture::X64;
    f.state.setSource(std::move(source));
    // Only the form is filled in; "ISO'ya ekle" is never touched.
    f.controller.edit([](core::UnattendOptions& o) {
        o.bypassTpm = true;
        o.accountName = L"berkay";
    });

    std::mutex mutex;
    std::vector<std::function<void()>> posted;
    std::vector<Error> failures;
    IsoController iso{f.state, IsoController::Events{[&](std::function<void()> fn) {
                                                        std::scoped_lock lock(mutex);
                                                        posted.push_back(std::move(fn));
                                                    },
                                                    [&](const Error& error) { failures.push_back(error); },
                                                    {}}};
    REQUIRE_FALSE(iso.blocker());
    IsoController::Request request;
    request.output = dir / L"out.iso";
    request.label = L"WL_TEST";
    request.boot = core::BootMode::BiosOnly;
    request.sha256 = false;
    request.openFolder = false;
    iso.start(request);
    f.state.engine().drain();
    {
        std::scoped_lock lock(mutex);
        for (auto& fn : posted) {
            fn();
        }
    }
    REQUIRE(failures.empty());
    REQUIRE(f.state.isoRun().has_value());
    REQUIRE(f.state.isoRun()->result.has_value());

    {
        auto image = core::UdfImage::open(request.output);
        REQUIRE(image.has_value());
        auto node = image->find(L"autounattend.xml");
        REQUIRE(node.has_value());
        const auto copy = dir / L"autounattend-from-iso.xml";
        REQUIRE(image->extract(*node, copy, core::TaskContext{}).has_value());
        const std::string text = readFile(copy);
        CHECK(text.find("BypassTPMCheck") != std::string::npos);
        CHECK(text.find("<Name>berkay</Name>") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(media / L"autounattend.xml")); // the setup folder is not touched
    } // the ISO is closed before it is deleted
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("unattend controller: the ISO never gets an invalid file") {
    Fixture f;
    f.state.setSource(armSource());
    f.controller.edit([](core::UnattendOptions& o) { o.computerName = L"LAB-01"; });
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

TEST_CASE("unattend controller: the file is built for the edition the image will install") {
    Fixture f;
    core::SourceInfo one = armSource(); // a single edition
    one.install.images.front().editionId = L"CoreSingleLanguage";
    f.state.setSource(one);
    f.controller.edit([](core::UnattendOptions& o) { o.acceptEula = true; });
    CHECK(f.controller.options().editionId == L"CoreSingleLanguage");
    CHECK(f.controller.xml().find(L"<Key>BT79Q-G7N6G-PGBYW-4YWX6-6F4BT</Key>") != std::wstring::npos);
    CHECK(f.state.unattend().options.editionId.empty()); // derived each time, not stored

    // Two editions and none picked: nobody knows which → Setup asks.
    core::SourceInfo two = one;
    core::ImageInfo pro = two.install.images.front();
    pro.index = 2;
    pro.editionId = L"Professional";
    two.install.images.push_back(pro);
    f.state.setSource(two);
    CHECK(f.controller.options().editionId.empty());
    CHECK(f.controller.xml().find(L"<Key>00000-00000-00000-00000-00000</Key>") != std::wstring::npos);

    // Picked on the form: that edition's key.
    f.controller.edit([](core::UnattendOptions& o) { o.imageIndex = 2; });
    CHECK(f.controller.xml().find(L"<Key>VK7JG-NPHTM-C97JM-9MPGT-3V66T</Key>") != std::wstring::npos);
}
