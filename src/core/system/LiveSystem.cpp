#include "core/system/LiveSystem.h"

#include <windows.h>

namespace wl::core {

namespace {

constexpr wchar_t kCurrentVersion[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

std::wstring readString(const wchar_t* name) {
    wchar_t buffer[256]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kCurrentVersion, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

DWORD readDword(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_LOCAL_MACHINE, kCurrentVersion, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

} // namespace

LiveSystemInfo readLiveSystem() {
    LiveSystemInfo info;
    info.productName = readString(L"ProductName");
    info.displayVersion = readString(L"DisplayVersion");
    info.build = _wtoi(readString(L"CurrentBuildNumber").c_str());
    info.ubr = static_cast<int>(readDword(L"UBR"));
    // Windows 11 kept "Windows 10" in ProductName for compatibility.
    constexpr std::wstring_view kTen = L"Windows 10";
    if (info.build >= 22000 && info.productName.starts_with(kTen)) {
        info.productName.replace(0, kTen.size(), L"Windows 11");
    }

    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    switch (system.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: info.architecture = Architecture::X64; break;
    case PROCESSOR_ARCHITECTURE_ARM64: info.architecture = Architecture::Arm64; break;
    case PROCESSOR_ARCHITECTURE_INTEL: info.architecture = Architecture::X86; break;
    case PROCESSOR_ARCHITECTURE_ARM: info.architecture = Architecture::Arm; break;
    default: break;
    }

    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    info.systemDrive = std::wstring(windows, 3); // "C:\"
    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    if (GetDiskFreeSpaceExW(info.systemDrive.c_str(), &freeBytes, &totalBytes, nullptr)) {
        info.driveFree = freeBytes.QuadPart;
        info.driveTotal = totalBytes.QuadPart;
    }
    return info;
}

} // namespace wl::core
