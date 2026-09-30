#include "core/usb/UsbMedia.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/wim/WimGapi.h"
#include "core/system/Process.h"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <fstream>
#include <set>
#include <thread>

namespace wl::core {

namespace {

struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit FileHandle(HANDLE handle) : h(handle) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    explicit operator bool() const noexcept { return h != INVALID_HANDLE_VALUE; }
};

std::wstring trimmed(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back())) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) {
        ++start;
    }
    return text.substr(start);
}

// A string of a STORAGE_DEVICE_DESCRIPTOR (ASCII at an offset; 0 = none).
std::wstring descriptorString(const std::vector<std::uint8_t>& buffer, DWORD offset) {
    if (offset == 0 || offset >= buffer.size()) {
        return {};
    }
    std::string text;
    for (std::size_t i = offset; i < buffer.size() && buffer[i] != 0; ++i) {
        text.push_back(static_cast<char>(buffer[i]));
    }
    return trimmed(utf8::toWide(text));
}

// Disk numbers a volume ("\\?\Volume{…}\") lies on.
std::vector<int> volumeDisks(const std::wstring& volume) {
    std::wstring path = volume;
    if (!path.empty() && path.back() == L'\\') {
        path.pop_back();
    }
    FileHandle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    std::vector<int> disks;
    if (!h) {
        return disks;
    }
    std::vector<std::uint8_t> buffer(sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT));
    DWORD returned = 0;
    if (DeviceIoControl(h.h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
        for (DWORD i = 0; i < extents->NumberOfDiskExtents && i < 16; ++i) {
            disks.push_back(static_cast<int>(extents->Extents[i].DiskNumber));
        }
    }
    return disks;
}

struct VolumeInfo {
    std::wstring guidPath; // "\\?\Volume{…}\" (with the trailing backslash)
    std::vector<int> disks;
    std::vector<std::wstring> letters;
};

std::vector<VolumeInfo> volumes() {
    std::vector<VolumeInfo> out;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        VolumeInfo v;
        v.guidPath = name;
        v.disks = volumeDisks(v.guidPath);
        wchar_t paths[1024] = {};
        DWORD length = 0;
        if (GetVolumePathNamesForVolumeNameW(name, paths, static_cast<DWORD>(std::size(paths)), &length)) {
            for (const wchar_t* p = paths; *p; p += wcslen(p) + 1) {
                v.letters.emplace_back(p);
            }
        }
        out.push_back(std::move(v));
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    return out;
}

// Disks the running Windows needs: the Windows, system and boot volumes and every volume with
// a page file are on them.
std::set<int> systemDisks(const std::vector<VolumeInfo>& all) {
    std::set<std::wstring> roots;
    wchar_t buffer[MAX_PATH];
    auto addRoot = [&](const wchar_t* path) {
        wchar_t root[MAX_PATH];
        if (GetVolumePathNameW(path, root, MAX_PATH)) {
            std::wstring r = root;
            std::ranges::transform(r, r.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
            roots.insert(r);
        }
    };
    if (GetWindowsDirectoryW(buffer, MAX_PATH)) {
        addRoot(buffer);
    }
    if (GetSystemDirectoryW(buffer, MAX_PATH)) {
        addRoot(buffer);
    }
    if (GetEnvironmentVariableW(L"SystemDrive", buffer, MAX_PATH)) {
        addRoot((std::wstring(buffer) + L"\\").c_str());
    }
    // Page files: HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", 0,
                      KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t data[2048] = {};
        DWORD size = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ExistingPageFiles", nullptr, &type, reinterpret_cast<BYTE*>(data), &size) ==
                ERROR_SUCCESS &&
            type == REG_MULTI_SZ) {
            for (const wchar_t* p = data; *p; p += wcslen(p) + 1) {
                std::wstring file = p; // "\??\C:\pagefile.sys"
                if (file.starts_with(L"\\??\\")) {
                    file = file.substr(4);
                }
                addRoot(file.c_str());
            }
        }
        RegCloseKey(key);
    }
    std::set<int> disks;
    for (const auto& v : all) {
        for (const auto& letter : v.letters) {
            std::wstring l = letter;
            std::ranges::transform(l, l.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
            if (roots.contains(l)) {
                disks.insert(v.disks.begin(), v.disks.end());
            }
        }
    }
    return disks;
}

bool offeredBus(std::uint32_t bus, bool includeVirtual) {
    if (bus == BusTypeUsb || bus == BusTypeSd || bus == BusTypeMmc) {
        return true;
    }
    return includeVirtual && (bus == BusTypeFileBackedVirtual || bus == BusTypeVirtual);
}

std::wstring upper(std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}

bool samePath(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

std::wstring oemText(std::string_view bytes) {
    if (bytes.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), n);
    return out;
}

// Runs a tool; its output (OEM code page, localised) goes to the log line by line.
Result<std::uint32_t> runLogged(const std::wstring& commandLine, std::wstring& tail) {
    log::info("usb", L"run: " + commandLine);
    std::string output;
    auto exit = runProcess(commandLine, [&](std::string_view chunk) {
        output.append(chunk);
        if (output.size() > 65536) {
            output.erase(0, output.size() - 32768);
        }
    });
    const std::wstring text = oemText(output);
    std::size_t start = 0;
    tail.clear();
    while (start < text.size()) {
        std::size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = trimmed(text.substr(start, end - start));
        if (!line.empty()) {
            log::info("usb", L"  " + line);
            tail = line; // the last thing it said
        }
        start = end + 1;
    }
    return exit;
}

} // namespace

std::wstring UsbDisk::name() const {
    std::wstring text = trimmed(vendor + L" " + model);
    return text.empty() ? std::format(L"Disk {}", number) : text;
}

std::wstring UsbDisk::identity() const {
    return std::format(L"{}|{}|{}|{}|{}", busType, vendor, model, serial, size);
}

namespace {

std::vector<UsbDisk> listDisks(bool includeVirtual, bool everything) {
    const auto all = volumes();
    const auto system = systemDisks(all);
    std::vector<UsbDisk> disks;
    for (int n = 0; n < 64; ++n) {
        const std::wstring device = std::format(L"\\\\.\\PhysicalDrive{}", n);
        FileHandle h(CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h) {
            continue;
        }
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        std::vector<std::uint8_t> buffer(4096);
        DWORD returned = 0;
        if (!DeviceIoControl(h.h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &returned, nullptr) ||
            returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            continue;
        }
        buffer.resize(returned);
        const auto* d = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        UsbDisk disk;
        disk.number = n;
        disk.busType = static_cast<std::uint32_t>(d->BusType);
        disk.removableMedia = d->RemovableMedia != FALSE;
        disk.vendor = descriptorString(buffer, d->VendorIdOffset);
        disk.model = descriptorString(buffer, d->ProductIdOffset);
        disk.serial = descriptorString(buffer, d->SerialNumberOffset);
        if (!everything && !offeredBus(disk.busType, includeVirtual)) {
            continue;
        }
        DISK_GEOMETRY_EX geometry{};
        if (DeviceIoControl(h.h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geometry, sizeof(geometry), &returned,
                            nullptr)) {
            disk.size = static_cast<std::uint64_t>(geometry.DiskSize.QuadPart);
        }
        if (disk.size == 0) {
            continue; // a card reader without a card
        }
        disk.system = system.contains(n);
        for (const auto& v : all) {
            if (std::ranges::find(v.disks, n) != v.disks.end()) {
                disk.letters.insert(disk.letters.end(), v.letters.begin(), v.letters.end());
            }
        }
        if (everything || !disk.system) {
            disks.push_back(std::move(disk));
        }
    }
    return disks;
}

} // namespace

std::vector<UsbDisk> listUsbDisks(bool includeVirtual) {
    return listDisks(includeVirtual, false);
}

std::vector<UsbDisk> listAllDisks() {
    return listDisks(true, true);
}

std::wstring fatLabel(std::wstring_view label) {
    std::wstring out;
    for (wchar_t c : label) {
        switch (c) {
        case L'ç': case L'Ç': c = L'C'; break;
        case L'ğ': case L'Ğ': c = L'G'; break;
        case L'ı': case L'İ': c = L'I'; break;
        case L'ö': case L'Ö': c = L'O'; break;
        case L'ş': case L'Ş': c = L'S'; break;
        case L'ü': case L'Ü': c = L'U'; break;
        default: c = static_cast<wchar_t>(std::towupper(c)); break;
        }
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-') {
            out.push_back(c);
        } else if (c == L' ' && !out.empty() && out.back() != L'_') {
            out.push_back(L'_');
        }
        if (out.size() == 11) {
            break;
        }
    }
    while (!out.empty() && out.back() == L'_') {
        out.pop_back();
    }
    return out.empty() ? std::wstring(L"WINLOVE") : out;
}

std::uint64_t usbPartitionMb(std::uint64_t diskBytes) {
    return diskBytes > kFat32FormatLimit ? kFat32FormatLimit / (1024 * 1024) : 0;
}

std::string diskpartScript(int disk, UsbScheme scheme, std::uint64_t partitionMb, std::wstring_view label) {
    std::string s = std::format("select disk {}\r\n", disk);
    s += "attributes disk clear readonly noerr\r\n";
    s += "clean\r\n";
    s += scheme == UsbScheme::GptUefi ? "convert gpt\r\n" : "convert mbr\r\n";
    s += partitionMb ? std::format("create partition primary size={}\r\n", partitionMb) : "create partition primary\r\n";
    s += std::format("format fs=fat32 quick label=\"{}\"\r\n", utf8::fromWide(fatLabel(label)));
    if (scheme == UsbScheme::MbrBiosUefi) {
        s += "active\r\n";
    }
    s += "assign\r\n";
    s += "exit\r\n";
    return s;
}

Result<UsbPlan> planUsbCopy(const UsbOptions& options) {
    const auto& folder = options.sourceFolder;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::NotFound, L"the setup folder is not there", folder.wstring());
    }
    if (!std::filesystem::exists(folder / L"sources", ec) ||
        (!std::filesystem::exists(folder / L"efi", ec) && !std::filesystem::exists(folder / L"bootmgr", ec))) {
        return fail(ErrorCode::InvalidArgument, L"not a Windows setup folder (sources, efi / bootmgr)", folder.wstring());
    }
    UsbPlan plan;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::end(it);
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::wstring relative = std::filesystem::relative(it->path(), folder, ec).wstring();
        std::uint64_t size = it->file_size(ec);
        const auto replaced = std::ranges::find_if(options.replacedFiles, [&](const IsoOptions::ReplacedFile& r) {
            return samePath(r.path, relative);
        });
        if (replaced != options.replacedFiles.end()) {
            size = std::filesystem::file_size(replaced->file, ec);
        }
        if (std::ranges::any_of(options.rootFiles, [&](const IsoOptions::RootFile& r) { return samePath(r.name, relative); })) {
            continue;
        }
        if (size > kFat32FileLimit) {
            if (samePath(relative, L"sources\\install.wim")) {
                plan.splitInstall = true;
                plan.installWim = it->path();
            } else if (samePath(relative, L"sources\\install.esd")) {
                return fail(ErrorCode::Unsupported,
                            L"install.esd is larger than 4 GB and cannot be split for FAT32 (repack it as WIM on the ISO tab)",
                            it->path().wstring());
            } else {
                return fail(ErrorCode::Unsupported, L"a file is larger than FAT32 allows (4 GB)", it->path().wstring());
            }
        }
        plan.bytes += size;
    }
    if (ec) {
        return fail(ErrorCode::IoError, L"could not read the setup folder", folder.wstring(), ec.value());
    }
    for (const auto& r : options.rootFiles) {
        plan.bytes += r.content.size();
    }
    return plan;
}

namespace {

struct CopyProgress {
    const TaskContext* task;
    std::uint64_t base;
    std::uint64_t total;
    double from;
    double span;
};

DWORD CALLBACK onCopyProgress(LARGE_INTEGER, LARGE_INTEGER transferred, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
                              HANDLE, HANDLE, LPVOID data) {
    const auto* p = static_cast<const CopyProgress*>(data);
    if (p->task->cancel.cancelled()) {
        return PROGRESS_CANCEL;
    }
    const double done = static_cast<double>(p->base + static_cast<std::uint64_t>(transferred.QuadPart));
    p->task->report(p->from + p->span * std::min(done / static_cast<double>(std::max<std::uint64_t>(p->total, 1)), 1.0),
                    L"copy");
    return PROGRESS_CONTINUE;
}

// The volume diskpart just made on `disk`, with its drive letter.
Result<std::wstring> findNewVolume(int disk, const CancelToken& cancel) {
    for (int attempt = 0; attempt < 60; ++attempt) {
        for (const auto& v : volumes()) {
            if (std::ranges::find(v.disks, disk) != v.disks.end() && !v.letters.empty()) {
                return v.letters.front();
            }
        }
        if (cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"cancelled");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return fail(ErrorCode::NotFound, L"the new partition did not show up with a drive letter",
                std::format(L"disk {}", disk));
}

} // namespace

Result<UsbResult> writeUsb(const UsbOptions& options, const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    auto plan = planUsbCopy(options);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    // The disk must still be the one that was picked: same bus, model, serial and size.
    const auto disks = listUsbDisks(options.allowVirtual);
    const auto disk = std::ranges::find(disks, options.disk, &UsbDisk::number);
    if (disk == disks.end() || disk->identity() != options.identity) {
        return fail(ErrorCode::InvalidArgument, L"the USB disk is not the one that was picked any more (unplugged or swapped)",
                    std::format(L"disk {}", options.disk));
    }
    const std::uint64_t partitionMb = usbPartitionMb(disk->size);
    const std::uint64_t room = partitionMb ? partitionMb * 1024 * 1024 : disk->size;
    if (plan->bytes + (64ull << 20) > room) {
        return fail(ErrorCode::InvalidArgument,
                    std::format(L"the setup files ({} MB) do not fit on the disk ({} MB)", plan->bytes >> 20, room >> 20),
                    disk->name());
    }
    log::info("usb", std::format(L"writing {} ({} MB, disk {}) from {}: {} MB, {}", disk->name(), disk->size >> 20,
                                 disk->number, options.sourceFolder.wstring(), plan->bytes >> 20,
                                 options.scheme == UsbScheme::GptUefi ? L"GPT, UEFI" : L"MBR, BIOS + UEFI"));
    if (auto r = task.cancel.check(L"usb"); !r) {
        return std::unexpected(r.error());
    }

    // 1. Partition + format (diskpart; a script file in %TEMP%, removed afterwards).
    task.report(0.0, L"format");
    auto diskpart = systemTool(L"diskpart.exe");
    if (!diskpart) {
        return std::unexpected(diskpart.error());
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path script = std::filesystem::path(temp) / std::format(L"winlove-usb-{}.txt", GetCurrentProcessId());
    {
        std::ofstream out(script, std::ios::binary | std::ios::trunc);
        out << diskpartScript(disk->number, options.scheme, partitionMb, options.label);
        if (!out) {
            return fail(ErrorCode::IoError, L"could not write the diskpart script", script.wstring());
        }
    }
    std::wstring said;
    auto exit = runLogged(std::format(L"\"{}\" /s \"{}\"", *diskpart, script.wstring()), said);
    std::error_code ec;
    std::filesystem::remove(script, ec);
    if (!exit) {
        return std::unexpected(exit.error());
    }
    if (*exit != 0) {
        return fail(ErrorCode::IoError, L"diskpart could not prepare the disk: " + said,
                    std::format(L"disk {} (exit {})", disk->number, *exit));
    }
    auto root = findNewVolume(disk->number, task.cancel);
    if (!root) {
        return std::unexpected(root.error());
    }
    log::info("usb", L"new FAT32 volume: " + *root);
    task.report(0.03, L"format");

    // 2. BIOS boot code (MBR + boot sector for BOOTMGR) with the media's own bootsect.
    if (options.scheme == UsbScheme::MbrBiosUefi) {
        const auto bootsect = options.sourceFolder / L"boot" / L"bootsect.exe";
        if (std::filesystem::exists(bootsect, ec)) {
            std::wstring letter = *root;
            while (!letter.empty() && letter.back() == L'\\') {
                letter.pop_back();
            }
            auto run = runLogged(std::format(L"\"{}\" /nt60 {} /force /mbr", bootsect.wstring(), letter), said);
            if (!run || *run != 0) {
                return fail(ErrorCode::IoError, L"bootsect could not write the BIOS boot code: " + said, bootsect.wstring());
            }
        } else {
            log::warn("usb", L"no boot\\bootsect.exe on the media: the format's boot code is used as it is");
        }
    }

    // 3. Files. Progress 0.05 … 1 by bytes; the split install.wim counts with its size.
    UsbResult result;
    result.root = *root;
    const std::filesystem::path target = *root;
    std::uint64_t done = 0;
    const std::uint64_t total = plan->bytes;
    auto copyOne = [&](const std::filesystem::path& from, const std::filesystem::path& to) -> Result<void> {
        std::filesystem::create_directories(to.parent_path(), ec);
        CopyProgress progress{&task, done, total, 0.05, 0.95};
        BOOL cancelFlag = FALSE;
        if (!CopyFileExW(from.c_str(), to.c_str(), &onCopyProgress, &progress, &cancelFlag, 0)) {
            const DWORD error = GetLastError();
            if (error == ERROR_REQUEST_ABORTED || task.cancel.cancelled()) {
                return fail(ErrorCode::Cancelled, L"copy cancelled", to.wstring());
            }
            return fail(ErrorCode::IoError, L"could not copy to the USB disk", to.wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
        }
        SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL); // read-only files of an extracted ISO
        done += std::filesystem::file_size(from, ec);
        return {};
    };
    for (auto it = std::filesystem::recursive_directory_iterator(options.sourceFolder, ec);
         !ec && it != std::filesystem::end(it); it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::wstring relative = std::filesystem::relative(it->path(), options.sourceFolder, ec).wstring();
        if (std::ranges::any_of(options.rootFiles, [&](const IsoOptions::RootFile& r) { return samePath(r.name, relative); })) {
            continue;
        }
        if (plan->splitInstall && samePath(relative, L"sources\\install.wim")) {
            continue; // below
        }
        std::filesystem::path from = it->path();
        const auto replaced = std::ranges::find_if(options.replacedFiles, [&](const IsoOptions::ReplacedFile& r) {
            return samePath(r.path, relative);
        });
        if (replaced != options.replacedFiles.end()) {
            from = replaced->file;
        }
        if (auto r = copyOne(from, target / relative); !r) {
            return std::unexpected(r.error());
        }
    }
    if (ec) {
        return fail(ErrorCode::IoError, L"could not read the setup folder", options.sourceFolder.wstring(), ec.value());
    }
    for (const auto& file : options.rootFiles) {
        std::ofstream out(target / file.name, std::ios::binary | std::ios::trunc);
        out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
        if (!out) {
            return fail(ErrorCode::IoError, L"could not write to the USB disk", (target / file.name).wstring());
        }
        done += file.content.size();
        log::info("usb", std::format(L"{} ({} bytes) written to the stick's root", file.name, file.content.size()));
    }
    if (plan->splitInstall) {
        const std::uint64_t wimSize = std::filesystem::file_size(plan->installWim, ec);
        const std::uint64_t base = done;
        const TaskContext split{task.cancel, [&](double fraction, std::wstring_view) {
                                    const double at = static_cast<double>(base) + fraction * static_cast<double>(wimSize);
                                    task.report(0.05 + 0.95 * std::min(at / static_cast<double>(std::max<std::uint64_t>(total, 1)), 1.0),
                                                L"split");
                                }};
        auto parts = splitWim(plan->installWim, target / L"sources" / L"install.swm", kSwmPartSize, split);
        if (!parts) {
            return std::unexpected(parts.error());
        }
        result.swmParts = *parts;
        done += wimSize;
        log::info("usb", std::format(L"install.wim ({} MB) split into {} part(s) for FAT32", wimSize >> 20, *parts));
    }
    result.bytes = done;

    // 4. Everything on the stick before "ready": flush the volume.
    std::wstring device = L"\\\\.\\" + *root;
    while (!device.empty() && device.back() == L'\\') {
        device.pop_back();
    }
    if (FileHandle volume(CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr));
        volume) {
        FlushFileBuffers(volume.h);
    }
    task.report(1.0, L"done");
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
    log::info("usb", std::format(L"USB stick ready: {} ({} MB, {} s)", *root, done >> 20, seconds.count()));
    return result;
}

} // namespace wl::core
