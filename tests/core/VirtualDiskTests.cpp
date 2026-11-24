// D-141: the pure parts of the VHD / VHDX source — no disk is ever attached here. The record file
// is pointed at a temporary folder; records whose changes disk does not exist are only read and
// forgotten (no virtdisk call is made for them).
#include "core/image/Source.h"
#include "core/image/VirtualDisk.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl::core;
namespace fs = std::filesystem;

namespace {

VolumeCandidate volume(const wchar_t* guid, const wchar_t* fileSystem, std::uint64_t gib, bool windows) {
    return VolumeCandidate{guid, fileSystem, gib << 30, (gib << 30) / 2, windows};
}

// The record file goes to a fresh temporary folder for the test and back to the default after it.
struct TempRecords {
    fs::path folder = fs::temp_directory_path() / L"wl-tests" / L"vhd-records";
    TempRecords() {
        std::error_code ec;
        fs::remove_all(folder, ec);
        fs::create_directories(folder, ec);
        setVhdRecordFile(folder / L"vhd-mounts.json");
    }
    ~TempRecords() {
        setVhdRecordFile({});
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
};

VhdMountRecord record(const fs::path& base, const wchar_t* name) {
    VhdMountRecord r;
    r.vhd = base / (std::wstring(name) + L".vhdx");
    r.child = base / L"changes" / (std::wstring(name) + L".winlove-changes.vhdx");
    r.folder = base / (std::wstring(L"mnt-") + name);
    r.volume = L"\\\\?\\Volume{00000000-1111-2222-3333-444444444444}\\";
    r.attachedAt = 1'762'800'000;
    r.by = L"wlcli";
    return r;
}

} // namespace

TEST_CASE("virtual disk paths are recognised by extension only") {
    CHECK(isVirtualDiskPath(L"C:\\lab\\golden.vhdx"));
    CHECK(isVirtualDiskPath(L"D:\\Disks\\WIN10.VHD"));
    CHECK_FALSE(isVirtualDiskPath(L"C:\\lab\\golden.vhdx.bak"));
    CHECK_FALSE(isVirtualDiskPath(L"C:\\lab\\install.wim"));
    CHECK_FALSE(isVirtualDiskPath(L"C:\\lab\\x64.iso"));
    CHECK(isVirtualDiskFormat(ImageFormat::Vhd));
    CHECK(isVirtualDiskFormat(ImageFormat::Vhdx));
    CHECK_FALSE(isVirtualDiskFormat(ImageFormat::Folder));
}

TEST_CASE("the Windows volume is the largest NTFS volume with a SYSTEM hive") {
    // A typical installed disk: EFI (FAT32), MSR (no file system), Windows, recovery.
    const std::vector<VolumeCandidate> installed{
        volume(L"\\\\?\\Volume{a}\\", L"FAT32", 0, false), volume(L"\\\\?\\Volume{b}\\", L"", 0, false),
        volume(L"\\\\?\\Volume{c}\\", L"NTFS", 39, true), volume(L"\\\\?\\Volume{d}\\", L"NTFS", 1, false)};
    REQUIRE(chooseWindowsVolume(installed).has_value());
    CHECK(*chooseWindowsVolume(installed) == 2);

    // Two Windows volumes: the larger one.
    const std::vector<VolumeCandidate> two{volume(L"\\\\?\\Volume{s}\\", L"NTFS", 10, true),
                                           volume(L"\\\\?\\Volume{l}\\", L"ntfs", 30, true)};
    CHECK(*chooseWindowsVolume(two) == 1);

    // None: no Windows at all, Windows only on a non-NTFS volume, nothing.
    CHECK_FALSE(chooseWindowsVolume(std::vector<VolumeCandidate>{volume(L"\\\\?\\Volume{x}\\", L"NTFS", 40, false)}).has_value());
    CHECK_FALSE(chooseWindowsVolume(std::vector<VolumeCandidate>{volume(L"\\\\?\\Volume{y}\\", L"ReFS", 40, true)}).has_value());
    CHECK_FALSE(chooseWindowsVolume(std::vector<VolumeCandidate>{}).has_value());
}

TEST_CASE("the disk number comes from the attached disk's physical path") {
    CHECK(diskNumberFromPhysicalPath(L"\\\\.\\PhysicalDrive7") == 7u);
    CHECK(diskNumberFromPhysicalPath(L"\\\\.\\physicaldrive12") == 12u);
    CHECK(diskNumberFromPhysicalPath(L"\\\\.\\PhysicalDrive0") == 0u);
    CHECK_FALSE(diskNumberFromPhysicalPath(L"\\\\.\\PhysicalDrive").has_value());
    CHECK_FALSE(diskNumberFromPhysicalPath(L"\\\\.\\PhysicalDrive7x").has_value());
    CHECK_FALSE(diskNumberFromPhysicalPath(L"\\\\.\\CdRom0").has_value());
    CHECK_FALSE(diskNumberFromPhysicalPath(L"C:\\").has_value());
}

TEST_CASE("the changes disk has the parent's format and lives in the work folder") {
    CHECK(changesDiskPath(L"D:\\Disks\\Win11.vhdx", L"C:\\WinLove\\vhd") == fs::path(L"C:\\WinLove\\vhd\\Win11.winlove-changes.vhdx"));
    CHECK(changesDiskPath(L"D:\\Disks\\Old.VHD", L"C:\\WinLove\\vhd") == fs::path(L"C:\\WinLove\\vhd\\Old.winlove-changes.vhd"));
}

TEST_CASE("the installed Windows becomes one edition, named from its edition id") {
    OfflineWindowsFacts facts;
    // What the lab's applied 26200 Pro image says (2026-11-11): ProductName is its servicing base's.
    facts.productName = L"Windows 10 Enterprise";
    facts.editionId = L"Professional";
    facts.installationType = L"Client";
    facts.build = 26200;
    facts.ubr = 8037;
    facts.processorArchitecture = L"AMD64";
    facts.languages = {L"tr-TR"};
    facts.defaultLanguage = L"tr-TR";
    const ImageInfo info = imageInfoFromFacts(facts, 20ull << 30);
    CHECK(info.index == 1);
    CHECK(info.name == L"Windows 11 Pro");
    CHECK(info.displayName == L"Windows 11 Pro");
    CHECK(info.editionId == L"Professional");
    CHECK(info.flags == L"Professional");
    CHECK(info.architecture == Architecture::X64);
    CHECK(info.versionString() == L"10.0.26200.8037");
    CHECK(info.totalBytes == (20ull << 30));
    CHECK(info.defaultLanguage == L"tr-TR");
    CHECK(isWindowsEdition(info));

    facts.build = 19045;
    facts.processorArchitecture = L"ARM64";
    facts.defaultLanguage.clear();
    const ImageInfo ten = imageInfoFromFacts(facts, 0);
    CHECK(ten.name == L"Windows 10 Pro");
    CHECK(ten.architecture == Architecture::Arm64);
    CHECK(ten.defaultLanguage == L"tr-TR"); // the first UI language when InstallLanguage is not readable

    // A server keeps ProductName ("10" turned into "11" from build 22000 only for client names).
    OfflineWindowsFacts server;
    server.productName = L"Windows Server 2025 Datacenter";
    server.editionId = L"ServerDatacenter";
    server.installationType = L"Server";
    server.build = 26100;
    CHECK(imageInfoFromFacts(server, 0).name == L"Windows Server 2025 Datacenter");
    CHECK(imageInfoFromFacts(server, 0).productType == L"ServerNT");
    // No edition id: ProductName, with 11 for 10 from build 22000.
    OfflineWindowsFacts noId;
    noId.productName = L"Windows 10 Home";
    noId.build = 22631;
    CHECK(imageInfoFromFacts(noId, 0).name == L"Windows 11 Home");

    OfflineWindowsFacts empty;
    CHECK(imageInfoFromFacts(empty, 0).name == L"Windows");
    CHECK(imageInfoFromFacts(empty, 0).architecture == Architecture::Unknown);
}

TEST_CASE("what a source can do: a VHD has one edition, no WIM, no setup media, and can be discarded") {
    SourceInfo vhd;
    vhd.format = ImageFormat::Vhdx;
    const auto v = sourceCapabilities(vhd);
    CHECK(v.virtualDisk);
    CHECK(v.mountable);
    CHECK_FALSE(v.wimEditions);
    CHECK_FALSE(v.verifiable);
    CHECK_FALSE(v.setupMedia);
    CHECK(v.discardable);

    SourceInfo iso;
    iso.format = ImageFormat::Iso;
    iso.installImage = L"sources/install.wim";
    iso.install.header.compression = WimCompression::Lzx;
    const auto i = sourceCapabilities(iso);
    CHECK_FALSE(i.virtualDisk);
    CHECK(i.setupMedia);
    CHECK(i.wimEditions);
    CHECK(i.mountable);

    SourceInfo esd;
    esd.format = ImageFormat::Esd;
    esd.installImage = L"install.esd";
    esd.install.header.compression = WimCompression::Lzms;
    esd.install.header.solid = true;
    const auto e = sourceCapabilities(esd);
    CHECK_FALSE(e.mountable);
    CHECK_FALSE(e.verifiable);
    CHECK_FALSE(e.wimEditions);

    SourceInfo wim;
    wim.format = ImageFormat::Wim;
    wim.installImage = L"install.wim";
    CHECK(sourceCapabilities(wim).wimEditions);
    CHECK_FALSE(sourceCapabilities(wim).setupMedia);
    wim.install.header.totalParts = 3; // split: join first
    CHECK_FALSE(sourceCapabilities(wim).wimEditions);
}

TEST_CASE("a record's state says what to do, and never deletes while the user's disk is away") {
    CHECK(classifyVhdMount(true, true, true, true) == VhdMountState::Ok);
    CHECK(classifyVhdMount(true, true, false, false) == VhdMountState::NotAttached);
    CHECK(classifyVhdMount(true, true, true, false) == VhdMountState::NoMountPoint);
    CHECK(classifyVhdMount(true, false, false, false) == VhdMountState::ChildMissing);
    CHECK(classifyVhdMount(false, false, false, false) == VhdMountState::ChildMissing);
    CHECK(classifyVhdMount(false, true, true, true) == VhdMountState::DiskMissing);
    CHECK(classifyVhdMount(false, true, false, false) == VhdMountState::DiskMissing);

    CHECK(recommendedVhdAction(VhdMountState::Ok) == VhdAction::None);
    CHECK(recommendedVhdAction(VhdMountState::NotAttached) == VhdAction::Remount);
    CHECK(recommendedVhdAction(VhdMountState::NoMountPoint) == VhdAction::Remount);
    CHECK(recommendedVhdAction(VhdMountState::ChildMissing) == VhdAction::Forget);
    CHECK(recommendedVhdAction(VhdMountState::DiskMissing) == VhdAction::None);
    CHECK(std::wstring(vhdMountStateName(VhdMountState::NotAttached)) == L"not attached");
    CHECK(std::wstring(vhdActionName(VhdAction::Remount)) == L"remount");
}

TEST_CASE("the record file survives a round trip and refuses what it cannot trust") {
    const fs::path base = L"C:\\WinLove";
    const std::vector<VhdMountRecord> records{record(base, L"golden"), record(base, L"İş diski")};
    const auto parsed = parseVhdRecords(serializeVhdRecords(records));
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0].vhd == records[0].vhd);
    CHECK(parsed[0].child == records[0].child);
    CHECK(parsed[0].folder == records[0].folder);
    CHECK(parsed[0].volume == records[0].volume);
    CHECK(parsed[0].attachedAt == records[0].attachedAt);
    CHECK(parsed[0].by == L"wlcli");
    CHECK(parsed[1].vhd == records[1].vhd); // non-ASCII names (UTF-8 in the file)

    CHECK(parseVhdRecords("").empty());
    CHECK(parseVhdRecords("{not json").empty());
    CHECK(parseVhdRecords(R"({"format":"something.else","mounts":[]})").empty());
    CHECK(parseVhdRecords(R"({"format":"winlove.vhd-mounts","mounts":{}})").empty());
    // Relative paths, a child that is not a virtual disk, missing fields: skipped, never acted on.
    const char* doubtful = R"({"format":"winlove.vhd-mounts","version":1,"mounts":[
        {"vhd":"golden.vhdx","child":"C:\\c\\x.vhdx","folder":"C:\\m"},
        {"vhd":"C:\\d\\golden.vhdx","child":"C:\\c\\x.txt","folder":"C:\\m"},
        {"vhd":"C:\\d\\golden.vhdx","child":"C:\\c\\x.vhdx"},
        42,
        {"vhd":"C:\\d\\golden.vhdx","child":"C:\\c\\x.vhdx","folder":"C:\\m","attachedAt":"soon"}]})";
    const auto kept = parseVhdRecords(doubtful);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].attachedAt == 0);
}

TEST_CASE("records are found by folder and by disk; a record without its changes disk is forgotten") {
    TempRecords temp;
    const auto a = record(temp.folder, L"a");
    const auto b = record(temp.folder, L"b");
    REQUIRE(saveVhdRecords({a, b}));
    CHECK(fs::exists(vhdRecordFile()));
    CHECK(loadVhdRecords().size() == 2);
    // The folder as given in another case and with a trailing backslash is the same folder.
    std::wstring spelled = a.folder.wstring();
    for (auto& c : spelled) {
        c = static_cast<wchar_t>(towupper(c));
    }
    REQUIRE(vhdRecordForFolder(spelled + L"\\").has_value());
    CHECK(vhdRecordForFolder(spelled + L"\\")->vhd == a.vhd);
    REQUIRE(vhdRecordForDisk(b.vhd).has_value());
    CHECK(vhdRecordForDisk(b.vhd)->folder == b.folder);
    CHECK_FALSE(vhdRecordForFolder(temp.folder / L"elsewhere").has_value());

    // Neither changes disk exists: nothing can be attached, the action is to forget the record.
    fs::create_directories(a.folder); // an empty folder WinLove left: removed with the record
    const auto checks = inspectVhdMounts();
    REQUIRE(checks.size() == 2);
    CHECK(checks[0].state == VhdMountState::ChildMissing);
    CHECK(checks[0].action == VhdAction::Forget);
    auto forgotten = repairVirtualDisk(checks[0], TaskContext{});
    REQUIRE(forgotten.has_value());
    CHECK_FALSE(fs::exists(a.folder));
    CHECK(loadVhdRecords().size() == 1);

    // A folder with someone's file in it is never removed.
    fs::create_directories(b.folder);
    std::ofstream(b.folder / L"keep.txt") << "mine";
    auto second = repairVirtualDisk(*inspectVhdMount(b.folder), TaskContext{});
    REQUIRE(second.has_value());
    CHECK(fs::exists(b.folder / L"keep.txt"));
    // The last record gone: the file goes too.
    CHECK(loadVhdRecords().empty());
    CHECK_FALSE(fs::exists(vhdRecordFile()));

    // Unmounting a folder WinLove has no record of says so and touches nothing.
    const auto none = unmountVirtualDisk(b.folder, /*commit=*/false, TaskContext{});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().code == wl::ErrorCode::NotFound);
    CHECK(fs::exists(b.folder / L"keep.txt"));
}

TEST_CASE("a missing virtual disk is reported before anything is attached") {
    TempRecords temp;
    const auto missing = openSource(temp.folder / L"nowhere.vhdx");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == wl::ErrorCode::NotFound);
    // Nor can a disk be mounted that is not there (before any record is written).
    const auto refused = mountVirtualDisk(temp.folder / L"nowhere.vhdx", temp.folder / L"mnt", temp.folder / L"changes", L"test",
                                          TaskContext{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(loadVhdRecords().empty());
    CHECK_FALSE(fs::exists(temp.folder / L"mnt"));
}
