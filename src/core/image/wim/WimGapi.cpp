#include "core/image/wim/WimGapi.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Utf8.h"
#include "core/image/WimFile.h"
#include "core/io/ByteSource.h"

#include <pugixml.hpp>

#include <windows.h>

#include <algorithm>
#include <format>
#include <functional>
#include <mutex>
#include <vector>

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
using GetImageInformationFn = BOOL(WINAPI*)(HANDLE, PVOID*, PDWORD);
using SetImageInformationFn = BOOL(WINAPI*)(HANDLE, PVOID, DWORD);
using CallbackFn = DWORD(CALLBACK*)(DWORD, WPARAM, LPARAM, PVOID);
using RegisterCallbackFn = DWORD(WINAPI*)(HANDLE, FARPROC, PVOID);
using UnregisterCallbackFn = BOOL(WINAPI*)(HANDLE, FARPROC);
using SplitFileFn = BOOL(WINAPI*)(HANDLE, PCWSTR, PLARGE_INTEGER, DWORD);

struct Api {
    CreateFileFn createFile = nullptr;
    CloseHandleFn closeHandle = nullptr;
    SetTemporaryPathFn setTemporaryPath = nullptr;
    LoadImageFn loadImage = nullptr;
    ExportImageFn exportImage = nullptr;
    DeleteImageFn deleteImage = nullptr;
    GetImageInformationFn getImageInformation = nullptr;
    SetImageInformationFn setImageInformation = nullptr;
    RegisterCallbackFn registerCallback = nullptr;
    UnregisterCallbackFn unregisterCallback = nullptr;
    SplitFileFn splitFile = nullptr;
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
                        load(m, "WIMGetImageInformation", instance.getImageInformation) &&
                        load(m, "WIMSetImageInformation", instance.setImageInformation) &&
                        load(m, "WIMRegisterMessageCallback", instance.registerCallback) &&
                        load(m, "WIMUnregisterMessageCallback", instance.unregisterCallback) &&
                        load(m, "WIMSplitFile", instance.splitFile);
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

// Not in wimgapi.h: WIMCreateFile flag for solid (ESD) resources. Known from community wrappers
// and confirmed by experiment on 25H2 (2026-10-01): with it the file is a solid LZMS ESD.
constexpr DWORD kWimFlagSolid = 0x20000000;

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

namespace {
Result<void> exportImageOnce(const std::filesystem::path& sourceInput, int index, const std::filesystem::path& destinationInput,
                             WimCompression compression, const TaskContext& task);
} // namespace

Result<void> exportImage(const std::filesystem::path& source, int index, const std::filesystem::path& destination,
                         WimCompression compression, const TaskContext& task) {
    if (auto r = exportImageOnce(source, index, destination, compression, task); !r) {
        return r;
    }
    // The handles are closed: the header is final. An ESD that is not solid LZMS is the silent
    // failure this caught (uncompressed output) — never hand that on as a "compressed" image.
    if (compression == WimCompression::Lzms) {
        auto file = DiskFile::open(nativePath(destination));
        if (!file) {
            return std::unexpected(file.error());
        }
        auto header = readWimHeader(**file);
        if (!header) {
            return std::unexpected(header.error());
        }
        if (header->compression != WimCompression::Lzms || !header->solid) {
            return fail(ErrorCode::WimFailure, L"the ESD was not written as solid LZMS", destination.wstring());
        }
    }
    return {};
}

namespace {

Result<void> exportImageOnce(const std::filesystem::path& sourceInput, int index, const std::filesystem::path& destinationInput,
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
    // LZMS needs the solid flag: without it wimgapi silently writes an *uncompressed* file
    // (25H2 Pro: 13.0 GB instead of 4.87 GB). With it: one solid resource, 64 MiB LZMS chunks —
    // what dism /Export-Image /Compress:recovery writes (ENGINE.md field notes, 2026-10-01).
    const DWORD createFlags = compression == WimCompression::Lzms ? kWimFlagSolid : 0;
    WimHandle dst{w, w->createFile(destination.c_str(), GENERIC_WRITE | GENERIC_READ, exists ? kOpenExisting : kCreateNew,
                                   createFlags, compressionCode(compression), &created)};
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

} // namespace

namespace {

// Removes the entry of edition `index` in place: the streams stay in the file.
Result<void> deleteImage(const std::filesystem::path& wim, int index) {
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

// Header + XML of `wim`; the file is closed again on return (it may be replaced afterwards).
Result<WimFile> readInfo(const std::filesystem::path& wim) {
    auto file = DiskFile::open(wim);
    if (!file) {
        return std::unexpected(file.error());
    }
    return readWim(**file);
}

// A plain WIM: one part, no LZMS / solid resources (an ESD).
bool plainWim(const WimHeader& header) {
    return header.compression != WimCompression::Lzms && header.compression != WimCompression::Unknown && !header.solid &&
           header.totalParts <= 1;
}

// Exports the editions `keep` of `wim`, in that order, into a new file and puts it in the place of
// `wim`. `stage` names the progress.
Result<void> rewriteWith(const std::filesystem::path& wim, WimCompression compression, std::span<const int> keep,
                         const TaskContext& task, std::wstring_view stage) {
    const std::filesystem::path fresh = wim.wstring() + L".new";
    std::error_code ec;
    std::filesystem::remove(fresh, ec);
    const double count = static_cast<double>(keep.size());
    for (std::size_t i = 0; i < keep.size(); ++i) {
        const TaskContext one{task.cancel, [&](double fraction, std::wstring_view) {
                                  task.report((static_cast<double>(i) + fraction) / count, stage);
                              }};
        if (auto exported = exportImage(wim, keep[i], fresh, compression, one); !exported) {
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
    task.report(1.0, stage);
    return {};
}

} // namespace

Result<void> optimizeWim(const std::filesystem::path& wimInput, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    auto info = readInfo(wim);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (!plainWim(info->header) || info->header.bootIndex != 0 || info->images.empty()) {
        return {}; // an export would change what the file is
    }
    std::vector<int> keep;
    for (const auto& image : info->images) {
        keep.push_back(image.index);
    }
    return rewriteWith(wim, info->header.compression, keep, task, L"optimize");
}

Result<void> removeImages(const std::filesystem::path& wimInput, std::span<const int> indexes, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    auto info = readInfo(wim);
    if (!info) {
        return std::unexpected(info.error());
    }
    for (const int index : indexes) {
        if (std::ranges::find(info->images, index, &ImageInfo::index) == info->images.end()) {
            return fail(ErrorCode::InvalidArgument, L"no such edition in the image", std::format(L"{} [{}]", wim.wstring(), index));
        }
    }
    std::vector<int> keep;
    for (const auto& image : info->images) {
        if (std::ranges::find(indexes, image.index) == indexes.end()) {
            keep.push_back(image.index);
        }
    }
    if (keep.empty()) {
        return fail(ErrorCode::InvalidArgument, L"an image must keep at least one edition", wim.wstring());
    }
    if (keep.size() == info->images.size()) {
        return {};
    }
    if (!plainWim(info->header)) {
        return fail(ErrorCode::Unsupported, L"editions can only be removed from a plain WIM (not an ESD or a split image)",
                    wim.wstring());
    }
    log::info("wim", std::format(L"remove {} of {} editions from {}", info->images.size() - keep.size(),
                                 info->images.size(), wim.wstring()));
    if (info->header.bootIndex != 0) {
        // Highest first: removing an entry renumbers the ones after it.
        std::vector<int> doomed(indexes.begin(), indexes.end());
        std::ranges::sort(doomed, std::greater{});
        doomed.erase(std::ranges::unique(doomed).begin(), doomed.end());
        for (const int index : doomed) {
            if (task.cancel.cancelled()) {
                return fail(ErrorCode::Cancelled, L"remove cancelled", wim.wstring());
            }
            if (auto removed = deleteImage(wim, index); !removed) {
                return removed;
            }
        }
        task.report(1.0, L"remove");
        return {};
    }
    return rewriteWith(wim, info->header.compression, keep, task, L"remove");
}

Result<void> setImageText(const std::filesystem::path& wimInput, int index, const ImageText& text) {
    const std::filesystem::path wim = nativePath(wimInput);
    auto clean = [](std::wstring value) {
        std::erase_if(value, [](wchar_t c) { return c < L' '; });
        const auto first = value.find_first_not_of(L' ');
        const auto last = value.find_last_not_of(L' ');
        return first == std::wstring::npos ? std::wstring() : value.substr(first, last - first + 1);
    };
    const std::wstring name = clean(text.name);
    if (name.empty() || name.size() > 255) {
        return fail(ErrorCode::InvalidArgument, L"an edition needs a name of 1 to 255 characters", wim.wstring());
    }
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    WimHandle file{w, w->createFile(wim.c_str(), GENERIC_WRITE | GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!file.h) {
        return std::unexpected(lastError(L"open " + wim.wstring()));
    }
    w->setTemporaryPath(file.h, wim.parent_path().c_str());
    WimHandle image{w, w->loadImage(file.h, static_cast<DWORD>(index))};
    if (!image.h) {
        return std::unexpected(lastError(std::format(L"load index {} of {}", index, wim.wstring())));
    }
    // The image's own <IMAGE> element, UTF-16 with a byte order mark; wimgapi owns the numbers
    // in it (sizes, times), the texts are ours to set.
    PVOID current = nullptr;
    DWORD currentSize = 0;
    if (!w->getImageInformation(image.h, &current, &currentSize)) {
        return std::unexpected(lastError(std::format(L"read the description of index {} of {}", index, wim.wstring())));
    }
    pugi::xml_document doc;
    const auto parsed = doc.load_buffer(current, currentSize, pugi::parse_default, pugi::encoding_auto);
    LocalFree(current);
    pugi::xml_node node = doc.child("IMAGE");
    if (!parsed || !node) {
        return fail(ErrorCode::ParseError, L"the image description is not the XML expected", wim.wstring());
    }
    auto set = [&](const char* tag, const std::wstring& value) {
        pugi::xml_node child = node.child(tag);
        if (!child) {
            child = node.append_child(tag);
        }
        child.text().set(utf8::fromWide(value).c_str());
    };
    set("NAME", name);
    set("DESCRIPTION", clean(text.description));
    // What Setup's edition list shows (Windows 10 and later); older images do not have them.
    set("DISPLAYNAME", name);
    set("DISPLAYDESCRIPTION", clean(text.description));
    if (text.flags) {
        set("FLAGS", clean(*text.flags));
    }
    struct Bytes final : pugi::xml_writer {
        std::string data;
        void write(const void* chunk, std::size_t size) override { data.append(static_cast<const char*>(chunk), size); }
    } out;
    doc.save(out, "", pugi::format_raw | pugi::format_no_declaration | pugi::format_write_bom, pugi::encoding_utf16_le);
    log::info("wim", std::format(L"rename index {} of {}: {}", index, wim.wstring(), name));
    if (!w->setImageInformation(image.h, out.data.data(), static_cast<DWORD>(out.data.size()))) {
        return std::unexpected(lastError(std::format(L"write the description of index {} of {}", index, wim.wstring())));
    }
    return {};
}

std::optional<int> indexAfterRemoval(int index, std::span<const int> removed) {
    std::vector<int> gone(removed.begin(), removed.end());
    std::ranges::sort(gone);
    gone.erase(std::ranges::unique(gone).begin(), gone.end());
    if (std::ranges::binary_search(gone, index)) {
        return std::nullopt;
    }
    return index - static_cast<int>(std::ranges::lower_bound(gone, index) - gone.begin());
}

Result<int> splitWim(const std::filesystem::path& sourceInput, const std::filesystem::path& firstPartInput,
                     std::uint64_t partSize, const TaskContext& task) {
    const std::filesystem::path source = nativePath(sourceInput);
    const std::filesystem::path firstPart = nativePath(firstPartInput);
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    log::info("wim", std::format(L"split {} -> {} (parts of {} MB)", source.wstring(), firstPart.wstring(), partSize >> 20));
    WimHandle src{w, w->createFile(source.c_str(), GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!src.h) {
        return std::unexpected(lastError(L"open " + source.wstring()));
    }
    std::error_code ec;
    std::filesystem::create_directories(firstPart.parent_path(), ec);
    w->setTemporaryPath(src.h, firstPart.parent_path().c_str());
    CallbackState state{&task};
    w->registerCallback(src.h, reinterpret_cast<FARPROC>(&onMessage), &state);
    LARGE_INTEGER size{};
    size.QuadPart = static_cast<LONGLONG>(partSize);
    const BOOL ok = w->splitFile(src.h, firstPart.c_str(), &size, 0);
    const DWORD splitError = GetLastError();
    w->unregisterCallback(src.h, reinterpret_cast<FARPROC>(&onMessage));
    if (!ok) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"split cancelled", firstPart.wstring());
        }
        return fail(ErrorCode::WimFailure, L"split failed", firstPart.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(splitError)));
    }
    // install.swm, install2.swm, … next to the first part.
    int parts = 0;
    const std::wstring stem = firstPart.stem().wstring();
    for (int n = 1;; ++n) {
        const auto part = firstPart.parent_path() / (n == 1 ? firstPart.filename().wstring()
                                                           : stem + std::to_wstring(n) + firstPart.extension().wstring());
        if (!std::filesystem::exists(part, ec)) {
            break;
        }
        parts = n;
    }
    task.report(1.0, L"split");
    return parts;
}

} // namespace wl::core
