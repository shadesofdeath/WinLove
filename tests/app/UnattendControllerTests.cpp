// P13: answer file options in the app state — editing, save / import, what goes into the ISO.
#include "app/controllers/IsoController.h"
#include "app/controllers/UnattendController.h"
#include "app/Localization.h"
#include "core/unattend/Welcome.h"
#include "core/image/UdfImage.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

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

TEST_CASE("ISO: the requirement bypasses go into a patched copy of boot.wim, the setup folder keeps its own") {
    const auto dir = scratch(L"iso-boot");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto media = dir / L"media";
    std::filesystem::create_directories(media / L"boot");
    std::filesystem::create_directories(media / L"sources");
    auto write = [](const std::filesystem::path& file, const std::string& text) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    };
    write(media / L"boot" / L"etfsboot.com", std::string(4096, '\0'));
    write(media / L"sources" / L"boot.wim", "the boot image as Microsoft made it");

    Fixture f;
    AppSettings settings = f.state.settings();
    settings.workRoot = dir / L"work-root"; // where the copy is made and mounted
    f.state.setSettings(settings);
    core::SourceInfo source = armSource();
    source.path = media;
    f.state.setSource(std::move(source));

    IsoController iso{f.state, IsoController::Events{[](std::function<void()> fn) { fn(); }, {}, {}}};
    std::vector<std::wstring> asked;       // the LabConfig values of each patch
    bool askedLegacy = false;              // and whether it boots into the previous Setup (D-074)
    std::filesystem::path patchedFile;
    iso.setBootPatcher([&](const std::filesystem::path& bootWim, const std::filesystem::path& mountDir,
                           const core::BootPatch& patch, const core::TaskContext&) -> Result<core::BootPatchReport> {
        asked = patch.labConfigValues();
        askedLegacy = patch.legacySetup;
        patchedFile = bootWim;
        CHECK(std::filesystem::is_directory(mountDir));
        std::ofstream out(bootWim, std::ios::binary | std::ios::app);
        out << " + LabConfig";
        return core::BootPatchReport{2, 0, {}};
    });
    auto build = [&](bool bootBypass, const wchar_t* name, bool legacySetup = false) {
        IsoController::Request request;
        request.legacySetup = legacySetup;
        request.output = dir / name;
        request.label = L"WL_BOOT";
        request.boot = core::BootMode::BiosOnly;
        request.sha256 = false;
        request.openFolder = false;
        request.bootBypass = bootBypass;
        iso.start(request);
        f.state.engine().drain();
        REQUIRE(f.state.isoRun().has_value());
        REQUIRE(f.state.isoRun()->result.has_value());
        auto image = core::UdfImage::open(request.output);
        REQUIRE(image.has_value());
        auto node = image->find(L"sources/boot.wim");
        REQUIRE(node.has_value());
        const auto copy = dir / (std::wstring(name) + L".boot.wim");
        REQUIRE(image->extract(*node, copy, core::TaskContext{}).has_value());
        return readFile(copy);
    };

    // No check switched off in the answers: nothing to write, the patcher is not run.
    CHECK(IsoController::bootPatch(f.state).empty());
    CHECK(build(true, L"none.iso") == "the boot image as Microsoft made it");
    CHECK(asked.empty());

    // The answers do not go into the ISO, the bypass still reaches Setup's image.
    f.controller.edit([](core::UnattendOptions& o) {
        o.bypassTpm = true;
        o.bypassCpu = true;
    });
    f.controller.setIncludeInIso(false);
    CHECK(build(true, L"patched.iso") == "the boot image as Microsoft made it + LabConfig");
    CHECK(asked == std::vector<std::wstring>{L"BypassTPMCheck", L"BypassCPUCheck"});
    CHECK(patchedFile == settings.workRoot / L"boot" / L"boot.wim");
    CHECK_FALSE(std::filesystem::exists(patchedFile));                                       // the copy is gone
    CHECK(readFile(media / L"sources" / L"boot.wim") == "the boot image as Microsoft made it"); // the folder's is not touched

    // The box cleared: the next ISO has the original again, nothing to undo.
    asked.clear();
    CHECK(build(false, L"plain.iso") == "the boot image as Microsoft made it");
    CHECK(asked.empty());

    // The previous Setup alone is a patch of its own, with or without the bypass box.
    CHECK(build(false, L"legacy.iso", true) == "the boot image as Microsoft made it + LabConfig");
    CHECK(asked.empty());
    CHECK(askedLegacy);
    CHECK(build(true, L"both.iso", true) == "the boot image as Microsoft made it + LabConfig");
    CHECK(asked == std::vector<std::wstring>{L"BypassTPMCheck", L"BypassCPUCheck"});
    CHECK(askedLegacy);
    CHECK(readFile(media / L"sources" / L"boot.wim") == "the boot image as Microsoft made it");

    // A patch that fails is a build that fails: an ISO without the bypass that was asked for is not "done".
    iso.setBootPatcher([](const std::filesystem::path&, const std::filesystem::path&, const core::BootPatch&,
                          const core::TaskContext&) -> Result<core::BootPatchReport> {
        return fail(ErrorCode::AccessDenied, L"DISM needs an elevated (administrator) process", L"Dism");
    });
    IsoController::Request request;
    request.output = dir / L"failed.iso";
    request.label = L"WL_BOOT";
    request.boot = core::BootMode::BiosOnly;
    request.sha256 = false;
    request.openFolder = false;
    iso.start(request);
    f.state.engine().drain();
    REQUIRE(f.state.isoRun().has_value());
    CHECK_FALSE(f.state.isoRun()->result.has_value());
    REQUIRE(f.state.isoRun()->error.has_value());
    CHECK(f.state.isoRun()->error->code == ErrorCode::AccessDenied);
    CHECK_FALSE(std::filesystem::exists(settings.workRoot / L"boot" / L"boot.wim"));
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("USB: the stick gets what the ISO would — answer file at the root, patched boot.wim — and its root is the result") {
    const auto dir = scratch(L"usb-pipeline");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto media = dir / L"media";
    std::filesystem::create_directories(media / L"sources");
    std::ofstream(media / L"sources" / L"boot.wim", std::ios::binary) << "boot";

    Fixture f;
    AppSettings settings = f.state.settings();
    settings.workRoot = dir / L"work-root";
    f.state.setSettings(settings);
    core::SourceInfo source = armSource();
    source.path = media;
    f.state.setSource(std::move(source));
    f.controller.edit([](core::UnattendOptions& o) {
        o.bypassTpm = true;
        o.accountName = L"admin";
    });
    f.controller.setIncludeInIso(true);

    std::filesystem::path finishedAt;
    IsoController iso{f.state, IsoController::Events{[](std::function<void()> fn) { fn(); },
                                                     {},
                                                     [&](const core::IsoResult&, const std::filesystem::path& output,
                                                         bool) { finishedAt = output; },
                                                     {}}};
    iso.setBootPatcher([](const std::filesystem::path& bootWim, const std::filesystem::path&, const core::BootPatch&,
                          const core::TaskContext&) -> Result<core::BootPatchReport> {
        std::ofstream(bootWim, std::ios::binary | std::ios::app) << " + LabConfig";
        return core::BootPatchReport{2, 0, {}};
    });
    std::optional<core::UsbOptions> written;
    std::string patchedBoot;
    iso.setUsbWriter([&](const core::UsbOptions& options, const core::TaskContext&) -> Result<core::UsbResult> {
        written = options;
        REQUIRE(options.replacedFiles.size() == 1);
        patchedBoot = readFile(options.replacedFiles.front().file); // read while the job runs, as writeUsb does
        return core::UsbResult{L"E:\\", 1234, 2};
    });

    IsoController::Request request;
    request.label = L"Win Love";
    request.usb = IsoController::Request::UsbTarget{3, L"7|SanDisk|Ultra|42|32000000000", L"SanDisk Ultra",
                                                    core::UsbScheme::GptUefi};
    iso.start(request);
    f.state.engine().drain();

    REQUIRE(written.has_value());
    CHECK(written->disk == 3);
    CHECK(written->identity == L"7|SanDisk|Ultra|42|32000000000");
    CHECK(written->scheme == core::UsbScheme::GptUefi);
    CHECK(written->sourceFolder == media);
    REQUIRE(written->rootFiles.size() == 1);
    CHECK(written->rootFiles.front().name == L"autounattend.xml");
    CHECK(written->replacedFiles.front().path == L"sources\\boot.wim");
    CHECK(patchedBoot == "boot + LabConfig");
    REQUIRE(f.state.isoRun().has_value());
    CHECK(f.state.isoRun()->usb);
    REQUIRE(f.state.isoRun()->result.has_value());
    CHECK(f.state.isoRun()->result->bytes == 1234);
    CHECK(f.state.isoRun()->output == std::filesystem::path(L"E:\\"));
    CHECK(finishedAt == std::filesystem::path(L"E:\\"));
    CHECK(readFile(media / L"sources" / L"boot.wim") == "boot"); // the setup folder is not touched
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

TEST_CASE("unattend controller: the welcome (D-084) — the setup account in the file, the script in the queue, texts in the file's language") {
    auto load = [](const wchar_t* lang) {
        std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings" / lang, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return Localization::fromJson(text.str()).value();
    };
    const Localization tr = load(L"tr.json");
    const Localization en = load(L"en.json");
    AppState state{scratch(L"welcome-recent.json"), scratch(L"welcome-settings.json")};
    UnattendController controller(state);
    controller.stringsOf = [&](Language language) { return language == Language::Turkish ? &tr : &en; };

    // The window's texts: every "welcome." key, without its prefix.
    const auto texts = UnattendController::welcomeTexts(tr);
    CHECK(texts.size() == 45);
    CHECK(std::ranges::find(texts, std::pair<std::string, std::wstring>{"accountHeading", L"Hoş geldin"}) != texts.end());

    CHECK_FALSE(controller.welcome());
    controller.setWelcome(true);
    CHECK(controller.welcome());
    CHECK(controller.options().welcomePassword.size() == 20);
    CHECK(controller.welcomeQueued());
    CHECK(controller.xml().find(L"<Name>WinLoveSetup</Name>") != std::wstring::npos);
    // No UI language in the file: the app's (Turkish by default).
    CHECK(controller.welcomePlan().texts.size() == 45);
    const auto title = [&] {
        for (const auto& [k, v] : controller.welcomePlan().texts) {
            if (k == "accountHeading") {
                return v;
            }
        }
        return std::wstring();
    };
    CHECK(title() == L"Hoş geldin");

    // An English setup: English texts; the plan's choices stay.
    auto plan = controller.welcomePlan();
    plan.computerPage = false;
    controller.setWelcomePlan(plan);
    controller.edit([](core::UnattendOptions& o) { o.uiLanguage = L"en-US"; });
    controller.setWelcomePlan(controller.welcomePlan());
    CHECK(title() == L"Welcome");
    CHECK_FALSE(controller.welcomePlan().computerPage);
    const std::wstring password = controller.options().welcomePassword;
    controller.setWelcome(false);
    CHECK_FALSE(controller.welcomeQueued());
    CHECK(controller.xml().find(L"WinLoveSetup") == std::wstring::npos);
    controller.setWelcome(true);
    CHECK(controller.options().welcomePassword == password); // made once
}
