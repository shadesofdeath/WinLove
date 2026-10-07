#include "core/image/dism/DismErrors.h"

#include <windows.h>

#include <array>

namespace wl::core {

namespace {

struct Entry {
    std::uint32_t code;
    const wchar_t* label;
    Remedy remedy;
};

// wimgapi.dll message table (0xC142xxxx), WimProvider/DismCore (0xC151xxxx), Win32 pass-through.
constexpr std::array kCatalog{
    Entry{0xC1420101, L"read-only container for read/write mount", Remedy::CheckPermissions},
    Entry{0xC1420103, L"mount directory already exists", Remedy::RepairFolder},
    Entry{0xC1420105, L"extract request for missing file (filter interference)", Remedy::DisableScanners},
    Entry{0xC142010B, L"directory is not mounted", Remedy::RepairFolder},
    Entry{0xC142010C, L"commit of a read-only mount", Remedy::NotSupported},
    Entry{0xC1420111, L"mounted image not found", Remedy::RepairFolder},
    Entry{0xC1420112, L"file in mount directory in use", Remedy::CloseOpenFiles},
    Entry{0xC1420113, L"directory already contains a mounted image", Remedy::UnmountFirst},
    Entry{0xC1420114, L"mount directory is not empty", Remedy::RepairFolder},
    Entry{0xC1420115, L"mount directory does not exist", Remedy::RepairFolder},
    Entry{0xC1420117, L"partial unmount (files still open)", Remedy::CloseOpenFiles},
    Entry{0xC1420118, L"mounted image in use by another process", Remedy::WaitForOther},
    Entry{0xC142011C, L"not a valid mounted directory", Remedy::RepairFolder},
    Entry{0xC142011D, L"cannot commit (partially unmounted or still mounting)", Remedy::RepairFolder},
    Entry{0xC142011E, L"mount directory changed", Remedy::RepairFolder},
    Entry{0xC142011F, L"volume does not support reparse points", Remedy::UseLocalFixedDrive},
    Entry{0xC1420120, L"corrupted mount in directory", Remedy::RepairFolder},
    Entry{0xC1420122, L"WIM modified after mount (commit refused)", Remedy::RepairFolder},
    Entry{0xC1420123, L"not a valid remount target", Remedy::RepairFolder},
    Entry{0xC1420124, L"WIM modified after mount (remount refused)", Remedy::RepairFolder},
    Entry{0xC1420125, L"unmount in progress", Remedy::WaitForOther},
    Entry{0xC1420126, L"image already mounted", Remedy::UnmountFirst},
    Entry{0xC1420127, L"image already mounted read/write", Remedy::UnmountFirst},
    Entry{0xC1420130, L"split WIM cannot be mounted", Remedy::NotSupported},
    Entry{0xC1420131, L"mount to volume root", Remedy::UseLocalFixedDrive},
    Entry{0xC1420132, L"wimgapi version mismatch for mount directory", Remedy::RepairFolder},
    Entry{0xC1420134, L"mount drive not a fixed drive", Remedy::UseLocalFixedDrive},
    Entry{0xC1420135, L"mount failed: scanner/indexer accessing mount path", Remedy::DisableScanners},
    Entry{0xC142013C, L"compression does not support mounting (ESD)", Remedy::ConvertEsd},
    Entry{0xC1510111, L"no permission to mount and modify", Remedy::CheckPermissions},
    Entry{0xC1510112, L"no permission to mount", Remedy::CheckPermissions},
    Entry{0xC1510113, L"image index does not exist", Remedy::NotSupported},
    Entry{0xC1510114, L"image needs remount", Remedy::Remount},
    Entry{0xC1510115, L"mounted image invalid", Remedy::RepairFolder},
    Entry{0xC15101FD, L"image being serviced by another DISM operation", Remedy::WaitForOther},
    // Win32 errors DISM passes through from the mount folder.
    Entry{0x80070005, L"access denied in mount folder", Remedy::CloseOpenFiles},
    Entry{0x800704D3, L"mount folder stub refused (folder in use)", Remedy::CloseOpenFiles},
    Entry{0x80070020, L"sharing violation", Remedy::CloseOpenFiles},
    Entry{0x80070021, L"lock violation", Remedy::CloseOpenFiles},
    Entry{0x80070091, L"directory not empty", Remedy::RepairFolder},
    Entry{0x800700B7, L"already exists", Remedy::RepairFolder},
    Entry{0x80070070, L"disk full", Remedy::FreeDiskSpace},
    Entry{0x80070027, L"disk full", Remedy::FreeDiskSpace},
    Entry{0x800702E4, L"elevation required", Remedy::CheckPermissions},
    Entry{0x8007000B, L"image format this wimgapi cannot read", Remedy::WimLibrary},
};

std::wstring fromModule(const wchar_t* module, DWORD code) {
    HMODULE handle = LoadLibraryExW(module, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE |
                                                          LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!handle) {
        return {};
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        handle, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = length ? std::wstring(buffer, length) : std::wstring();
    LocalFree(buffer);
    FreeLibrary(handle);
    return text;
}

} // namespace

ErrorInfo explainError(std::int32_t hresult) noexcept {
    for (const auto& e : kCatalog) {
        if (static_cast<std::int32_t>(e.code) == hresult) {
            return {hresult, e.label, e.remedy, true};
        }
    }
    return {hresult, L"", Remedy::None, false};
}

const wchar_t* remedyName(Remedy remedy) noexcept {
    switch (remedy) {
    case Remedy::None: return L"none";
    case Remedy::CloseOpenFiles: return L"close open files";
    case Remedy::RepairFolder: return L"repair mount folder";
    case Remedy::Remount: return L"remount";
    case Remedy::UnmountFirst: return L"unmount first";
    case Remedy::ConvertEsd: return L"convert ESD to WIM";
    case Remedy::CheckPermissions: return L"check permissions";
    case Remedy::DisableScanners: return L"pause antivirus/indexer";
    case Remedy::UseLocalFixedDrive: return L"use a folder on a local fixed drive";
    case Remedy::WaitForOther: return L"wait for the other operation";
    case Remedy::FreeDiskSpace: return L"free disk space";
    case Remedy::NotSupported: return L"not supported";
    case Remedy::WimLibrary: return L"no wimgapi reads it";
    }
    return L"?";
}

std::wstring systemMessage(std::int32_t hresult) {
    const auto code = static_cast<DWORD>(hresult);
    std::wstring text;
    if ((code & 0xFFFF0000) == 0xC1420000) {
        text = fromModule(L"wimgapi.dll", code);
    } else if ((code & 0xFFFF0000) == 0xC1510000) {
        text = fromModule(L"Dism\\WimProvider.dll", code);
        if (text.empty()) {
            text = fromModule(L"Dism\\DismCore.dll", code);
        }
    }
    if (text.empty()) {
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                            nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        if (length) {
            text.assign(buffer, length);
        }
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

} // namespace wl::core
