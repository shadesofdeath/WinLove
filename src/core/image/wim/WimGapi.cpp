#include "core/image/wim/WimGapi.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/io/ByteSource.h"

#include <windows.h>

#include <format>
#include <mutex>

namespace wl::core {

namespace {

// Subset of wimgapi.h (ADK 10.1.26100).
constexpr DWORD kCreateNew = CREATE_NEW;
constexpr DWORD kOpenExisting = OPEN_EXISTING;
constexpr DWORD kMsgProgress = WM_APP + 0x1476 + 2; // WIM_MSG_PROGRESS: wParam = percent, lParam = ms left
constexpr DWORD kMsgSuccess = ERROR_SUCCESS;
constexpr DWORD kMsgAbort = 0xFFFFFFFF;           // WIM_MSG_ABORT_IMAGE

using CreateFileFn = HANDLE(WINAPI*)(PCWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
using SetTemporaryPathFn = BOOL(WINAPI*)(HANDLE, PCWSTR);
using LoadImageFn = HANDLE(WINAPI*)(HANDLE, DWORD);
using ExportImageFn = BOOL(WINAPI*)(HANDLE, HANDLE, DWORD);
using DeleteImageFn = BOOL(WINAPI*)(HANDLE, DWORD);
using CallbackFn = DWORD(CALLBACK*)(DWORD, WPARAM, LPARAM, PVOID);
using RegisterCallbackFn = DWORD(WINAPI*)(HANDLE, FARPROC, PVOID);
using UnregisterCallbackFn = BOOL(WINAPI*)(HANDLE, FARPROC);

struct Api {
    CreateFileFn createFile = nullptr;
    CloseHandleFn closeHandle = nullptr;
    SetTemporaryPathFn setTemporaryPath = nullptr;
    LoadImageFn loadImage = nullptr;
    ExportImageFn exportImage = nullptr;
    DeleteImageFn deleteImage = nullptr;
    RegisterCallbackFn registerCallback = nullptr;
    UnregisterCallbackFn unregisterCallback = nullptr;
};

template <class F>
bool load(HMODULE module, const char* name, F& target) {
    target = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    return target != nullptr;
}

Result<const Api*> api() {
    static Api instance;
    static Result<void> status = [] () -> Result<void> {
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        const auto path = std::filesystem::path(system) / L"wimgapi.dll";
        const HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m) {
            return fail(ErrorCode::NotFound, L"wimgapi.dll not found", path.wstring());
        }
        const bool ok = load(m, "WIMCreateFile", instance.createFile) && load(m, "WIMCloseHandle", instance.closeHandle) &&
                        load(m, "WIMSetTemporaryPath", instance.setTemporaryPath) &&
                        load(m, "WIMLoadImage", instance.loadImage) && load(m, "WIMExportImage", instance.exportImage) &&
                        load(m, "WIMDeleteImage", instance.deleteImage) &&
                        load(m, "WIMRegisterMessageCallback", instance.registerCallback) &&
                        load(m, "WIMUnregisterMessageCallback", instance.unregisterCallback);
        if (!ok) {
            return fail(ErrorCode::Unsupported, L"wimgapi.dll is missing expected entry points", path.wstring());
        }
        return {};
    }();
    if (!status) {
        return std::unexpected(status.error());
    }
    return &instance;
}

Error lastError(std::wstring context) {
    const DWORD code = GetLastError();
    return Error{ErrorCode::WimFailure, L"wimgapi call failed", std::move(context),
                 static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))};
}

// Closes a wimgapi handle at scope exit.
struct WimHandle {
    const Api* a;
    HANDLE h = nullptr;
    ~WimHandle() {
        if (h) {
            a->closeHandle(h);
        }
    }
};

DWORD compressionCode(WimCompression c) {
    switch (c) {
    case WimCompression::None: return 0;
    case WimCompression::Xpress: return 1;
    case WimCompression::Lzms: return 3;
    case WimCompression::Lzx:
    case WimCompression::Unknown: break;
    }
    return 2; // LZX: the default for install.wim
}

struct CallbackState {
    const TaskContext* task;
};

DWORD CALLBACK onMessage(DWORD message, WPARAM wParam, LPARAM, PVOID user) {
    const auto* state = static_cast<const CallbackState*>(user);
    if (state && state->task) {
        if (state->task->cancel.cancelled()) {
            return kMsgAbort;
        }
        if (message == kMsgProgress) {
            state->task->report(static_cast<double>(wParam) / 100.0, L"export");
        }
    }
    return kMsgSuccess;
}

} // namespace

Result<void> exportImage(const std::filesystem::path& sourceInput, int index, const std::filesystem::path& destinationInput,
                         WimCompression compression, const TaskContext& task) {
    const std::filesystem::path source = nativePath(sourceInput);
    const std::filesystem::path destination = nativePath(destinationInput);
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    log::info("wim", std::format(L"export {} [{}] -> {} ({})", source.wstring(), index, destination.wstring(),
                                 compressionName(compression)));

    WimHandle src{w, w->createFile(source.c_str(), GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!src.h) {
        return std::unexpected(lastError(L"open " + source.wstring()));
    }
    std::error_code ec;
    const auto tempDir = destination.parent_path().empty() ? std::filesystem::temp_directory_path()
                                                           : destination.parent_path();
    std::filesystem::create_directories(tempDir, ec);
    w->setTemporaryPath(src.h, tempDir.c_str());

    const bool exists = std::filesystem::exists(destination, ec);
    DWORD created = 0;
    WimHandle dst{w, w->createFile(destination.c_str(), GENERIC_WRITE | GENERIC_READ, exists ? kOpenExisting : kCreateNew,
                                   0, compressionCode(compression), &created)};
    if (!dst.h) {
        return std::unexpected(lastError(L"create " + destination.wstring()));
    }
    w->setTemporaryPath(dst.h, tempDir.c_str());

    WimHandle image{w, w->loadImage(src.h, static_cast<DWORD>(index))};
    if (!image.h) {
        return std::unexpected(lastError(std::format(L"load index {} of {}", index, source.wstring())));
    }
    CallbackState state{&task};
    w->registerCallback(src.h, reinterpret_cast<FARPROC>(&onMessage), &state);
    const BOOL ok = w->exportImage(image.h, dst.h, 0);
    const DWORD exportError = GetLastError();
    w->unregisterCallback(src.h, reinterpret_cast<FARPROC>(&onMessage));
    if (!ok) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"export cancelled", destination.wstring());
        }
        return fail(ErrorCode::WimFailure, L"export failed", destination.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(exportError)));
    }
    task.report(1.0, L"export");
    return {};
}

Result<void> deleteImage(const std::filesystem::path& wimInput, int index) {
    const std::filesystem::path wim = nativePath(wimInput);
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    log::info("wim", std::format(L"delete index {} from {}", index, wim.wstring()));
    WimHandle file{w, w->createFile(wim.c_str(), GENERIC_WRITE | GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!file.h) {
        return std::unexpected(lastError(L"open " + wim.wstring()));
    }
    w->setTemporaryPath(file.h, wim.parent_path().c_str());
    if (!w->deleteImage(file.h, static_cast<DWORD>(index))) {
        return std::unexpected(lastError(std::format(L"delete index {} of {}", index, wim.wstring())));
    }
    return {};
}

Result<void> optimizeWim(const std::filesystem::path& wimInput, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    WimFile info;
    {
        auto file = DiskFile::open(wim);
        if (!file) {
            return std::unexpected(file.error());
        }
        auto read = readWim(**file);
        if (!read) {
            return std::unexpected(read.error());
        }
        info = std::move(*read);
    } // the handle is closed before the file is replaced
    const auto& header = info.header;
    if (header.compression == WimCompression::Lzms || header.compression == WimCompression::Unknown || header.solid ||
        header.totalParts > 1 || header.bootIndex != 0 || info.images.empty()) {
        return {}; // an export would change what the file is
    }
    const std::filesystem::path fresh = wim.wstring() + L".new";
    std::error_code ec;
    std::filesystem::remove(fresh, ec);
    const double count = static_cast<double>(info.images.size());
    for (std::size_t i = 0; i < info.images.size(); ++i) {
        const TaskContext one{task.cancel, [&](double fraction, std::wstring_view) {
                                  task.report((static_cast<double>(i) + fraction) / count, L"optimize");
                              }};
        if (auto exported = exportImage(wim, info.images[i].index, fresh, header.compression, one); !exported) {
            std::filesystem::remove(fresh, ec);
            return std::unexpected(exported.error());
        }
    }
    const std::filesystem::path old = wim.wstring() + L".old";
    std::filesystem::remove(old, ec);
    // Swap through a rename: the original is only deleted once the new file is in its place.
    std::filesystem::rename(wim, old, ec);
    if (ec) {
        std::filesystem::remove(fresh, ec);
        return fail(ErrorCode::IoError, L"cannot replace the image file (in use?)", wim.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
    }
    std::filesystem::rename(fresh, wim, ec);
    if (ec) {
        const auto code = ec.value();
        std::filesystem::rename(old, wim, ec); // put the original back
        std::filesystem::remove(fresh, ec);
        return fail(ErrorCode::IoError, L"cannot move the rewritten image into place", wim.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(code)));
    }
    std::filesystem::remove(old, ec);
    task.report(1.0, L"optimize");
    return {};
}

} // namespace wl::core
