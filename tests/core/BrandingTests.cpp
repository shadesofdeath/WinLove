// D-056 Kişiselleştirme engine pieces that need no mounted image: fresh files over hard links,
// pictures (WIC), fonts, Wi-Fi profiles and their post-setup step, Compact OS in the answer file.
#include "base/Utf8.h"
#include "core/image/Branding.h"
#include "core/image/Fonts.h"
#include "core/image/ImageFiles.h"
#include "core/postsetup/PostSetup.h"
#include "core/postsetup/Wifi.h"
#include "core/system/Picture.h"
#include "core/unattend/Unattend.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"branding" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string bytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write(const std::filesystem::path& file, const std::string& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

// A 24-bit BMP, width × height, a horizontal gradient: any WIC decoder reads it.
std::string bmp(int width, int height) {
    const int stride = (width * 3 + 3) & ~3;
    const std::uint32_t pixels = static_cast<std::uint32_t>(stride * height);
    std::string b(54 + pixels, '\0');
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            b[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
        }
    };
    b[0] = 'B';
    b[1] = 'M';
    put32(2, static_cast<std::uint32_t>(b.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    b[26] = 1;
    b[28] = 24;
    put32(34, pixels);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t at = 54 + static_cast<std::size_t>(y * stride + x * 3);
            b[at] = static_cast<char>(x * 255 / std::max(width - 1, 1));
            b[at + 1] = static_cast<char>(y * 255 / std::max(height - 1, 1));
            b[at + 2] = static_cast<char>(128);
        }
    }
    return b;
}

DWORD linkCount(const std::filesystem::path& file) {
    HANDLE h = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           0, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    return info.nNumberOfLinks;
}

} // namespace

TEST_CASE("replaceImageFile: a hard link gets a file of its own, the other name keeps its bytes") {
    const auto dir = scratch(L"links");
    write(dir / L"WinSxS" / L"img0.jpg", "store copy");
    std::filesystem::create_directories(dir / L"Web");
    REQUIRE(CreateHardLinkW((dir / L"Web" / L"img0.jpg").c_str(), (dir / L"WinSxS" / L"img0.jpg").c_str(), nullptr));
    REQUIRE(linkCount(dir / L"Web" / L"img0.jpg") == 2);

    REQUIRE(replaceImageFile(dir, L"Web\\img0.jpg", "ours"));
    CHECK(bytesOf(dir / L"Web" / L"img0.jpg") == "ours");
    CHECK(bytesOf(dir / L"WinSxS" / L"img0.jpg") == "store copy");
    CHECK(linkCount(dir / L"Web" / L"img0.jpg") == 1);
    CHECK(linkCount(dir / L"WinSxS" / L"img0.jpg") == 1);

    // A new file, and unlinking what is not there.
    REQUIRE(replaceImageFile(dir, L"Web\\new.bin", std::string(3, 'x')));
    CHECK(bytesOf(dir / L"Web" / L"new.bin") == "xxx");
    CHECK(unlinkImageFile(dir, L"Web\\missing.bin"));
    // Paths that leave the image are refused.
    CHECK_FALSE(replaceImageFile(dir, L"..\\escape.bin", "x"));
}

TEST_CASE("pictures: cover-scale to the size the image's own file has, in its format") {
    const auto dir = scratch(L"pictures");
    const auto source = dir / L"source.bmp";
    write(source, bmp(300, 200));
    auto size = pictureSize(source);
    REQUIRE(size);
    CHECK(size->width == 300);
    CHECK(size->height == 200);

    // An "image" with Windows' defaults (small ones, made here).
    const auto mount = dir / L"mount";
    const std::filesystem::path wall = mount / L"Windows" / L"Web" / L"Wallpaper" / L"Windows";
    const std::filesystem::path wall4k = mount / L"Windows" / L"Web" / L"4K" / L"Wallpaper" / L"Windows";
    const std::filesystem::path account = mount / L"ProgramData" / L"Microsoft" / L"User Account Pictures";
    write(wall / L"img0.jpg", *encodePicture(source, PictureFormat::Jpeg, 64, 40));
    write(wall4k / L"img0_32x20.jpg", *encodePicture(source, PictureFormat::Jpeg, 32, 20));
    write(account / L"user.png", *encodePicture(source, PictureFormat::Png, 48, 48));
    write(account / L"user-32.png", *encodePicture(source, PictureFormat::Png, 32, 32));
    write(account / L"user.bmp", *encodePicture(source, PictureFormat::Bmp, 48, 48));
    write(account / L"guest.png", "guest stays");
    std::filesystem::create_directories(mount / L"Windows" / L"System32");

    CHECK(pictureTargets(mount, PictureSlot::Wallpaper).size() == 2);
    CHECK(pictureTargets(mount, PictureSlot::Account).size() == 3);
    CHECK(pictureTargets(mount, PictureSlot::LockScreen).empty());
    CHECK_FALSE(applyPicture(mount, PictureSlot::LockScreen, source, TaskContext{})); // nothing to replace

    const auto other = dir / L"other.bmp";
    write(other, bmp(90, 160)); // portrait: cropped, not stretched
    auto wallpaper = applyPicture(mount, PictureSlot::Wallpaper, other, TaskContext{});
    REQUIRE(wallpaper);
    CHECK(wallpaper->files == 2);
    CHECK(pictureSize(wall / L"img0.jpg")->width == 64);
    CHECK(pictureSize(wall4k / L"img0_32x20.jpg")->height == 20);
    CHECK(bytesOf(wall / L"img0.jpg").substr(0, 2) == "\xFF\xD8");

    auto pictures = applyPicture(mount, PictureSlot::Account, other, TaskContext{});
    REQUIRE(pictures);
    CHECK(pictures->files == 3);
    CHECK(bytesOf(account / L"user.png").substr(1, 3) == "PNG");
    CHECK(bytesOf(account / L"user.bmp").substr(0, 2) == "BM");
    CHECK(pictureSize(account / L"user-32.png")->width == 32);
    CHECK(bytesOf(account / L"guest.png") == "guest stays");

    auto logo = applyPicture(mount, PictureSlot::OemLogo, other, TaskContext{});
    REQUIRE(logo);
    CHECK(pictureSize(mount / L"Windows" / L"System32" / L"oemlogo.bmp")->width == 120);
    REQUIRE(logo->registry.size() == 1);
    CHECK(logo->registry[0].name == L"Logo");

    write(dir / L"not-a-picture.png", "text");
    CHECK_FALSE(applyPicture(mount, PictureSlot::Wallpaper, dir / L"not-a-picture.png", TaskContext{}));
    CHECK(pictureSlotFromKey(L"lockscreen") == PictureSlot::LockScreen);
    CHECK_FALSE(pictureSlotFromKey(L"desktop"));
}

TEST_CASE("fonts: the name Windows gives the registry value") {
    const std::filesystem::path segoe = L"C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::exists(segoe)) {
        auto info = readFontInfo(segoe);
        REQUIRE(info);
        CHECK(info->registryName() == L"Segoe UI (TrueType)");
    }
    const std::filesystem::path cambria = L"C:\\Windows\\Fonts\\cambria.ttc";
    if (std::filesystem::exists(cambria)) {
        CHECK(readFontInfo(cambria)->registryName() == L"Cambria & Cambria Math (TrueType)");
    }
    CHECK_FALSE(parseFont("not a font at all"));
    CHECK(fontFileName(L"C:\\x\\My Font-Bold.ttf") == L"My Font-Bold.ttf");
    CHECK(fontFileName(L"C:\\x\\Yazı tipi.otf") == L"Yaz_ tipi.otf");
    CHECK(isFontFile(L"a.TTC"));
    CHECK_FALSE(isFontFile(L"a.fon"));
}

TEST_CASE("wifi: profile XML, validation, and a post-setup step that runs as SYSTEM") {
    WifiNetwork n{L"Ev & <Ofis>", L"sifre12345", WifiSecurity::Wpa2Personal, true};
    CHECK(validateWifi(n) == WifiProblem::None);
    const std::wstring xml = wifiProfileXml(n);
    CHECK(xml.find(L"<name>Ev &amp; &lt;Ofis&gt;</name>") != std::wstring::npos);
    CHECK(xml.find(L"<authentication>WPA2PSK</authentication>") != std::wstring::npos);
    CHECK(xml.find(L"<nonBroadcast>true</nonBroadcast>") != std::wstring::npos);
    CHECK(wifiProfileName(xml) == L"Ev &amp; &lt;Ofis&gt;");
    CHECK(wifiProfilePortable(xml));
    CHECK_FALSE(wifiProfilePortable(L"<sharedKey><protected>true</protected></sharedKey>"));

    CHECK(validateWifi({L"", L"sifre12345"}) == WifiProblem::Ssid);
    CHECK(validateWifi({std::wstring(33, L'a'), L"sifre12345"}) == WifiProblem::Ssid);
    CHECK(validateWifi({L"x", L"short"}) == WifiProblem::Password);
    CHECK(validateWifi({L"x", L"", WifiSecurity::Open}) == WifiProblem::None);
    CHECK(wifiProfileXml({L"x", L"", WifiSecurity::Open}).find(L"sharedKey") == std::wstring::npos);
    CHECK(wifiProfileXml({L"x", L"sifre12345", WifiSecurity::Wpa3Personal}).find(L"WPA3SAE") != std::wstring::npos);

    PostSetupPlan plan; // first logon: the Wi-Fi step still runs in the machine script
    plan.steps.push_back({PostSetupStep::Type::Wifi, L"Ev", xml});
    plan.steps.push_back({PostSetupStep::Type::Winget, L"7-Zip", L"7zip.7zip"});
    CHECK(validatePostSetup(plan).empty());
    const auto scripts = buildPostSetupScripts(plan);
    CHECK(scripts.machine.find(L"netsh wlan add profile filename=\"%WL%\\files\\1\\wifi.xml\" user=all") != std::wstring::npos);
    CHECK(scripts.machine.find(L"del /f /q \"%WL%\\files\\1\\wifi.xml\"") != std::wstring::npos);
    CHECK(scripts.user.find(L"netsh") == std::wstring::npos);
    auto back = postSetupFromJson(postSetupToJson(plan));
    REQUIRE(back);
    CHECK(*back == plan);

    PostSetupPlan bad;
    bad.steps.push_back({PostSetupStep::Type::Wifi, L"x", L"<notaprofile/>"});
    REQUIRE(validatePostSetup(bad).size() == 1);
    CHECK(validatePostSetup(bad)[0].second == PostSetupProblem::BadWifiProfile);

    // The staged profile lands where the script reads it.
    const auto mount = scratch(L"wifi");
    std::filesystem::create_directories(mount / L"Windows" / L"Setup" / L"Scripts");
    REQUIRE(applyPostSetup(mount, plan, TaskContext{}));
    const auto staged = mount / L"Windows" / L"Setup" / L"Scripts" / L"WinLove" / L"files" / L"1" / L"wifi.xml";
    CHECK(bytesOf(staged).find("<WLANProfile") != std::string::npos);
}

TEST_CASE("unattend: Compact OS goes into windowsPE OSImage and is read back") {
    UnattendOptions o;
    o.compactOs = true;
    const std::wstring xml = buildUnattendXml(o);
    CHECK(xml.find(L"<Compact>true</Compact>") != std::wstring::npos);
    CHECK(xml.find(L"<InstallTo>") == std::wstring::npos); // nothing else asked for
    auto back = parseUnattendXml(utf8::fromWide(xml));
    REQUIRE(back);
    CHECK(back->compactOs);
    CHECK(buildUnattendXml(UnattendOptions{}).find(L"Compact") == std::wstring::npos);
}
