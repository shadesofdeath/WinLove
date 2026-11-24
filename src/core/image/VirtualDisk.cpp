#include "core/image/VirtualDisk.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/OffReg.h"
#include "core/image/Source.h"
#include "core/image/dism/Edition.h"
#include "core/image/dism/MountHealth.h"
#include "core/system/FileLocks.h"
#include "core/system/Privileges.h"

#include <json.hpp>

#include <windows.h>
#include <winioctl.h>
#include <virtdisk.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <ctime>
#include <format>
#include <thread>

namespace wl::core {

namespace {

using nlohmann::json;

// VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT, here so that no GUID library is needed.
constexpr GUID kVendorMicrosoft = {0xec984aec, 0xa0f9, 0x47e9, {0x90, 0x1f, 0x71, 0x41, 0x5a, 0x66, 0x34, 0x5b}};

std::filesystem::path g_recordFile; // empty: the default

VIRTUAL_STORAGE_TYPE storageType(ImageFormat format) {
    VIRTUAL_STORAGE_TYPE type{};
    type.DeviceId = format == ImageFormat::Vhd ? VIRTUAL_STORAGE_TYPE_DEVICE_VHD : VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    type.VendorId = kVendorMicrosoft;
    return type;
}

VIRTUAL_STORAGE_TYPE storageTypeOf(const std::filesystem::path& file) {
    return storageType(formatFromPath(file.wstring()));
}

class DiskHandle {
public:
    DiskHandle() = default;
    explicit DiskHandle(HANDLE h) : m_h(h) {}
    DiskHandle(DiskHandle&& o) noexcept : m_h(std::exchange(o.m_h, nullptr)) {}
    DiskHandle& operator=(DiskHandle&& o) noexcept {
        if (this != &o) {
            reset();
            m_h = std::exchange(o.m_h, nullptr);
        }
        return *this;
    }
    DiskHandle(const DiskHandle&) = delete;
    DiskHandle& operator=(const DiskHandle&) = delete;
    ~DiskHandle() { reset(); }
    void reset() {
        if (m_h && m_h != INVALID_HANDLE_VALUE) {
            CloseHandle(m_h);
        }
        m_h = nullptr;
    }
    [[nodiscard]] HANDLE get() const noexcept { return m_h; }
    HANDLE* out() noexcept {
        reset();
        return &m_h;
    }

private:
    HANDLE m_h = nullptr;
};

std::unexpected<Error> win32Failure(ErrorCode code, std::wstring message, std::wstring context, DWORD status) {
    log::error("vhd", std::format(L"{} ({}) [0x{:08X}]", message, context, static_cast<unsigned>(HRESULT_FROM_WIN32(status))));
    return fail(code, std::move(message), std::move(context), static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
}

// Opened for what a v1 open allows (attach, detach, information, merge): `rwDepth` stores of the
// chain read / write, the rest read only.
Result<DiskHandle> openDiskV1(const std::filesystem::path& file, VIRTUAL_DISK_ACCESS_MASK access, ULONG rwDepth) {
    VIRTUAL_STORAGE_TYPE type = storageTypeOf(file);
    OPEN_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    params.Version1.RWDepth = rwDepth;
    DiskHandle handle;
    const DWORD status = OpenVirtualDisk(&type, file.c_str(), access, OPEN_VIRTUAL_DISK_FLAG_NONE, &params, handle.out());
    if (status != ERROR_SUCCESS) {
        return win32Failure(ErrorCode::IoError, L"cannot open the virtual disk", file.wstring(), status);
    }
    return handle;
}

// For questions about a disk that may be attached (by this or another process) and for detaching
// it: no open parameters (a v1 open with a read / write depth asks for the file itself and is
// refused while the disk is attached — lab 2026-11-11: every attached disk read "not attached"),
// then, for information only, a v2 GetInfoOnly open.
Result<DiskHandle> openDiskQuery(const std::filesystem::path& file, VIRTUAL_DISK_ACCESS_MASK access) {
    VIRTUAL_STORAGE_TYPE type = storageTypeOf(file);
    DiskHandle handle;
    const DWORD status = OpenVirtualDisk(&type, file.c_str(), access, OPEN_VIRTUAL_DISK_FLAG_NONE, nullptr, handle.out());
    if (status == ERROR_SUCCESS) {
        return handle;
    }
    if ((access & ~VIRTUAL_DISK_ACCESS_GET_INFO) == 0) {
        OPEN_VIRTUAL_DISK_PARAMETERS params{};
        params.Version = OPEN_VIRTUAL_DISK_VERSION_2;
        params.Version2.GetInfoOnly = TRUE;
        const DWORD second = OpenVirtualDisk(&type, file.c_str(), VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NONE, &params,
                                             handle.out());
        if (second == ERROR_SUCCESS) {
            return handle;
        }
        log::warn("vhd", std::format(L"information open refused too (0x{:08X})", static_cast<unsigned>(HRESULT_FROM_WIN32(second))));
    }
    return win32Failure(ErrorCode::IoError, L"cannot open the virtual disk", file.wstring(), status);
}

// Read only (v2 open): nothing of the file is opened for writing.
Result<DiskHandle> openDiskReadOnly(const std::filesystem::path& file) {
    VIRTUAL_STORAGE_TYPE type = storageTypeOf(file);
    OPEN_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = OPEN_VIRTUAL_DISK_VERSION_2;
    params.Version2.GetInfoOnly = FALSE;
    params.Version2.ReadOnly = TRUE;
    DiskHandle handle;
    const DWORD status = OpenVirtualDisk(&type, file.c_str(), VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NONE, &params,
                                         handle.out());
    if (status != ERROR_SUCCESS) {
        return win32Failure(ErrorCode::IoError, L"cannot open the virtual disk (read only)", file.wstring(), status);
    }
    return handle;
}

std::optional<std::wstring> physicalPath(HANDLE disk) {
    std::wstring path(MAX_PATH * 2, L'\0');
    ULONG bytes = static_cast<ULONG>(path.size() * sizeof(wchar_t));
    if (GetVirtualDiskPhysicalPath(disk, &bytes, path.data()) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    path.resize(wcsnlen(path.c_str(), path.size()));
    return path;
}

// Sizes, kind (fixed / dynamic / differencing) and the parent of an open disk.
void readDiskInfo(HANDLE disk, VirtualDiskInfo& info) {
    std::vector<std::uint8_t> buffer(64 * 1024);
    auto query = [&](GET_VIRTUAL_DISK_INFO_VERSION version) -> GET_VIRTUAL_DISK_INFO* {
        std::fill(buffer.begin(), buffer.end(), std::uint8_t{0});
        auto* data = reinterpret_cast<GET_VIRTUAL_DISK_INFO*>(buffer.data());
        data->Version = version;
        ULONG size = static_cast<ULONG>(buffer.size());
        ULONG used = 0;
        return GetVirtualDiskInformation(disk, &size, data, &used) == ERROR_SUCCESS ? data : nullptr;
    };
    if (const auto* size = query(GET_VIRTUAL_DISK_INFO_SIZE)) {
        info.virtualSize = size->Size.VirtualSize;
    }
    if (const auto* subtype = query(GET_VIRTUAL_DISK_INFO_PROVIDER_SUBTYPE)) {
        info.dynamic = subtype->ProviderSubtype == 3;
        info.differencing = subtype->ProviderSubtype == 4;
    }
    if (info.differencing) {
        if (const auto* parent = query(GET_VIRTUAL_DISK_INFO_PARENT_LOCATION)) {
            info.parent = std::wstring(parent->ParentLocation.ParentLocationBuffer);
        }
    }
}

std::uint64_t fileSizeOf(const std::filesystem::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool fileExists(const std::filesystem::path& file) {
    const DWORD attributes = GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring withoutSlash(std::wstring text) {
    while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }
    return text;
}

std::wstring withSlash(std::wstring text) {
    if (text.empty() || text.back() != L'\\') {
        text.push_back(L'\\');
    }
    return text;
}

bool samePathText(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(withoutSlash(nativePath(a).wstring()).c_str(), withoutSlash(nativePath(b).wstring()).c_str()) == 0;
}

// Every volume with an extent on disk `number` (no other volume is opened beyond asking for its extents).
std::vector<VolumeCandidate> volumesOnDisk(std::uint32_t number) {
    std::vector<VolumeCandidate> out;
    wchar_t name[MAX_PATH]{};
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        const std::wstring guid = name;
        const std::wstring device = withoutSlash(guid);
        HANDLE volume = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (volume == INVALID_HANDLE_VALUE) {
            continue;
        }
        alignas(8) std::uint8_t buffer[2048]{};
        DWORD bytes = 0;
        bool onDisk = false;
        if (DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer, sizeof(buffer), &bytes, nullptr)) {
            const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer);
            for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) {
                onDisk = onDisk || extents->Extents[i].DiskNumber == number;
            }
        }
        CloseHandle(volume);
        if (!onDisk) {
            continue;
        }
        VolumeCandidate candidate;
        candidate.guidPath = withSlash(guid);
        wchar_t fileSystem[MAX_PATH + 1]{};
        if (GetVolumeInformationW(candidate.guidPath.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fileSystem, MAX_PATH + 1)) {
            candidate.fileSystem = fileSystem;
        }
        ULARGE_INTEGER freeCaller{}, total{}, freeTotal{};
        if (GetDiskFreeSpaceExW(candidate.guidPath.c_str(), &freeCaller, &total, &freeTotal)) {
            candidate.size = total.QuadPart;
            candidate.freeBytes = freeTotal.QuadPart;
        }
        candidate.hasWindows = fileExists(candidate.guidPath + L"Windows\\System32\\config\\SYSTEM");
        out.push_back(std::move(candidate));
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    return out;
}

std::wstring volumeLabelOf(const std::wstring& guid) {
    wchar_t label[MAX_PATH + 1]{};
    if (GetVolumeInformationW(guid.c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) {
        return label;
    }
    return {};
}

struct FoundVolume {
    VolumeCandidate volume;
    int count = 0;
};

// The volumes of a freshly attached disk arrive a moment after the attach: asked again until the
// Windows volume is there (or `seconds` passed).
Result<FoundVolume> waitForWindowsVolume(HANDLE disk, const std::filesystem::path& file, int seconds) {
    const auto physical = physicalPath(disk);
    if (!physical) {
        return fail(ErrorCode::IoError, L"the attached disk has no device path", file.wstring());
    }
    const auto number = diskNumberFromPhysicalPath(*physical);
    if (!number) {
        return fail(ErrorCode::IoError, L"the attached disk's device path is not a physical drive", *physical);
    }
    log::info("vhd", std::format(L"{} attached as {} (disk {})", file.wstring(), *physical, *number));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::vector<VolumeCandidate> volumes;
    for (;;) {
        volumes = volumesOnDisk(*number);
        if (const auto chosen = chooseWindowsVolume(volumes)) {
            return FoundVolume{volumes[*chosen], static_cast<int>(volumes.size())};
        }
        if (std::chrono::steady_clock::now() > deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::wstring seen;
    for (const auto& v : volumes) {
        seen += std::format(L"{}{} {} {:.1f} GB{}", seen.empty() ? L"" : L"; ", v.guidPath, v.fileSystem.empty() ? L"?" : v.fileSystem,
                            static_cast<double>(v.size) / (1024.0 * 1024 * 1024), v.hasWindows ? L" Windows" : L"");
    }
    return fail(ErrorCode::NotFound,
                volumes.empty() ? L"no volume of the virtual disk appeared (is the disk offline, or not partitioned?)"
                                : L"the virtual disk has no NTFS volume with Windows\\System32\\config\\SYSTEM",
                file.wstring() + (seen.empty() ? L"" : L" — " + seen));
}

// A drive letter Windows gave the volume anyway (a letter remembered for it, say): removed — only on
// this one volume, found by its GUID.
void removeDriveLetters(const std::wstring& guid) {
    std::wstring names(4096, L'\0');
    DWORD length = 0;
    if (!GetVolumePathNamesForVolumeNameW(guid.c_str(), names.data(), static_cast<DWORD>(names.size()), &length)) {
        return;
    }
    for (const wchar_t* p = names.c_str(); *p; p += wcslen(p) + 1) {
        const std::wstring path = p;
        if (path.size() == 3 && path[1] == L':' && path[2] == L'\\') {
            if (DeleteVolumeMountPointW(path.c_str())) {
                log::info("vhd", L"drive letter removed from the image volume: " + path);
            } else {
                log::warn("vhd", std::format(L"could not remove the drive letter {} (0x{:08X})", path,
                                             static_cast<unsigned>(HRESULT_FROM_WIN32(GetLastError()))));
            }
        }
    }
}

// The volume GUID path mounted on `folder`; empty when the folder is not a mount point (or its
// volume is gone).
std::wstring volumeOnFolder(const std::filesystem::path& folder) {
    wchar_t guid[MAX_PATH]{};
    if (!GetVolumeNameForVolumeMountPointW(withSlash(nativePath(folder).wstring()).c_str(), guid, MAX_PATH)) {
        return {};
    }
    return withSlash(guid);
}

bool isReparseFolder(const std::filesystem::path& folder) {
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// The folder WinLove mounts on must be empty (or missing) and its own: never a mount point already,
// never a folder with someone's files.
Result<void> prepareFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) {
        if (isReparseFolder(folder)) {
            return fail(ErrorCode::InvalidArgument, L"the mount folder is already a mount point or a link", folder.wstring());
        }
        if (!std::filesystem::is_directory(folder, ec) || !std::filesystem::is_empty(folder, ec)) {
            return fail(ErrorCode::InvalidArgument, L"the mount folder must be an empty folder", folder.wstring());
        }
        return {};
    }
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"could not create the mount folder", folder.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
    }
    return {};
}

Result<void> putOnFolder(const std::wstring& guid, const std::filesystem::path& folder) {
    if (auto r = prepareFolder(folder); !r) {
        return r;
    }
    if (!SetVolumeMountPointW(withSlash(folder.wstring()).c_str(), guid.c_str())) {
        return win32Failure(ErrorCode::IoError, L"could not mount the Windows volume on the folder", folder.wstring() + L" <- " + guid,
                            GetLastError());
    }
    log::info("vhd", L"volume " + guid + L" mounted on " + folder.wstring());
    return {};
}

// Removes the mount point WinLove put on `folder` (only when it is the recorded volume's, or a
// dangling one), then the folder itself when it is empty.
void takeOffFolder(const std::filesystem::path& folder, const std::wstring& recordedVolume) {
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec) && !isReparseFolder(folder)) {
        return;
    }
    if (isReparseFolder(folder)) {
        const std::wstring now = volumeOnFolder(folder);
        if (now.empty() || recordedVolume.empty() || _wcsicmp(now.c_str(), recordedVolume.c_str()) == 0) {
            if (!DeleteVolumeMountPointW(withSlash(folder.wstring()).c_str())) {
                log::warn("vhd", std::format(L"could not remove the mount point {} (0x{:08X})", folder.wstring(),
                                             static_cast<unsigned>(HRESULT_FROM_WIN32(GetLastError()))));
            } else {
                log::info("vhd", L"mount point removed: " + folder.wstring());
            }
        } else {
            log::warn("vhd", L"the folder holds another volume than the recorded one; left as it is: " + folder.wstring());
            return;
        }
    }
    if (std::filesystem::is_directory(folder, ec) && !isReparseFolder(folder) && std::filesystem::is_empty(folder, ec)) {
        RemoveDirectoryW(folder.c_str());
    }
}

// \\?\Volume{…}\ → \Device\HarddiskVolume12 (what hivelist shows for a hive loaded from it).
std::wstring volumeDevice(const std::wstring& guid) {
    std::wstring name = withoutSlash(guid);
    constexpr std::wstring_view kPrefix = L"\\\\?\\";
    if (name.starts_with(kPrefix)) {
        name = name.substr(kPrefix.size());
    }
    wchar_t device[MAX_PATH]{};
    if (QueryDosDeviceW(name.c_str(), device, MAX_PATH) == 0) {
        return {};
    }
    return device;
}

// Locks and dismounts the volume so that everything is on the disk and nothing holds it; false when
// something kept it open (it is still flushed).
bool lockAndDismount(const std::wstring& guid) {
    const std::wstring device = withoutSlash(guid);
    HANDLE volume = CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        return false;
    }
    FlushFileBuffers(volume);
    DWORD bytes = 0;
    bool locked = false;
    for (int attempt = 0; attempt < 20 && !locked; ++attempt) {
        locked = DeviceIoControl(volume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr) != FALSE;
        if (!locked) {
            Sleep(500);
        }
    }
    if (locked) {
        DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr);
    } else {
        log::warn("vhd", L"the image volume could not be locked (a handle is open); flushed and detached all the same");
    }
    CloseHandle(volume);
    return locked;
}

// WinLove's own changes disk, opened the way that also answers while it is attached (by this or
// any other process). Lab 2026-11-11: on an attached disk a v1 open asking GET_INFO (or DETACH)
// alone is refused with 0x80070005 — the disk then read "not attached" and a merge ran on a disk
// that was still attached — while one that also asks ATTACH_RW is let through and gives the
// physical path. Never used on the user's own disk.
Result<DiskHandle> openOwnDisk(const std::filesystem::path& file) {
    auto disk = openDiskV1(file, VIRTUAL_DISK_ACCESS_ATTACH_RW | VIRTUAL_DISK_ACCESS_GET_INFO | VIRTUAL_DISK_ACCESS_DETACH, 1);
    if (disk) {
        return disk;
    }
    auto plain = openDiskQuery(file, VIRTUAL_DISK_ACCESS_GET_INFO | VIRTUAL_DISK_ACCESS_DETACH);
    if (plain) {
        log::info("vhd", L"opened without parameters: " + file.wstring());
        return plain;
    }
    return std::unexpected(disk.error());
}

Result<void> detachFile(const std::filesystem::path& file) {
    auto disk = openOwnDisk(file);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    if (!physicalPath(disk->get())) {
        return {}; // not attached
    }
    const DWORD status = DetachVirtualDisk(disk->get(), DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
    if (status != ERROR_SUCCESS) {
        return win32Failure(ErrorCode::IoError, L"could not detach the virtual disk", file.wstring(), status);
    }
    log::info("vhd", L"detached " + file.wstring());
    return {};
}

// An overlapped virtdisk operation (merge), with its progress.
Result<void> waitOperation(HANDLE disk, OVERLAPPED& overlapped, DWORD started, const TaskContext& task, const wchar_t* stage,
                           const std::wstring& context) {
    if (started != ERROR_IO_PENDING && started != ERROR_SUCCESS) {
        return win32Failure(ErrorCode::IoError, std::format(L"{} failed", stage), context, started);
    }
    for (;;) {
        VIRTUAL_DISK_PROGRESS progress{};
        const DWORD status = GetVirtualDiskOperationProgress(disk, &overlapped, &progress);
        if (status != ERROR_SUCCESS) {
            return win32Failure(ErrorCode::IoError, std::format(L"{}: no progress", stage), context, status);
        }
        if (progress.OperationStatus == ERROR_IO_PENDING) {
            if (progress.CompletionValue > 0) {
                task.report(static_cast<double>(progress.CurrentValue) / static_cast<double>(progress.CompletionValue), stage);
            }
            WaitForSingleObject(overlapped.hEvent, 250);
            continue;
        }
        if (progress.OperationStatus != ERROR_SUCCESS) {
            return win32Failure(ErrorCode::IoError, std::format(L"{} failed", stage), context, progress.OperationStatus);
        }
        task.report(1.0, stage);
        return {};
    }
}

// The child's blocks written into its parent (offline: the child is detached). Not cancellable.
Result<void> mergeIntoParent(const std::filesystem::path& child, const TaskContext& task) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        return fail(ErrorCode::IoError, L"no event for the merge", child.wstring());
    }
    Result<void> result;
    {
        // v1: the child and its parent opened read / write (RWDepth 2), merge depth 1.
        auto disk = openDiskV1(child, VIRTUAL_DISK_ACCESS_METAOPS | VIRTUAL_DISK_ACCESS_GET_INFO, 2);
        if (disk) {
            MERGE_VIRTUAL_DISK_PARAMETERS params{};
            params.Version = MERGE_VIRTUAL_DISK_VERSION_1;
            params.Version1.MergeDepth = 1;
            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            const DWORD started = MergeVirtualDisk(disk->get(), MERGE_VIRTUAL_DISK_FLAG_NONE, &params, &overlapped);
            result = waitOperation(disk->get(), overlapped, started, task, L"merge", child.wstring());
        } else {
            result = std::unexpected(disk.error());
        }
    }
    if (!result) {
        // v2 (source depth 1 = the child, target depth 2 = its parent), opened the v2 way.
        log::warn("vhd", L"merge (v1) failed, trying v2: " + describe(result.error()));
        VIRTUAL_STORAGE_TYPE type = storageTypeOf(child);
        OPEN_VIRTUAL_DISK_PARAMETERS open{};
        open.Version = OPEN_VIRTUAL_DISK_VERSION_2;
        DiskHandle disk;
        DWORD status = OpenVirtualDisk(&type, child.c_str(), VIRTUAL_DISK_ACCESS_NONE, OPEN_VIRTUAL_DISK_FLAG_NONE, &open, disk.out());
        if (status == ERROR_SUCCESS) {
            ResetEvent(event);
            MERGE_VIRTUAL_DISK_PARAMETERS params{};
            params.Version = MERGE_VIRTUAL_DISK_VERSION_2;
            params.Version2.MergeSourceDepth = 1;
            params.Version2.MergeTargetDepth = 2;
            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            status = MergeVirtualDisk(disk.get(), MERGE_VIRTUAL_DISK_FLAG_NONE, &params, &overlapped);
            result = waitOperation(disk.get(), overlapped, status, task, L"merge", child.wstring());
        } else {
            result = win32Failure(ErrorCode::IoError, L"cannot open the child disk for the merge", child.wstring(), status);
        }
    }
    CloseHandle(event);
    return result;
}

std::wstring regString(const OffRegKey& key, const wchar_t* name) {
    const auto data = key.bytes(name);
    std::wstring text(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return text;
}

int toInt(const std::wstring& text) {
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return value > 0 && value < 1'000'000 ? static_cast<int>(value) : 0;
}

std::wstring pathText(const std::filesystem::path& p) {
    return p.wstring();
}

std::int64_t nowSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

Result<void> removeRecord(const std::filesystem::path& folder) {
    auto records = loadVhdRecords();
    std::erase_if(records, [&](const VhdMountRecord& r) { return samePathText(r.folder, folder); });
    return saveVhdRecords(records);
}

Result<void> putRecord(const VhdMountRecord& record) {
    auto records = loadVhdRecords();
    std::erase_if(records, [&](const VhdMountRecord& r) { return samePathText(r.folder, record.folder); });
    records.push_back(record);
    return saveVhdRecords(records);
}

VhdMountCheck checkRecord(const VhdMountRecord& record) {
    VhdMountCheck check;
    check.record = record;
    const bool diskThere = fileExists(record.vhd);
    const bool childThere = fileExists(record.child);
    // Not answering counts as attached: only Ok / Remount follow from it, nothing is deleted.
    const bool attached = childThere && changesDiskAttached(record.child).value_or(true);
    const std::wstring onFolder = volumeOnFolder(record.folder);
    const bool folderOk = attached && !onFolder.empty() && !record.volume.empty() && _wcsicmp(onFolder.c_str(), record.volume.c_str()) == 0;
    check.state = classifyVhdMount(diskThere, childThere, attached, folderOk);
    check.action = recommendedVhdAction(check.state);
    check.childSize = childThere ? fileSizeOf(record.child) : 0;
    return check;
}

// Attaches the recorded child (when it is not) and puts its Windows volume on the folder (when it
// is not). The record learns the volume.
Result<VhdMountRecord> attachAndMount(VhdMountRecord record, DiskHandle* keep) {
    auto disk = openOwnDisk(record.child);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    // Attached already (only the folder lost its mount point): only asked where it is.
    if (!physicalPath(disk->get())) {
        ATTACH_VIRTUAL_DISK_PARAMETERS params{};
        params.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
        const DWORD status = AttachVirtualDisk(disk->get(), nullptr,
                                               ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER, 0,
                                               &params, nullptr);
        if (status != ERROR_SUCCESS) {
            return win32Failure(ErrorCode::IoError, L"could not attach the virtual disk", record.child.wstring(), status);
        }
    }
    auto found = waitForWindowsVolume(disk->get(), record.child, 30);
    if (!found) {
        return std::unexpected(found.error());
    }
    removeDriveLetters(found->volume.guidPath);
    if (volumeOnFolder(record.folder) != found->volume.guidPath) {
        if (auto r = putOnFolder(found->volume.guidPath, record.folder); !r) {
            return std::unexpected(r.error());
        }
    }
    record.volume = found->volume.guidPath;
    if (keep) {
        *keep = std::move(*disk);
    }
    return record;
}

SourceInfo sourceFrom(const std::filesystem::path& vhd, const OfflineWindowsFacts& facts, const VolumeCandidate& volume,
                      VirtualDiskInfo disk) {
    SourceInfo info;
    info.path = vhd;
    info.format = formatFromPath(vhd.wstring());
    info.installImage = vhd.filename().wstring();
    info.installImageSize = fileSizeOf(vhd);
    disk.fileSize = info.installImageSize;
    disk.volumeSize = volume.size;
    disk.volumeFree = volume.freeBytes;
    info.install.header.imageCount = 1;
    info.install.header.compression = WimCompression::None;
    info.install.images.push_back(imageInfoFromFacts(facts, volume.size > volume.freeBytes ? volume.size - volume.freeBytes : 0));
    info.disk = std::move(disk);
    return info;
}

} // namespace

// ---- pure ---------------------------------------------------------------------------------------

bool isVirtualDiskFormat(ImageFormat format) noexcept {
    return format == ImageFormat::Vhd || format == ImageFormat::Vhdx;
}

bool isVirtualDiskPath(const std::filesystem::path& path) noexcept {
    return isVirtualDiskFormat(formatFromPath(path.native()));
}

SourceCapabilities sourceCapabilities(const SourceInfo& source) noexcept {
    SourceCapabilities caps;
    if (isVirtualDiskFormat(source.format)) {
        caps.virtualDisk = true;
        caps.mountable = true;
        caps.wimEditions = false;
        caps.verifiable = false;
        caps.setupMedia = false;
        caps.discardable = true; // through the child disk
        return caps;
    }
    const auto& header = source.install.header;
    const bool packed = header.solid || header.compression == WimCompression::Lzms || source.format == ImageFormat::Esd ||
                        text::iendsWith(source.installImage, L".esd");
    caps.mountable = !packed;
    caps.verifiable = !packed;
    caps.wimEditions = (source.format == ImageFormat::Wim || source.format == ImageFormat::Folder || source.format == ImageFormat::Iso) &&
                       !packed && header.totalParts <= 1;
    caps.setupMedia = source.format == ImageFormat::Iso || source.format == ImageFormat::Folder;
    caps.discardable = true;
    return caps;
}

std::optional<std::size_t> chooseWindowsVolume(std::span<const VolumeCandidate> volumes) noexcept {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < volumes.size(); ++i) {
        const auto& v = volumes[i];
        if (!v.hasWindows || _wcsicmp(v.fileSystem.c_str(), L"NTFS") != 0) {
            continue;
        }
        if (!best || v.size > volumes[*best].size) {
            best = i;
        }
    }
    return best;
}

std::optional<std::uint32_t> diskNumberFromPhysicalPath(std::wstring_view path) noexcept {
    constexpr std::wstring_view kPrefix = L"\\\\.\\PhysicalDrive";
    if (path.size() <= kPrefix.size() || _wcsnicmp(path.data(), kPrefix.data(), kPrefix.size()) != 0) {
        return std::nullopt;
    }
    std::uint32_t number = 0;
    for (const wchar_t c : path.substr(kPrefix.size())) {
        if (c < L'0' || c > L'9' || number > 100000) {
            return std::nullopt;
        }
        number = number * 10 + static_cast<std::uint32_t>(c - L'0');
    }
    return number;
}

std::filesystem::path changesDiskPath(const std::filesystem::path& vhd, const std::filesystem::path& workFolder) {
    const std::wstring extension = formatFromPath(vhd.wstring()) == ImageFormat::Vhd ? L".vhd" : L".vhdx";
    return workFolder / (vhd.stem().wstring() + L".winlove-changes" + extension);
}

ImageInfo imageInfoFromFacts(const OfflineWindowsFacts& facts, std::uint64_t usedBytes) {
    ImageInfo info;
    info.index = 1;
    // The name comes from EditionID: an applied, never booted image's ProductName is still the one
    // its servicing base had (lab 2026-11-11: "Windows 10 Enterprise" for a Professional image).
    // Servers and unknown ids keep ProductName, which says 10 on Windows 11 too.
    std::wstring name;
    const bool client = facts.installationType.empty() || _wcsicmp(facts.installationType.c_str(), L"Client") == 0;
    if (client && isEditionId(facts.editionId) && facts.build > 0) {
        name = editionDisplayName(facts.editionId, facts.build);
    } else {
        name = facts.productName.empty() ? std::wstring(L"Windows") : facts.productName;
        if (facts.build >= 22000 && name.starts_with(L"Windows 10")) {
            name = L"Windows 11" + name.substr(10);
        }
    }
    info.name = name;
    info.displayName = name;
    info.description = name;
    info.displayDescription = name;
    info.editionId = facts.editionId;
    info.flags = facts.editionId;
    info.installationType = facts.installationType;
    info.major = facts.major;
    info.minor = facts.minor;
    info.build = facts.build;
    info.spBuild = facts.ubr;
    info.languages = facts.languages;
    info.defaultLanguage = facts.defaultLanguage.empty() && !facts.languages.empty() ? facts.languages.front() : facts.defaultLanguage;
    info.totalBytes = usedBytes;
    info.systemRoot = facts.systemRoot;
    info.productType = facts.installationType == L"Server" ? L"ServerNT" : L"WinNT";
    const std::wstring& arch = facts.processorArchitecture;
    info.architecture = _wcsicmp(arch.c_str(), L"AMD64") == 0   ? Architecture::X64
                        : _wcsicmp(arch.c_str(), L"ARM64") == 0 ? Architecture::Arm64
                        : _wcsicmp(arch.c_str(), L"x86") == 0   ? Architecture::X86
                        : _wcsicmp(arch.c_str(), L"ARM") == 0   ? Architecture::Arm
                                                                : Architecture::Unknown;
    return info;
}

Result<OfflineWindowsFacts> readWindowsFacts(const std::filesystem::path& windowsRoot) {
    const auto config = windowsRoot / L"Windows" / L"System32" / L"config";
    auto software = OffRegKey::openHive(config / L"SOFTWARE");
    if (!software) {
        return std::unexpected(software.error());
    }
    OfflineWindowsFacts facts;
    {
        const auto cv = software->open(L"Microsoft\\Windows NT\\CurrentVersion");
        if (!cv) {
            return fail(ErrorCode::NotFound, L"the image's SOFTWARE hive has no Windows NT\\CurrentVersion", (config / L"SOFTWARE").wstring());
        }
        facts.productName = regString(cv, L"ProductName");
        facts.editionId = regString(cv, L"EditionID");
        facts.installationType = regString(cv, L"InstallationType");
        facts.displayVersion = regString(cv, L"DisplayVersion");
        facts.build = toInt(regString(cv, L"CurrentBuildNumber"));
        if (facts.build == 0) {
            facts.build = toInt(regString(cv, L"CurrentBuild"));
        }
        facts.ubr = static_cast<int>(cv.dword(L"UBR").value_or(0));
        facts.major = static_cast<int>(cv.dword(L"CurrentMajorVersionNumber").value_or(10));
        facts.minor = static_cast<int>(cv.dword(L"CurrentMinorVersionNumber").value_or(0));
        std::wstring root = regString(cv, L"SystemRoot"); // "C:\WINDOWS"
        if (const auto slash = root.find_last_of(L'\\'); slash != std::wstring::npos) {
            root = root.substr(slash + 1);
        }
        facts.systemRoot = root.empty() ? std::wstring(L"WINDOWS") : root;
    }
    auto system = OffRegKey::openHive(config / L"SYSTEM");
    if (system) {
        const auto select = system->open(L"Select");
        const DWORD current = select ? select.dword(L"Current").value_or(1) : 1;
        const std::wstring set = std::format(L"ControlSet{:03}", current);
        if (const auto env = system->open(set + L"\\Control\\Session Manager\\Environment")) {
            facts.processorArchitecture = regString(env, L"PROCESSOR_ARCHITECTURE");
        }
        if (const auto mui = system->open(set + L"\\Control\\MUI\\UILanguages")) {
            facts.languages = mui.subkeys();
        }
        if (const auto nls = system->open(set + L"\\Control\\Nls\\Language")) {
            const std::wstring install = regString(nls, L"InstallLanguage"); // "041F"
            if (!install.empty()) {
                const LCID lcid = static_cast<LCID>(std::wcstoul(install.c_str(), nullptr, 16));
                wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
                if (lcid != 0 && LCIDToLocaleName(lcid, locale, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
                    for (const auto& l : facts.languages) {
                        if (_wcsicmp(l.c_str(), locale) == 0) {
                            facts.defaultLanguage = l;
                        }
                    }
                    if (facts.defaultLanguage.empty()) {
                        facts.defaultLanguage = locale;
                    }
                }
            }
        }
    } else {
        log::warn("vhd", L"SYSTEM hive not read: " + describe(system.error()));
    }
    std::ranges::sort(facts.languages, [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return facts;
}

// ---- records ------------------------------------------------------------------------------------

std::filesystem::path vhdRecordFile() {
    if (!g_recordFile.empty()) {
        return g_recordFile;
    }
    return log::defaultDirectory().parent_path() / L"vhd-mounts.json";
}

void setVhdRecordFile(std::filesystem::path file) {
    g_recordFile = file.empty() ? std::filesystem::path() : nativePath(file);
}

std::vector<VhdMountRecord> parseVhdRecords(std::string_view text) {
    std::vector<VhdMountRecord> out;
    const auto doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", "") != "winlove.vhd-mounts") {
        return out;
    }
    const auto it = doc.find("mounts");
    if (it == doc.end() || !it->is_array()) {
        return out;
    }
    auto str = [](const json& e, const char* key) {
        const auto v = e.find(key);
        return v != e.end() && v->is_string() ? utf8::toWide(v->get<std::string>()) : std::wstring();
    };
    for (const auto& e : *it) {
        if (!e.is_object()) {
            continue;
        }
        VhdMountRecord r;
        r.vhd = str(e, "vhd");
        r.child = str(e, "child");
        r.folder = str(e, "folder");
        r.volume = str(e, "volume");
        r.by = str(e, "by");
        if (const auto t = e.find("attachedAt"); t != e.end() && t->is_number_integer()) {
            r.attachedAt = t->get<std::int64_t>();
        }
        // A record that cannot name what it attached is of no use (and never acted on).
        if (r.vhd.empty() || r.child.empty() || r.folder.empty() || !r.vhd.is_absolute() || !r.child.is_absolute() ||
            !r.folder.is_absolute() || !isVirtualDiskPath(r.vhd) || !isVirtualDiskPath(r.child)) {
            continue;
        }
        out.push_back(std::move(r));
    }
    return out;
}

std::string serializeVhdRecords(const std::vector<VhdMountRecord>& records) {
    json mounts = json::array();
    for (const auto& r : records) {
        mounts.push_back({{"vhd", utf8::fromWide(pathText(r.vhd))},
                          {"child", utf8::fromWide(pathText(r.child))},
                          {"folder", utf8::fromWide(pathText(r.folder))},
                          {"volume", utf8::fromWide(r.volume)},
                          {"attachedAt", r.attachedAt},
                          {"by", utf8::fromWide(r.by)}});
    }
    json doc{{"format", "winlove.vhd-mounts"}, {"version", 1}, {"mounts", mounts}};
    return doc.dump(2) + "\n";
}

std::vector<VhdMountRecord> loadVhdRecords() {
    const auto bytes = readFileBytes(vhdRecordFile());
    if (!bytes) {
        return {};
    }
    return parseVhdRecords(*bytes);
}

Result<void> saveVhdRecords(const std::vector<VhdMountRecord>& records) {
    const auto file = vhdRecordFile();
    std::error_code ec;
    if (records.empty()) {
        std::filesystem::remove(file, ec); // nothing attached: no file left behind
        return {};
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    return writeFileAtomic(file, serializeVhdRecords(records));
}

std::optional<VhdMountRecord> vhdRecordForFolder(const std::filesystem::path& folder) {
    for (auto& r : loadVhdRecords()) {
        if (samePathText(r.folder, folder)) {
            return r;
        }
    }
    return std::nullopt;
}

std::optional<VhdMountRecord> vhdRecordForDisk(const std::filesystem::path& vhd) {
    for (auto& r : loadVhdRecords()) {
        if (samePathText(r.vhd, vhd)) {
            return r;
        }
    }
    return std::nullopt;
}

const wchar_t* vhdMountStateName(VhdMountState state) noexcept {
    switch (state) {
    case VhdMountState::Ok: return L"ok";
    case VhdMountState::NotAttached: return L"not attached";
    case VhdMountState::NoMountPoint: return L"no mount point";
    case VhdMountState::ChildMissing: return L"changes disk missing";
    case VhdMountState::DiskMissing: return L"disk missing";
    }
    return L"?";
}

const wchar_t* vhdActionName(VhdAction action) noexcept {
    switch (action) {
    case VhdAction::None: return L"none";
    case VhdAction::Remount: return L"remount";
    case VhdAction::Forget: return L"forget";
    }
    return L"?";
}

VhdMountState classifyVhdMount(bool diskExists, bool childExists, bool attached, bool folderOnVolume) noexcept {
    if (!childExists) {
        return VhdMountState::ChildMissing;
    }
    if (!diskExists) {
        return VhdMountState::DiskMissing;
    }
    if (!attached) {
        return VhdMountState::NotAttached;
    }
    return folderOnVolume ? VhdMountState::Ok : VhdMountState::NoMountPoint;
}

VhdAction recommendedVhdAction(VhdMountState state) noexcept {
    switch (state) {
    case VhdMountState::Ok:
    case VhdMountState::DiskMissing: return VhdAction::None; // nothing is deleted while the user's disk is away
    case VhdMountState::NotAttached:
    case VhdMountState::NoMountPoint: return VhdAction::Remount;
    case VhdMountState::ChildMissing: return VhdAction::Forget;
    }
    return VhdAction::None;
}

std::vector<VhdMountCheck> inspectVhdMounts() {
    std::vector<VhdMountCheck> out;
    for (const auto& r : loadVhdRecords()) {
        out.push_back(checkRecord(r));
    }
    return out;
}

std::optional<VhdMountCheck> inspectVhdMount(const std::filesystem::path& folder) {
    if (auto r = vhdRecordForFolder(folder)) {
        return checkRecord(*r);
    }
    return std::nullopt;
}

std::optional<bool> changesDiskAttached(const std::filesystem::path& file) {
    if (!fileExists(file)) {
        return false;
    }
    auto disk = openOwnDisk(file);
    if (!disk) {
        return std::nullopt;
    }
    return physicalPath(disk->get()).has_value();
}

// ---- operations ---------------------------------------------------------------------------------

Result<SourceInfo> openVirtualDiskSource(const std::filesystem::path& input) {
    const std::filesystem::path vhd = nativePath(input);
    if (!fileExists(vhd)) {
        return fail(ErrorCode::NotFound, L"virtual disk not found", vhd.wstring());
    }
    // Mounted by WinLove: read through its folder (the file itself is in use as the parent then).
    if (const auto record = vhdRecordForDisk(vhd)) {
        const auto check = checkRecord(*record);
        if (check.state == VhdMountState::Ok) {
            auto facts = readWindowsFacts(record->folder);
            if (!facts) {
                return std::unexpected(facts.error());
            }
            VolumeCandidate volume;
            volume.guidPath = record->volume;
            ULARGE_INTEGER freeCaller{}, total{}, freeTotal{};
            if (GetDiskFreeSpaceExW(record->volume.c_str(), &freeCaller, &total, &freeTotal)) {
                volume.size = total.QuadPart;
                volume.freeBytes = freeTotal.QuadPart;
            }
            VirtualDiskInfo disk;
            if (auto handle = openDiskQuery(vhd, VIRTUAL_DISK_ACCESS_GET_INFO)) {
                readDiskInfo(handle->get(), disk);
            }
            disk.volumeLabel = volumeLabelOf(record->volume);
            disk.volumes = 1;
            log::info("vhd", L"opened (mounted) " + vhd.wstring());
            return sourceFrom(vhd, *facts, volume, std::move(disk));
        }
        return fail(ErrorCode::InvalidArgument,
                    std::format(L"WinLove's mount of this disk needs attention first ({})", vhdMountStateName(check.state)),
                    vhd.wstring());
    }
    if (!isElevated()) {
        return fail(ErrorCode::AccessDenied, L"reading a virtual disk needs an elevated (administrator) process: it is attached",
                    vhd.wstring());
    }
    const auto started = std::chrono::steady_clock::now();
    auto disk = openDiskReadOnly(vhd);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    VirtualDiskInfo info;
    readDiskInfo(disk->get(), info);
    if (physicalPath(disk->get())) {
        return fail(ErrorCode::InvalidArgument, L"the virtual disk is attached already (by Windows or another program): detach it first",
                    vhd.wstring());
    }
    ATTACH_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
    // Not PERMANENT_LIFETIME: the disk goes away with this handle (and with the process, whatever happens).
    const DWORD status = AttachVirtualDisk(disk->get(), nullptr, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER,
                                           0, &params, nullptr);
    if (status != ERROR_SUCCESS) {
        return win32Failure(ErrorCode::IoError, L"could not attach the virtual disk (read only)", vhd.wstring(), status);
    }
    Result<SourceInfo> result = fail(ErrorCode::Unknown, L"not read", vhd.wstring());
    if (auto found = waitForWindowsVolume(disk->get(), vhd, 30); !found) {
        result = std::unexpected(found.error());
    } else {
        removeDriveLetters(found->volume.guidPath);
        auto facts = readWindowsFacts(found->volume.guidPath);
        if (!facts) {
            result = std::unexpected(facts.error());
        } else {
            info.volumeLabel = volumeLabelOf(found->volume.guidPath);
            info.volumes = found->count;
            result = sourceFrom(vhd, *facts, found->volume, std::move(info));
        }
    }
    const DWORD detached = DetachVirtualDisk(disk->get(), DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
    if (detached != ERROR_SUCCESS) {
        log::warn("vhd", std::format(L"read-only detach answered 0x{:08X}; the handle's close detaches it",
                                     static_cast<unsigned>(HRESULT_FROM_WIN32(detached))));
    }
    disk->reset();
    log::info("vhd", std::format(L"read {} in {} ms", vhd.wstring(),
                                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
    return result;
}

Result<VhdMountRecord> mountVirtualDisk(const std::filesystem::path& vhdInput, const std::filesystem::path& folderInput,
                                        const std::filesystem::path& workFolderInput, std::wstring_view by, const TaskContext& task) {
    if (!isElevated()) {
        return fail(ErrorCode::AccessDenied, L"attaching a virtual disk needs an elevated (administrator) process", vhdInput.wstring());
    }
    const std::filesystem::path vhd = nativePath(vhdInput);
    const std::filesystem::path folder = nativePath(folderInput);
    const std::filesystem::path workFolder = nativePath(workFolderInput);
    if (!isVirtualDiskPath(vhd) || !fileExists(vhd)) {
        return fail(ErrorCode::NotFound, L"not a .vhd / .vhdx file", vhd.wstring());
    }
    // Already WinLove's: the same disk on the same folder is used as it is (after a restart).
    if (const auto byFolder = vhdRecordForFolder(folder)) {
        if (!samePathText(byFolder->vhd, vhd)) {
            return fail(ErrorCode::InvalidArgument, L"another virtual disk is mounted on this folder", folder.wstring() + L" holds " + byFolder->vhd.wstring());
        }
        const auto check = checkRecord(*byFolder);
        if (check.state == VhdMountState::Ok) {
            return *byFolder;
        }
        if (check.action == VhdAction::Remount) {
            auto repaired = repairVirtualDisk(check, task);
            if (!repaired) {
                return std::unexpected(repaired.error());
            }
            return repaired->record;
        }
        return fail(ErrorCode::InvalidArgument, std::format(L"WinLove's earlier mount of this disk is {}", vhdMountStateName(check.state)),
                    folder.wstring());
    }
    if (const auto byDisk = vhdRecordForDisk(vhd)) {
        return fail(ErrorCode::InvalidArgument, L"this virtual disk is mounted by WinLove on another folder", byDisk->folder.wstring());
    }
    const std::filesystem::path child = changesDiskPath(vhd, workFolder);
    if (fileExists(child)) {
        // Not recorded: not known to be WinLove's work in progress — never deleted or reused blindly.
        return fail(ErrorCode::InvalidArgument,
                    L"a changes disk of an earlier mount is in the way and no record names it; move or delete it by hand", child.wstring());
    }
    // Attached by Windows or another program already? (Read only; when the question cannot be
    // asked, attaching the changes disk fails on its own: the parent is then in use.)
    if (auto parent = openDiskQuery(vhd, VIRTUAL_DISK_ACCESS_GET_INFO); parent && physicalPath(parent->get())) {
        return fail(ErrorCode::InvalidArgument, L"the virtual disk is attached already (by Windows or another program): detach it first",
                    vhd.wstring());
    }
    if (auto r = prepareFolder(folder); !r) {
        return std::unexpected(r.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(workFolder, ec);
    VhdMountRecord record{vhd, child, folder, L"", nowSeconds(), std::wstring(by)};
    // Recorded before anything exists: whatever happens next, the next start knows what is WinLove's.
    if (auto r = putRecord(record); !r) {
        return std::unexpected(r.error());
    }
    auto rollback = [&](const Error& error) -> Result<VhdMountRecord> {
        log::warn("vhd", L"mount failed, undoing: " + describe(error));
        takeOffFolder(folder, record.volume);
        if (fileExists(child)) {
            if (auto r = detachFile(child); !r) {
                log::error("vhd", L"the changes disk stays attached (recorded): " + describe(r.error()));
                return std::unexpected(error);
            }
            DeleteFileW(child.c_str());
        }
        (void)removeRecord(folder);
        return std::unexpected(error);
    };
    task.report(0.1, L"create");
    {
        VIRTUAL_STORAGE_TYPE childType = storageTypeOf(child);
        VIRTUAL_STORAGE_TYPE parentType = storageTypeOf(vhd);
        CREATE_VIRTUAL_DISK_PARAMETERS params{};
        params.Version = CREATE_VIRTUAL_DISK_VERSION_2;
        params.Version2.ParentPath = vhd.c_str();
        params.Version2.ParentVirtualStorageType = parentType;
        DiskHandle created;
        const DWORD status = CreateVirtualDisk(&childType, child.c_str(), VIRTUAL_DISK_ACCESS_NONE, nullptr, CREATE_VIRTUAL_DISK_FLAG_NONE, 0,
                                               &params, nullptr, created.out());
        if (status != ERROR_SUCCESS) {
            auto e = win32Failure(ErrorCode::IoError, L"could not create the changes disk", child.wstring(), status);
            return rollback(e.error());
        }
        log::info("vhd", L"changes disk created: " + child.wstring() + L" (parent " + vhd.wstring() + L")");
    }
    task.report(0.3, L"attach");
    auto mounted = attachAndMount(record, nullptr);
    if (!mounted) {
        return rollback(mounted.error());
    }
    record = *mounted;
    if (auto r = putRecord(record); !r) {
        log::error("vhd", L"mounted but the record could not be updated: " + describe(r.error()));
    }
    task.report(1.0, L"mounted");
    log::info("vhd", std::format(L"mounted {} on {} through {}", vhd.wstring(), folder.wstring(), child.wstring()));
    return record;
}

Result<VhdUnmountOutcome> unmountVirtualDisk(const std::filesystem::path& folderInput, bool commit, const TaskContext& task) {
    const std::filesystem::path folder = nativePath(folderInput);
    const auto record = vhdRecordForFolder(folder);
    if (!record) {
        return fail(ErrorCode::NotFound, L"no virtual disk of WinLove is mounted on this folder", folder.wstring());
    }
    if (!fileExists(record->vhd) && commit) {
        return fail(ErrorCode::NotFound, L"the virtual disk is gone: the changes cannot be saved into it (they stay in the changes disk)",
                    record->vhd.wstring());
    }
    VhdUnmountOutcome outcome;
    const auto attachedNow = changesDiskAttached(record->child);
    if (!attachedNow) {
        // Without a sure answer nothing is detached, merged or deleted (lab 2026-11-11: a wrong
        // "not attached" let a merge run on an attached disk).
        return fail(ErrorCode::IoError, L"WinLove could not tell whether its changes disk is attached; nothing was done",
                    record->child.wstring());
    }
    const bool attached = *attachedNow;
    if (attached) {
        const std::wstring volume = !record->volume.empty() ? record->volume : volumeOnFolder(folder);
        releaseExplorerWindows(folder);
        // Hives of the image still loaded (a crash in a registry step): unloaded, by the volume's
        // device path (hivelist names the volume, not the folder) and by the folder's.
        if (!volume.empty()) {
            if (const std::wstring device = volumeDevice(volume); !device.empty()) {
                if (auto r = unloadHives(hivesLoadedFromDevice(device + L"\\")); !r) {
                    return std::unexpected(r.error());
                }
            }
        }
        if (auto r = unloadHivesUnder(folder); !r) {
            return std::unexpected(r.error());
        }
        task.report(0.05, L"detach");
        takeOffFolder(folder, volume);
        if (!volume.empty()) {
            outcome.locked = lockAndDismount(volume);
        }
        if (auto r = detachFile(record->child); !r) {
            return std::unexpected(r.error());
        }
        // Detached for sure before anything is merged or deleted.
        const auto after = changesDiskAttached(record->child);
        if (!after || *after) {
            return fail(ErrorCode::IoError, L"the changes disk is still attached after the detach; nothing was merged or deleted",
                        record->child.wstring());
        }
    } else {
        takeOffFolder(folder, record->volume);
    }
    outcome.changeBytes = fileExists(record->child) ? fileSizeOf(record->child) : 0;
    if (commit && fileExists(record->child)) {
        const TaskContext mergeTask{CancelToken{}, [&](double f, std::wstring_view stage) { task.report(0.1 + 0.85 * f, stage); }};
        const auto started = std::chrono::steady_clock::now();
        if (auto r = mergeIntoParent(record->child, mergeTask); !r) {
            // Nothing is lost: the changes stay in the child, the record stays (state "not attached").
            return std::unexpected(Error{r.error().code,
                                         L"the changes could not be merged into the virtual disk; they are kept in " + record->child.wstring() +
                                             L" (mount again, or discard): " + r.error().message,
                                         r.error().context, r.error().hresult});
        }
        outcome.merged = true;
        log::info("vhd", std::format(L"merged {} into {} in {} ms", record->child.wstring(), record->vhd.wstring(),
                                     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
    } else if (!commit) {
        outcome.discarded = fileExists(record->child);
    }
    if (fileExists(record->child) && !DeleteFileW(record->child.c_str())) {
        return win32Failure(ErrorCode::IoError, L"the changes disk could not be deleted", record->child.wstring(), GetLastError());
    }
    if (auto r = removeRecord(folder); !r) {
        return std::unexpected(r.error());
    }
    task.report(1.0, commit ? L"saved" : L"discarded");
    log::info("vhd", std::format(L"unmounted {} ({}; {} bytes of changes)", folder.wstring(),
                                 outcome.merged ? L"merged" : outcome.discarded ? L"discarded" : L"nothing to save", outcome.changeBytes));
    return outcome;
}

Result<VhdMountCheck> repairVirtualDisk(const VhdMountCheck& check, const TaskContext&) {
    log::info("vhd", std::format(L"repair {}: {} -> {}", check.record.folder.wstring(), vhdMountStateName(check.state),
                                 vhdActionName(check.action)));
    switch (check.action) {
    case VhdAction::None: break;
    case VhdAction::Remount: {
        if (!isElevated()) {
            return fail(ErrorCode::AccessDenied, L"attaching a virtual disk needs an elevated (administrator) process",
                        check.record.child.wstring());
        }
        // A dangling mount point (the volume went away with a reboot) is taken off first.
        if (isReparseFolder(check.record.folder) && volumeOnFolder(check.record.folder).empty()) {
            takeOffFolder(check.record.folder, check.record.volume);
        }
        auto mounted = attachAndMount(check.record, nullptr);
        if (!mounted) {
            return std::unexpected(mounted.error());
        }
        if (auto r = putRecord(*mounted); !r) {
            return std::unexpected(r.error());
        }
        break;
    }
    case VhdAction::Forget: {
        takeOffFolder(check.record.folder, check.record.volume);
        if (auto r = removeRecord(check.record.folder); !r) {
            return std::unexpected(r.error());
        }
        VhdMountCheck gone = check;
        gone.state = VhdMountState::ChildMissing;
        gone.action = VhdAction::None;
        return gone;
    }
    }
    if (auto after = inspectVhdMount(check.record.folder)) {
        return *after;
    }
    return check;
}

} // namespace wl::core
