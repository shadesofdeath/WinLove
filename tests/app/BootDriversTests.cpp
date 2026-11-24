// D-134: Sürücüler › Kurulum ortamı (boot.wim) — what "Bu bilgisayardan al" ticks there, a storage driver
// that has to reach the installed Windows too, and the ISO build: the drivers go into boot.wim whatever the
// bypass box says, a refused one is named and does not cost the ISO.
#include "app/controllers/ImageDriverController.h"
#include "app/controllers/IsoController.h"
#include "app/state/Preset.h"
#include "core/image/UdfImage.h"
#include "support/UiThread.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"boot-drivers-app";
    std::filesystem::create_directories(dir);
    return dir / name;
}

core::DriverInf inf(const wchar_t* path, const wchar_t* cls, const wchar_t* hwid, int ifType = 0,
                    const wchar_t* arch = L"NTamd64", const wchar_t* version = L"1.0.0.0") {
    std::wstring text = std::wstring(L"[Version]\nClass=") + cls + L"\nCatalogFile=x.cat\nDriverVer=01/01/2025," + version +
                        L"\n[Manufacturer]\n%M%=M," + arch + L"\n[M." + arch + L"]\n%D%=Install, " + hwid + L"\n";
    if (ifType != 0) {
        text += L"[Install.NDI]\n*IfType = " + std::to_wstring(ifType) + L"\n";
    }
    return core::parseInfText(text, path);
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    test::UiThread ui;
    ImageDriverController drivers{state, ImageDriverController::Events{ui.post(), {}, {}}};
    std::vector<core::DriverInf> host{
        inf(L"C:\\h\\oem0\\iastorvd.inf", L"SCSIAdapter", L"PCI\\VEN_8086&DEV_A77F"),
        inf(L"C:\\h\\oem603\\rt68cx21x64.inf", L"Net", L"PCI\\VEN_10EC&DEV_8125", 6),
        inf(L"C:\\h\\oem604\\netwtw6e.inf", L"Net", L"PCI\\VEN_8086&DEV_51F1", 71),
        inf(L"C:\\h\\oem92\\netadapter.inf", L"Net", L"*VMnetAdapter1", 6),
        inf(L"C:\\h\\oem613\\iigd_dch.inf", L"Display", L"PCI\\VEN_8086&DEV_A7A0"),
        inf(L"C:\\h\\oem9\\qcnvme.inf", L"SCSIAdapter", L"PCI\\VEN_17CB&DEV_0306", 0, L"NTarm64"),
    };
};

core::WimDriver present(const wchar_t* name, const wchar_t* cls, const wchar_t* version) {
    core::WimDriver d;
    d.publishedName = L"oem0.inf";
    d.originalName = name;
    d.inf = core::parseInfText(std::wstring(L"[Version]\nClass=") + cls + L"\nDriverVer=01/01/2025," + version + L"\n", name);
    return d;
}

} // namespace

TEST_CASE("boot tab, this PC's drivers: storage and wired network of the medium's architecture, not what boot.wim has") {
    Fixture f;
    auto picked = ImageDriverController::setupPick(f.host, core::Architecture::X64, {});
    CHECK(picked == std::vector<std::filesystem::path>{L"C:\\h\\oem0\\iastorvd.inf", L"C:\\h\\oem603\\rt68cx21x64.inf"});
    // An ARM64 medium: only what has ARM64 models.
    CHECK(ImageDriverController::setupPick(f.host, core::Architecture::Arm64, {}) ==
          std::vector<std::filesystem::path>{L"C:\\h\\oem9\\qcnvme.inf"});
    // The medium's boot.wim carries the Realtek driver of that version already.
    picked = ImageDriverController::setupPick(f.host, core::Architecture::X64, {present(L"rt68cx21x64.inf", L"Net", L"1.0.0.0")});
    CHECK(picked == std::vector<std::filesystem::path>{L"C:\\h\\oem0\\iastorvd.inf"});
}

TEST_CASE("boot tab: a storage driver ticked with an image mounted goes into the image's queue too; the rest does not") {
    Fixture f;
    f.state.addDriverScan(L"C:\\h", f.host);
    // No image mounted: boot.wim only — and the ISO page has to say the installed Windows lacks it.
    CHECK_FALSE(f.drivers.toggleBoot(f.host[0]));
    CHECK(f.state.isBootDriver(f.host[0].path));
    CHECK(f.state.changes().empty());
    CHECK(ImageDriverController::storageOnlyInBoot(f.state) == std::vector<std::filesystem::path>{f.host[0].path});
    f.drivers.toggleBoot(f.host[0]); // off again
    CHECK(ImageDriverController::storageOnlyInBoot(f.state).empty());

    f.state.setMounted(MountedImage{L"C:\\WinLove\\mount", L"C:\\WinLove\\work\\sources\\install.wim", 4, L"Windows 11 Pro"});
    CHECK(f.drivers.toggleBoot(f.host[0])); // storage: queued for the image as well
    CHECK(f.state.changes().find(core::ops::OpKind::AddDriver, f.host[0].path.wstring()) != nullptr);
    CHECK_FALSE(f.drivers.toggleBoot(f.host[1])); // wired network: boot.wim only
    CHECK(f.state.changes().find(core::ops::OpKind::AddDriver, f.host[1].path.wstring()) == nullptr);
    CHECK(ImageDriverController::storageOnlyInBoot(f.state).empty());
    // Off boot.wim: the image keeps what it was given (the user takes it off on the other tab).
    f.drivers.toggleBoot(f.host[0]);
    CHECK_FALSE(f.state.isBootDriver(f.host[0].path));
    CHECK(f.state.changes().find(core::ops::OpKind::AddDriver, f.host[0].path.wstring()) != nullptr);

    // Several at once (a class row): one each, the storage ones queued once.
    const int queued = f.drivers.setBoot({f.host[0], f.host[1], f.host[5]}, true);
    CHECK(queued == 1); // qcnvme; iastorvd was queued already
    CHECK(f.state.bootDrivers().size() == 3);
}

TEST_CASE("boot tab: a storage driver the install image has already is not reported as missing from it") {
    Fixture f;
    f.state.addDriverScan(L"C:\\h", f.host);
    f.state.setBootDriver(f.host[0].path, true);
    core::SourceInfo source;
    source.path = L"D:\\iso\\win.iso";
    source.format = core::ImageFormat::Iso;
    f.state.setSource(source);
    REQUIRE(ImageDriverController::storageOnlyInBoot(f.state).size() == 1);
    AppState::MediaDrivers media;
    media.status = AppState::MediaDrivers::Status::Ready;
    media.source = source.path;
    media.drivers.installRead = true;
    media.drivers.install = {present(L"iastorvd.inf", L"SCSIAdapter", L"1.0.0.0")};
    f.state.setMediaDrivers(media);
    CHECK(ImageDriverController::storageOnlyInBoot(f.state).empty());
    // Another version of it is not the same driver.
    media.drivers.install = {present(L"iastorvd.inf", L"SCSIAdapter", L"0.9.0.0")};
    f.state.setMediaDrivers(media);
    CHECK(ImageDriverController::storageOnlyInBoot(f.state).size() == 1);
    // A new source drops what was read for the old one.
    f.state.setSource(source);
    CHECK_FALSE(f.state.mediaDrivers().has_value());
}

TEST_CASE("ISO: boot.wim gets the drivers with the bypass box cleared; a refused one is named, the ISO is still made") {
    const auto dir = scratch(L"iso");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto media = dir / L"media";
    std::filesystem::create_directories(media / L"boot");
    std::filesystem::create_directories(media / L"sources");
    std::ofstream(media / L"boot" / L"etfsboot.com", std::ios::binary) << std::string(4096, '\0');
    std::ofstream(media / L"sources" / L"boot.wim", std::ios::binary) << "boot";

    Fixture f;
    AppSettings settings = f.state.settings();
    settings.workRoot = dir / L"work-root";
    f.state.setSettings(settings);
    core::SourceInfo source;
    source.path = media;
    source.format = core::ImageFormat::Folder;
    f.state.setSource(source);
    f.state.setBootDriver(L"D:\\drv\\pvscsi.inf", true);
    f.state.setBootDriver(L"D:\\drv\\qcwlan_arm.inf", true);

    test::UiThread ui;
    IsoController iso{f.state, IsoController::Events{ui.post(), {}, {}}};
    std::optional<core::BootPatch> asked;
    iso.setBootPatcher([&](const std::filesystem::path& bootWim, const std::filesystem::path&, const core::BootPatch& patch,
                           const core::TaskContext&) -> Result<core::BootPatchReport> {
        asked = patch;
        std::ofstream(bootWim, std::ios::binary | std::ios::app) << " + drivers";
        core::BootPatchReport report;
        report.index = 2;
        report.driversAdded = {L"D:\\drv\\pvscsi.inf"};
        report.driversRefused = {core::DriverRefusal{L"D:\\drv\\qcwlan_arm.inf", core::DriverRefusal::Why::Architecture, L"arm64"}};
        return report;
    });
    // The answers would switch the TPM check off — the box says no: no LabConfig, the drivers still go in.
    AppState::Unattend answers = f.state.unattend();
    answers.options.bypassTpm = true;
    answers.includeInIso = false;
    f.state.setUnattend(answers);
    IsoController::Request request;
    request.output = dir / L"drivers.iso";
    request.label = L"WL";
    request.boot = core::BootMode::BiosOnly;
    request.sha256 = false;
    request.openFolder = false;
    request.bootBypass = false;
    iso.start(request);
    ui.settle(f.state.engine());
    REQUIRE(asked.has_value());
    CHECK(asked->labConfigValues().empty());
    CHECK(asked->drivers == std::vector<std::filesystem::path>{L"D:\\drv\\pvscsi.inf", L"D:\\drv\\qcwlan_arm.inf"});
    const auto& run = f.state.isoRun();
    REQUIRE(run.has_value());
    REQUIRE(run->result.has_value()); // made
    CHECK(run->bootDriversAdded == std::vector<std::filesystem::path>{L"D:\\drv\\pvscsi.inf"});
    REQUIRE(run->bootDriversRefused.size() == 1);
    CHECK(run->bootDriversRefused.front().why == core::DriverRefusal::Why::Architecture);
    {
        auto image = core::UdfImage::open(request.output);
        REQUIRE(image.has_value());
        auto node = image->find(L"sources/boot.wim");
        REQUIRE(node.has_value());
        CHECK(node->size == std::string("boot + drivers").size()); // the patched copy went into the ISO
    }
    // With the bypass box on, both.
    request.bootBypass = true;
    request.output = dir / L"both.iso";
    iso.start(request);
    ui.settle(f.state.engine());
    REQUIRE(asked.has_value());
    CHECK(asked->labConfigValues() == std::vector<std::wstring>{L"BypassTPMCheck"});
    CHECK(asked->drivers.size() == 2);
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("preset: the boot.wim drivers of a preset saved before D-134 load as they were") {
    const std::string old =
        R"({"format":"winlove.changeset","version":1,"name":"Dell","operations":[],"bootDrivers":["D:\\drivers\\vmd\\iaStorVD.inf"]})";
    const auto preset = presetFromJson(old, L"x");
    REQUIRE(preset.has_value());
    CHECK(preset->bootDrivers == std::vector<std::filesystem::path>{L"D:\\drivers\\vmd\\iaStorVD.inf"});
}
