// D-134: drivers for Setup's own image (boot.wim) — what an INF is for Setup (role), what stops it before
// DISM sees it (architecture, catalog), and how the refusals read. The DISM part (added / already there /
// taken as unsigned and taken out again) is proven on a real boot.wim: tools\lab_boot_drivers.ps1.
#include "core/image/BootImage.h"
#include "core/image/DriverInf.h"
#include "base/Utf8.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"boot-drivers" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

// The shape of VMware's pvscsi.inf (Windows 10 build): models for x86 (empty) and x64.
constexpr const char* kPvscsi = R"([Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=%COMPANY%
DriverVer=06/06/2024,1.3.28.0
CatalogFile=pvscsi.cat
[Manufacturer]
%COMPANY%=pvscsi,NTx86,NTamd64.10.0...20124
[pvscsi.NTx86]
[pvscsi.NTamd64.10.0...20124]
%DEVICE%=DDInstall, PCI\VEN_15AD&DEV_07C0
[Strings]
COMPANY="Broadcom Inc."
DEVICE="VMware PVSCSI Controller"
)";

} // namespace

TEST_CASE("INF roles for Setup: storage, wired network, Wi-Fi (both *IfType forms), a host's virtual adapter, the rest") {
    const auto storage = parseInfText(utf8::toWide(kPvscsi));
    CHECK(setupRole(storage) == SetupRole::Storage);
    CHECK(storage.hardwareIds == std::vector<std::wstring>{L"PCI\\VEN_15AD&DEV_07C0"});
    CHECK(storage.catalog == L"pvscsi.cat");

    // Realtek: "*IfType = 6" in an NDI section of its own (NetAdapterCx), a PCI ID.
    const auto wired = parseInfText(L"[Version]\nClass=Net\nCatalogFile=rt.cat\n[Manufacturer]\n%R%=R,NTamd64\n[R.NTamd64]\n"
                                    L"%D%=Install, PCI\\VEN_10EC&DEV_8125&SUBSYS_00000000, PCI\\VEN_10EC&DEV_8125\n"
                                    L"[Install.NDI]\n*IfType = 6 ; IF_TYPE_ETHERNET_CSMACD\n");
    CHECK(wired.ifType == 6);
    CHECK(wired.hardwareIds.size() == 2);
    CHECK(setupRole(wired) == SetupRole::Network);
    CHECK(setupUseful(setupRole(wired)));

    // Intel Wi-Fi: "*IfType = 71"; an older INF writes it through AddReg.
    const auto wifi = parseInfText(L"[Version]\nClass=net\n[Manufacturer]\n%I%=I,NTamd64\n[I.NTamd64]\n%D%=X, PCI\\VEN_8086&DEV_51F1\n"
                                   L"[X.NDI]\n*IfType              = 71   ; IF_TYPE_IEEE80211\n");
    CHECK(wifi.ifType == 71);
    CHECK(setupRole(wifi) == SetupRole::Wireless);
    CHECK_FALSE(setupUseful(setupRole(wifi)));
    const auto wifiAddReg = parseInfText(L"[Version]\nClass=Net\n[Manufacturer]\n%I%=I,NTamd64\n[I.NTamd64]\n%D%=X, USB\\VID_0BDA&PID_8179\n"
                                         L"[X.Reg]\nHKR, , *IfType, 0x00010001, 0x47\n");
    CHECK(wifiAddReg.ifType == 71);
    CHECK(setupRole(wifiAddReg) == SetupRole::Wireless);

    // VMware's host adapters (VMnet1 / VMnet8) are not on a bus: nothing for Windows PE to drive.
    const auto vmnet = parseInfText(L"[Version]\nClass=Net\n[Manufacturer]\n%V%=V,NTx86,NTamd64\n[V.NTamd64]\n%D%=X, *VMnetAdapter1\n"
                                    L"[X.NDI]\n*IfType = 6\n");
    CHECK(setupRole(vmnet) == SetupRole::Virtual);
    // A network INF without models (nothing known) is still a network driver.
    CHECK(setupRole(parseInfText(L"[Version]\nClass=Net\n")) == SetupRole::Network);
    CHECK(setupRole(parseInfText(L"[Version]\nClass=HDC\n")) == SetupRole::Storage);
    CHECK(setupRole(parseInfText(L"[Version]\nClass=Display\n")) == SetupRole::None);
    CHECK(setupRole(parseInfText(L"[Version]\nClass=MEDIA\n")) == SetupRole::None);

    // A catalog given per architecture only.
    CHECK(parseInfText(L"[Version]\nClass=Net\nCatalogFile.NTarm64=q.cat\n").catalog == L"q.cat");
}

TEST_CASE("boot drivers: an INF of another architecture, without its catalog or missing is refused before DISM") {
    const auto dir = scratch(L"check");
    // Signed-looking pvscsi: the INF and its catalog side by side.
    std::filesystem::create_directories(dir / L"ok");
    write(dir / L"ok" / L"pvscsi.inf", kPvscsi);
    write(dir / L"ok" / L"pvscsi.cat", "catalog");
    CHECK_FALSE(checkBootDriver(dir / L"ok" / L"pvscsi.inf", Architecture::X64).has_value());
    CHECK_FALSE(checkBootDriver(dir / L"ok" / L"pvscsi.inf", Architecture::Unknown).has_value());

    // The same for an ARM64 medium: it has no NTarm64 models.
    const auto arm = checkBootDriver(dir / L"ok" / L"pvscsi.inf", Architecture::Arm64);
    REQUIRE(arm.has_value());
    CHECK(arm->why == DriverRefusal::Why::Architecture);
    CHECK(arm->detail == L"x86, x64");
    CHECK(describe(*arm) == L"pvscsi.inf: built for x86, x64 only, not for the setup image's architecture");

    // ARM64-only INF on an x64 medium (DISM itself takes this one as "unsigned", lab 2026-11-10).
    std::filesystem::create_directories(dir / L"arm");
    std::string armText = kPvscsi;
    armText.replace(armText.find("NTx86,NTamd64.10.0...20124"), 26, "NTarm64.10.0...20124");
    write(dir / L"arm" / L"pvscsi.inf", armText);
    write(dir / L"arm" / L"pvscsi.cat", "catalog");
    const auto onX64 = checkBootDriver(dir / L"arm" / L"pvscsi.inf", Architecture::X64);
    REQUIRE(onX64.has_value());
    CHECK(onX64->why == DriverRefusal::Why::Architecture);
    CHECK(onX64->detail == L"arm64");

    // Its catalog not next to it / none named.
    std::filesystem::create_directories(dir / L"nocat");
    write(dir / L"nocat" / L"pvscsi.inf", kPvscsi);
    const auto missingCat = checkBootDriver(dir / L"nocat" / L"pvscsi.inf", Architecture::X64);
    REQUIRE(missingCat.has_value());
    CHECK(missingCat->why == DriverRefusal::Why::CatalogMissing);
    CHECK(missingCat->detail == L"pvscsi.cat");
    std::string unnamed = kPvscsi;
    unnamed.erase(unnamed.find("CatalogFile=pvscsi.cat\n"), 23);
    write(dir / L"nocat" / L"plain.inf", unnamed);
    const auto noCat = checkBootDriver(dir / L"nocat" / L"plain.inf", Architecture::X64);
    REQUIRE(noCat.has_value());
    CHECK(noCat->why == DriverRefusal::Why::NoCatalog);

    const auto gone = checkBootDriver(dir / L"nope.inf", Architecture::X64);
    REQUIRE(gone.has_value());
    CHECK(gone->why == DriverRefusal::Why::Missing);

    // How the rest reads (CLI, log).
    CHECK(describe(DriverRefusal{L"C:\\d\\rt.inf", DriverRefusal::Why::Unsigned, {}}) ==
          L"rt.inf: unsigned: its catalog does not match its files, Windows PE would not load it");
    CHECK(describe(DriverRefusal{L"C:\\d\\rt.inf", DriverRefusal::Why::Unsigned, L"0x80070032"}).ends_with(L"(DISM 0x80070032)"));
    CHECK(describe(DriverRefusal{L"C:\\d\\x.inf", DriverRefusal::Why::Dism, L"boom"}) == L"x.inf: boom");
}

TEST_CASE("boot drivers: which architecture an INF fits") {
    const auto x64only = parseInfText(L"[Manufacturer]\n%M%=M,NTamd64\n");
    CHECK(fitsArchitecture(x64only, Architecture::X64));
    CHECK_FALSE(fitsArchitecture(x64only, Architecture::Arm64));
    CHECK_FALSE(fitsArchitecture(x64only, Architecture::X86));
    CHECK(fitsArchitecture(x64only, Architecture::Unknown));
    const auto any = parseInfText(L"[Version]\nClass=Extension\n"); // no models: any
    CHECK(fitsArchitecture(any, Architecture::Arm64));
    CHECK(fitsArchitecture(any, Architecture::X64));
}

TEST_CASE("boot patch: nothing asked is nothing done, the drivers count as a patch") {
    BootPatch patch;
    CHECK(patch.empty());
    patch.drivers = {L"C:\\d\\pvscsi.inf"};
    CHECK_FALSE(patch.empty());
    CHECK(patch.labConfigValues().empty()); // drivers alone write no LabConfig value
}
