// D-047: the pure parts of the USB writer — FAT32 label, partition size, diskpart script, copy
// plan (4 GB limit → install.swm). Writing to a real disk is a lab test (tools/lab_usb.ps1, admin).
#include "core/usb/UsbMedia.h"

#include <doctest.h>

#include <windows.h>
#include <winioctl.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path setupFolder(const wchar_t* name) {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"usb" / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / L"sources");
    std::filesystem::create_directories(root / L"efi" / L"boot");
    std::ofstream(root / L"bootmgr", std::ios::binary) << "bootmgr";
    std::ofstream(root / L"efi" / L"boot" / L"bootx64.efi", std::ios::binary) << "efi";
    std::ofstream(root / L"sources" / L"boot.wim", std::ios::binary) << "boot-wim";
    return root;
}

// A sparse file of `size` bytes (takes no space; only its length matters to the plan).
void sparseFile(const std::filesystem::path& file, std::uint64_t size) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(size);
    SetFilePointerEx(h, end, nullptr, FILE_BEGIN);
    SetEndOfFile(h);
    CloseHandle(h);
}

} // namespace

TEST_CASE("usb: FAT32 labels are 11 characters of A-Z 0-9 _ -") {
    CHECK(fatLabel(L"WinLove") == L"WINLOVE");
    CHECK(fatLabel(L"Win 11 Türkçe Kurulum") == L"WIN_11_TURK");
    CHECK(fatLabel(L"ığüşöçİ") == L"IGUSOCI");
    CHECK(fatLabel(L"  ") == L"WINLOVE");
    CHECK(fatLabel(L"a:b*c?") == L"ABC");
}

TEST_CASE("usb: a disk larger than 32 GB gets a 32 GB partition; the script erases only the picked disk") {
    CHECK(usbPartitionMb(16ull << 30) == 0);
    CHECK(usbPartitionMb(64ull << 30) == 32000);
    const std::string mbr = diskpartScript(3, UsbScheme::MbrBiosUefi, 32000, L"Win Love");
    CHECK(mbr.starts_with("select disk 3\r\n"));
    CHECK(mbr.find("clean\r\n") != std::string::npos);
    CHECK(mbr.find("convert mbr\r\n") != std::string::npos);
    CHECK(mbr.find("create partition primary size=32000\r\n") != std::string::npos);
    CHECK(mbr.find("format fs=fat32 quick label=\"WIN_LOVE\"\r\n") != std::string::npos);
    CHECK(mbr.find("active\r\n") != std::string::npos);
    CHECK(mbr.find("assign\r\n") != std::string::npos);
    // "clean" comes right after selecting the disk (and clearing read-only), never before.
    CHECK(mbr.find("select disk 3") < mbr.find("clean"));
    const std::string gpt = diskpartScript(1, UsbScheme::GptUefi, 0, L"");
    CHECK(gpt.find("convert gpt\r\n") != std::string::npos);
    CHECK(gpt.find("create partition primary\r\n") != std::string::npos);
    CHECK(gpt.find("active") == std::string::npos);
    CHECK(gpt.find("label=\"WINLOVE\"") != std::string::npos);
}

TEST_CASE("usb: copy plan — sizes, replaced and root files, install.wim over 4 GB is split") {
    auto folder = setupFolder(L"plan");
    std::ofstream(folder / L"sources" / L"install.wim", std::ios::binary) << "small-wim";
    UsbOptions options;
    options.sourceFolder = folder;
    auto plan = planUsbCopy(options);
    REQUIRE(plan);
    CHECK_FALSE(plan->splitInstall);
    const std::uint64_t base = plan->bytes;

    // A patched boot.wim counts with its own size; autounattend.xml of the folder is replaced.
    const auto patched = folder.parent_path() / L"patched-boot.wim";
    std::ofstream(patched, std::ios::binary) << "patched-boot-wim-longer";
    std::ofstream(folder / L"autounattend.xml", std::ios::binary) << "old";
    options.replacedFiles.push_back({L"sources\\boot.wim", patched});
    options.rootFiles.push_back({L"autounattend.xml", std::string(100, 'x')});
    plan = planUsbCopy(options);
    REQUIRE(plan);
    CHECK(plan->bytes == base - 8 + 23 + 100);

    sparseFile(folder / L"sources" / L"install.wim", 5ull << 30);
    plan = planUsbCopy(options);
    REQUIRE(plan);
    CHECK(plan->splitInstall);
    CHECK(plan->installWim.filename() == L"install.wim");
}

TEST_CASE("usb: an ESD or another file over 4 GB cannot go to FAT32; a folder that is not setup media is refused") {
    auto folder = setupFolder(L"esd");
    sparseFile(folder / L"sources" / L"install.esd", 5ull << 30);
    UsbOptions options;
    options.sourceFolder = folder;
    auto plan = planUsbCopy(options);
    REQUIRE_FALSE(plan);
    CHECK(plan.error().code == ErrorCode::Unsupported);
    CHECK(plan.error().message.find(L"install.esd") != std::wstring::npos);

    std::filesystem::remove(folder / L"sources" / L"install.esd");
    sparseFile(folder / L"sources" / L"big.dat", 5ull << 30);
    CHECK_FALSE(planUsbCopy(options));

    options.sourceFolder = std::filesystem::temp_directory_path();
    CHECK_FALSE(planUsbCopy(options));
}

TEST_CASE("usb: the disk list never holds the system disk") {
    for (const auto& disk : listUsbDisks(true)) {
        CHECK_FALSE(disk.system);
    }
    bool sawSystem = false;
    for (const auto& disk : listAllDisks()) {
        sawSystem = sawSystem || disk.system;
    }
    CHECK(sawSystem); // the disk the tests run from
}
