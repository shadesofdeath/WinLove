#include "core/image/wim/WimGapi.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/WimFile.h"
#include "core/image/dism/HostDism.h"
#include "core/image/wim/WimVerify.h"
#include "core/io/ByteSource.h"
#include "core/system/Files.h"
#include "core/system/Process.h"
#include "core/system/Signature.h"

#include <pugixml.hpp>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <functional>
#include <map>
#include <memory>
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
using SetReferenceFileFn = BOOL(WINAPI*)(HANDLE, PCWSTR, DWORD);
using SetBootImageFn = BOOL(WINAPI*)(HANDLE, DWORD);
using CaptureImageFn = HANDLE(WINAPI*)(HANDLE, PCWSTR, DWORD);
using ApplyImageFn = BOOL(WINAPI*)(HANDLE, PCWSTR, DWORD);
constexpr DWORD kReferenceAppend = 0x00010000;  // WIM_REFERENCE_APPEND
constexpr DWORD kExportAllowDuplicates = 0x1;    // WIM_EXPORT_ALLOW_DUPLICATES
// WIM_FLAG_NO_DIRACL | WIM_FLAG_NO_FILEACL: a scratch copy of a package, readable and deletable by the caller.
constexpr DWORD kApplyNoAcls = 0x10 | 0x20;

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
    SetReferenceFileFn setReferenceFile = nullptr;
    SetBootImageFn setBootImage = nullptr;
    CaptureImageFn captureImage = nullptr;
    ApplyImageFn applyImage = nullptr;
};

template <class F>
bool load(HMODULE module, const char* name, F& target) {
    target = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    return target != nullptr;
}

// One copy of wimgapi.dll (D-081): this PC's (System32), the Windows ADK's, or a setup media's own
// (sources\wimgapi.dll next to install.esd) — loaded once, never unloaded.
struct Library {
    std::filesystem::path path;
    std::wstring origin; // "system", "adk", "media", "forced"
    std::wstring version;
    Api api;
    Result<void> status = {}; // loaded with every entry point
};

std::mutex g_librariesMutex;
std::vector<std::unique_ptr<Library>> g_libraries;
std::optional<std::filesystem::path> g_forced;              // wlcli --wimgapi=
std::map<std::wstring, const Library*> g_choice;             // per file (path|size|time): the copy that opens it
thread_local const Library* t_library = nullptr;             // this operation's copy

const Library& loadLibrary(const std::filesystem::path& path, std::wstring origin) {
    std::scoped_lock lock(g_librariesMutex);
    for (const auto& l : g_libraries) {
        if (text::lower(l->path.wstring()) == text::lower(path.wstring())) {
            return *l;
        }
    }
    auto l = std::make_unique<Library>();
    l->path = path;
    l->origin = std::move(origin);
    l->version = fileVersion(path);
    // Only System32 and our own choice of folder are trusted to hold the dll: another copy must
    // carry Microsoft's signature (a setup media is the user's file, but WinLove runs elevated).
    if (l->origin != L"system" && l->origin != L"forced" && !signedByMicrosoft(path)) {
        l->status = fail(ErrorCode::Unsupported, L"wimgapi.dll is not signed by Microsoft", path.wstring());
    } else if (const HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); !m) {
        l->status = fail(ErrorCode::NotFound, L"wimgapi.dll not found", path.wstring(),
                         static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    } else {
        Api& a = l->api;
        const bool ok = load(m, "WIMCreateFile", a.createFile) && load(m, "WIMCloseHandle", a.closeHandle) &&
                        load(m, "WIMSetTemporaryPath", a.setTemporaryPath) && load(m, "WIMLoadImage", a.loadImage) &&
                        load(m, "WIMExportImage", a.exportImage) && load(m, "WIMDeleteImage", a.deleteImage) &&
                        load(m, "WIMGetImageInformation", a.getImageInformation) &&
                        load(m, "WIMSetImageInformation", a.setImageInformation) &&
                        load(m, "WIMRegisterMessageCallback", a.registerCallback) &&
                        load(m, "WIMUnregisterMessageCallback", a.unregisterCallback) && load(m, "WIMSplitFile", a.splitFile) &&
                        load(m, "WIMSetReferenceFile", a.setReferenceFile) && load(m, "WIMSetBootImage", a.setBootImage) &&
                        load(m, "WIMCaptureImage", a.captureImage) && load(m, "WIMApplyImage", a.applyImage);
        if (!ok) {
            l->status = fail(ErrorCode::Unsupported, L"wimgapi.dll is missing expected entry points", path.wstring());
        }
    }
    if (!l->status) {
        log::warn("wim", std::format(L"wimgapi ({}) unusable: {} — {}", l->origin, l->status.error().message, path.wstring()));
    }
    g_libraries.push_back(std::move(l));
    return *g_libraries.back();
}

const Library& defaultLibrary() {
    if (g_forced) {
        return loadLibrary(*g_forced, L"forced");
    }
    const auto dll = systemTool(L"wimgapi.dll");
    return loadLibrary(dll ? std::filesystem::path(*dll) : std::filesystem::path(L"wimgapi.dll"), L"system");
}

const Library& currentLibrary() {
    return t_library ? *t_library : defaultLibrary();
}

Result<const Api*> api() {
    const Library& l = currentLibrary();
    if (!l.status) {
        return std::unexpected(l.status.error());
    }
    return &l.api;
}

std::wstring describe(const Library& l) {
    return std::format(L"wimgapi.dll {} ({})", l.version.empty() ? L"?" : l.version, l.origin);
}

// Whether `l` opens `file` and reads its first edition's file list — where a wimgapi that cannot
// read the file's format fails (a missing / replaced copy fails earlier).
bool opens(const Library& l, const std::filesystem::path& file) {
    if (!l.status) {
        return false;
    }
    const Api& a = l.api;
    const HANDLE h = a.createFile(file.c_str(), GENERIC_READ, OPEN_EXISTING, 0, 0, nullptr);
    if (!h) {
        log::info("wim", std::format(L"{} cannot open {} ({:#010x})", describe(l), file.wstring(),
                                     static_cast<unsigned>(HRESULT_FROM_WIN32(GetLastError()))));
        return false;
    }
    a.setTemporaryPath(h, tempFolder().c_str());
    const HANDLE image = a.loadImage(h, 1);
    const DWORD error = GetLastError();
    if (image) {
        a.closeHandle(image);
    }
    a.closeHandle(h);
    if (!image) {
        log::info("wim", std::format(L"{} cannot read {} ({:#010x})", describe(l), file.wstring(),
                                     static_cast<unsigned>(HRESULT_FROM_WIN32(error))));
    }
    return image != nullptr;
}

// The other Microsoft copies at hand for `file`, in the order they are tried.
std::vector<const Library*> alternatives(const std::filesystem::path& file) {
    std::vector<const Library*> out;
    const Library& first = defaultLibrary();
    auto add = [&](const std::filesystem::path& path, const wchar_t* origin) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || text::lower(path.wstring()) == text::lower(first.path.wstring())) {
            return;
        }
        const Library& l = loadLibrary(path, origin);
        if (l.status && std::ranges::find(out, &l) == out.end()) {
            out.push_back(&l);
        }
    };
    if (const auto adk = adkDismFolder()) {
        add(*adk / L"wimgapi.dll", L"adk");
    }
    if (!file.empty() && file.has_parent_path()) {
        add(file.parent_path() / L"wimgapi.dll", L"media"); // install.esd's own sources\ folder
    }
    return out;
}

// The copy to work on `file` with: this PC's, unless it is unusable or cannot read the file and
// another copy can (D-081). Remembered per file version.
const Library* chooseFor(const std::filesystem::path& file) {
    const Library& first = defaultLibrary();
    std::error_code ec;
    const bool exists = !file.empty() && std::filesystem::is_regular_file(file, ec);
    if (!exists) {
        // Nothing to read yet (a capture into a new file): any copy that loads.
        if (first.status) {
            return &first;
        }
        const auto others = alternatives(file);
        return others.empty() ? &first : others.front();
    }
    const auto size = std::filesystem::file_size(file, ec);
    const auto time = std::filesystem::last_write_time(file, ec).time_since_epoch().count();
    const std::wstring key = std::format(L"{}|{}|{}", text::lower(file.wstring()), size, time);
    {
        std::scoped_lock lock(g_librariesMutex);
        if (const auto it = g_choice.find(key); it != g_choice.end()) {
            return it->second;
        }
    }
    const Library* chosen = &first;
    if (!opens(first, file)) {
        for (const Library* other : alternatives(file)) {
            if (opens(*other, file)) {
                log::warn("wim", std::format(L"{} could not read {}; using {} ({})", describe(first), file.wstring(),
                                             describe(*other), other->path.wstring()));
                chosen = other;
                break;
            }
        }
    }
    std::scoped_lock lock(g_librariesMutex);
    g_choice[key] = chosen;
    return chosen;
}

// For the time of one public call: the copy chosen for its file (an outer call's choice stays).
class UseLibrary {
public:
    explicit UseLibrary(const std::filesystem::path& file) : m_previous(t_library) {
        if (!t_library) {
            t_library = chooseFor(file);
        }
    }
    explicit UseLibrary(const Library& library) : m_previous(t_library) { t_library = &library; }
    ~UseLibrary() { t_library = m_previous; }
    UseLibrary(const UseLibrary&) = delete;
    UseLibrary& operator=(const UseLibrary&) = delete;

private:
    const Library* m_previous;
};

Error lastError(std::wstring context) {
    const DWORD code = GetLastError();
    return Error{ErrorCode::WimFailure, std::format(L"wimgapi call failed: {}", describe(currentLibrary())), std::move(context),
                 static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))};
}

} // namespace

WimLibraryInfo wimgapiFor(const std::filesystem::path& file) {
    const Library* l = chooseFor(nativePath(file));
    return {l->path, l->version, l->origin, static_cast<bool>(l->status)};
}

void forceWimgapi(std::filesystem::path dll) {
    std::scoped_lock lock(g_librariesMutex);
    g_forced = std::move(dll);
}

namespace {

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
                             WimCompression compression, const TaskContext& task, DWORD exportFlags = 0,
                             const std::vector<std::filesystem::path>& references = {});
} // namespace

Result<void> exportImage(const std::filesystem::path& source, int index, const std::filesystem::path& destination,
                         WimCompression compression, const TaskContext& task) {
    const UseLibrary use{nativePath(source)};
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
                             WimCompression compression, const TaskContext& task, DWORD exportFlags,
                             const std::vector<std::filesystem::path>& references) {
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
    for (const auto& part : references) {
        if (!w->setReferenceFile(src.h, part.c_str(), kReferenceAppend)) {
            return std::unexpected(lastError(L"part " + part.wstring() + L" of " + source.wstring()));
        }
    }

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
    const BOOL ok = w->exportImage(image.h, dst.h, exportFlags);
    const DWORD exportError = GetLastError();
    w->unregisterCallback(src.h, reinterpret_cast<FARPROC>(&onMessage));
    if (!ok) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"export cancelled", destination.wstring());
        }
        return fail(ErrorCode::WimFailure, std::format(L"export failed: {}", describe(currentLibrary())),
                    destination.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(exportError)));
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
    // Swap through a rename: the original is only deleted once the new file is in its place.
    if (auto swapped = swapIntoPlace(wim, fresh, wim); !swapped) {
        return swapped;
    }
    task.report(1.0, stage);
    return {};
}

} // namespace

Result<void> optimizeWim(const std::filesystem::path& wimInput, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
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
    const UseLibrary use{wim};
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

Result<void> reorderImages(const std::filesystem::path& wimInput, std::span<const int> order, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
    auto info = readInfo(wim);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (!isPermutation(order, static_cast<int>(info->images.size()))) {
        return fail(ErrorCode::InvalidArgument, L"the new order must name every edition once", wim.wstring());
    }
    if (std::ranges::is_sorted(order)) {
        return {};
    }
    if (!plainWim(info->header) || info->header.bootIndex != 0) {
        return fail(ErrorCode::Unsupported,
                    L"editions can only be reordered in a plain install WIM (not an ESD, a split or a boot image)",
                    wim.wstring());
    }
    log::info("wim", std::format(L"reorder the {} editions of {}", info->images.size(), wim.wstring()));
    return rewriteWith(wim, info->header.compression, order, task, L"reorder");
}

bool isPermutation(std::span<const int> order, int count) {
    if (static_cast<int>(order.size()) != count) {
        return false;
    }
    std::vector<bool> seen(static_cast<std::size_t>(count), false);
    for (const int index : order) {
        if (index < 1 || index > count || seen[static_cast<std::size_t>(index - 1)]) {
            return false;
        }
        seen[static_cast<std::size_t>(index - 1)] = true;
    }
    return true;
}

Result<void> setImageText(const std::filesystem::path& wimInput, int index, const ImageText& text) {
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
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
    const UseLibrary use{source};
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

// ---- Images page tools (D-058) -----------------------------------------------------------------

namespace {

// The other parts of a split WIM: install2.swm, install3.swm … next to install.swm. WIMSetReferenceFile
// takes them one by one (a wildcard is refused: ERROR_INVALID_NAME, 2026-10-01).
std::vector<std::filesystem::path> otherParts(const std::filesystem::path& firstPart) {
    std::wstring stem = firstPart.stem().wstring();
    while (!stem.empty() && std::iswdigit(stem.back())) {
        stem.pop_back();
    }
    std::vector<std::filesystem::path> parts;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(firstPart.parent_path(), ec)) {
        const auto& p = entry.path();
        if (!entry.is_regular_file(ec) || _wcsicmp(p.extension().c_str(), firstPart.extension().c_str()) != 0 ||
            _wcsicmp(p.filename().c_str(), firstPart.filename().c_str()) == 0) {
            continue;
        }
        const std::wstring name = p.stem().wstring();
        if (name.size() > stem.size() && _wcsnicmp(name.c_str(), stem.c_str(), stem.size()) == 0 &&
            std::all_of(name.begin() + static_cast<std::ptrdiff_t>(stem.size()), name.end(), [](wchar_t c) { return std::iswdigit(c) != 0; })) {
            parts.push_back(p);
        }
    }
    std::ranges::sort(parts);
    return parts;
}

std::filesystem::path withExtension(std::filesystem::path p, const wchar_t* extension) {
    p.replace_extension(extension);
    return p;
}

} // namespace

namespace {

// Every copy of wimgapi failed on `source`: which ones, and — for an ESD — what its solid resources
// are. An ESD from Windows has LZMS ones; another tool's (wimlib's options) may not be readable.
Error explainFailure(Error error, const std::filesystem::path& source, const std::vector<std::wstring>& tried) {
    std::wstring detail;
    if (auto file = DiskFile::open(source)) {
        if (auto header = readWimHeader(**file); header && header->solid) {
            if (auto solid = solidResources(**file); solid && !solid->empty()) {
                std::map<std::pair<int, std::uint32_t>, int> kinds;
                for (const auto& r : *solid) {
                    ++kinds[{static_cast<int>(r.compression), r.chunkSize}];
                }
                bool foreign = false;
                for (const auto& [kind, n] : kinds) {
                    const auto compression = static_cast<WimCompression>(kind.first);
                    foreign = foreign || compression != WimCompression::Lzms;
                    detail += std::format(L"{}{} × {} {} KiB", detail.empty() ? L"" : L", ", n, compressionName(compression),
                                          kind.second / 1024);
                }
                detail = std::format(L"; solid resources: {}{}", detail,
                                     foreign ? L" — Windows writes LZMS only: this ESD was made by another tool" : L"");
            }
        }
    }
    std::wstring copies;
    for (const auto& t : tried) {
        copies += (copies.empty() ? L"" : L", ") + t;
    }
    error.message = std::format(L"no wimgapi could read the image (tried {}){}", copies, detail);
    log::error("wim", error.message + L" — " + source.wstring());
    return error;
}

} // namespace

Result<void> exportImageWithReferences(const std::filesystem::path& source, int index,
                                       std::span<const std::filesystem::path> references,
                                       const std::filesystem::path& destination, WimCompression compression,
                                       const TaskContext& task) {
    std::vector<std::filesystem::path> refs;
    for (const auto& r : references) {
        refs.push_back(nativePath(r));
    }
    const UseLibrary use{source};
    return exportImageOnce(source, index, destination, compression, task, 0, refs);
}

Result<void> exportImages(const std::filesystem::path& sourceInput, std::span<const int> indexes,
                          const std::filesystem::path& destinationInput, WimCompression compression, const TaskContext& task) {
    const std::filesystem::path source = nativePath(sourceInput);
    const std::filesystem::path destination = nativePath(destinationInput);
    auto info = readInfo(source);
    if (!info) {
        return std::unexpected(info.error());
    }
    const std::vector<std::filesystem::path> references =
        info->header.totalParts > 1 ? otherParts(source) : std::vector<std::filesystem::path>{};
    if (info->header.totalParts > 1 && references.size() + 1 != info->header.totalParts) {
        return fail(ErrorCode::NotFound, std::format(L"{} of the {} parts are next to it", references.size() + 1, info->header.totalParts),
                    source.wstring());
    }
    const double count = static_cast<double>(std::max<std::size_t>(indexes.size(), 1));
    auto all = [&]() -> Result<void> {
        for (std::size_t i = 0; i < indexes.size(); ++i) {
            const TaskContext one{task.cancel, [&](double f, std::wstring_view) {
                                      task.report((static_cast<double>(i) + f) / count, L"export");
                                  }};
            if (auto r = exportImageOnce(source, indexes[i], destination, compression, one, kExportAllowDuplicates, references);
                !r) {
                return r;
            }
        }
        return {};
    };
    std::error_code ec;
    const bool fresh = !std::filesystem::exists(destination, ec);
    const UseLibrary use{source};
    auto done = all();
    // A wimgapi that read the first file list may still fail on the files themselves (a format it
    // does not know): into a new file, the other copies get their turn (D-081).
    if (!done && done.error().code == ErrorCode::WimFailure && fresh && !task.cancel.cancelled()) {
        std::vector<std::wstring> tried{describe(currentLibrary())};
        for (const Library* other : alternatives(source)) {
            if (other == &currentLibrary()) {
                continue;
            }
            std::filesystem::remove(destination, ec);
            log::warn("wim", std::format(L"export with {} failed ({}); again with {}", tried.back(), done.error().message,
                                         describe(*other)));
            const UseLibrary again{*other};
            tried.push_back(describe(*other));
            if (auto retried = all(); retried || retried.error().code != ErrorCode::WimFailure) {
                done = std::move(retried);
                break;
            }
        }
        if (!done) {
            done = std::unexpected(explainFailure(done.error(), source, tried));
        }
    }
    if (!done) {
        if (fresh) {
            std::filesystem::remove(destination, ec); // nothing half-written stays behind
        }
        return done;
    }
    if (compression == WimCompression::Lzms) {
        auto header = readInfo(destination);
        if (!header || header->header.compression != WimCompression::Lzms || !header->header.solid) {
            return fail(ErrorCode::WimFailure, L"the ESD was not written as solid LZMS", destination.wstring());
        }
    }
    task.report(1.0, L"export");
    return {};
}

Result<std::filesystem::path> recompressWim(const std::filesystem::path& wimInput, WimCompression target,
                                            const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    auto info = readInfo(wim);
    if (!info) {
        return std::unexpected(info.error());
    }
    const auto& header = info->header;
    if (header.totalParts > 1) {
        return fail(ErrorCode::Unsupported, L"a split WIM is joined first (SWM -> WIM)", wim.wstring());
    }
    if (header.compression == target && header.solid == (target == WimCompression::Lzms)) {
        return fail(ErrorCode::InvalidArgument, L"the image already has this compression", wim.wstring());
    }
    const std::filesystem::path result = withExtension(wim, target == WimCompression::Lzms ? L".esd" : L".wim");
    const std::filesystem::path fresh = result.wstring() + L".new";
    std::error_code ec;
    if (result != wim && std::filesystem::exists(result, ec)) {
        return fail(ErrorCode::InvalidArgument, L"a file of that name is already next to it", result.wstring());
    }
    std::filesystem::remove(fresh, ec);
    std::vector<int> all;
    for (const auto& image : info->images) {
        all.push_back(image.index);
    }
    if (auto r = exportImages(wim, all, fresh, target, task); !r) {
        std::filesystem::remove(fresh, ec);
        return std::unexpected(r.error());
    }
    if (header.bootIndex > 0) {
        if (auto r = setBootImage(fresh, static_cast<int>(header.bootIndex)); !r) {
            std::filesystem::remove(fresh, ec);
            return std::unexpected(r.error());
        }
    }
    // Swap: the original goes only once the new file is in its place.
    if (auto swapped = swapIntoPlace(wim, fresh, result); !swapped) {
        return std::unexpected(swapped.error());
    }
    log::info("wim", std::format(L"recompressed {} -> {} ({})", wim.wstring(), result.wstring(), compressionName(target)));
    return result;
}

Result<void> mergeSplitWim(const std::filesystem::path& firstPartInput, const std::filesystem::path& destinationInput,
                           const TaskContext& task) {
    const std::filesystem::path first = nativePath(firstPartInput);
    const std::filesystem::path destination = nativePath(destinationInput);
    auto info = readInfo(first);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (info->header.partNumber != 1) {
        return fail(ErrorCode::InvalidArgument, L"open the first part (install.swm), not a later one", first.wstring());
    }
    std::error_code ec;
    if (std::filesystem::exists(destination, ec)) {
        return fail(ErrorCode::InvalidArgument, L"the WIM to write is already there", destination.wstring());
    }
    std::vector<int> all;
    for (const auto& image : info->images) {
        all.push_back(image.index);
    }
    if (auto r = exportImages(first, all, destination, WimCompression::Lzx, task); !r) {
        std::filesystem::remove(destination, ec);
        return r;
    }
    return {};
}

Result<int> duplicateEdition(const std::filesystem::path& wimInput, int index, const std::wstring& name, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
    auto info = readInfo(wim);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (!plainWim(info->header)) {
        return fail(ErrorCode::Unsupported, L"editions are copied within a plain WIM (convert an ESD first)", wim.wstring());
    }
    std::wstring description;
    for (const auto& image : info->images) {
        if (image.index == index) {
            description = image.description;
        }
    }
    // wimgapi will not export an edition into the file it is read from: through a one-edition copy.
    const std::filesystem::path one = wim.wstring() + L".copy.tmp";
    std::error_code ec;
    std::filesystem::remove(one, ec);
    const TaskContext half{task.cancel, [&](double f, std::wstring_view s) { task.report(f * 0.5, s); }};
    if (auto r = exportImageOnce(wim, index, one, info->header.compression, half); !r) {
        std::filesystem::remove(one, ec);
        return std::unexpected(r.error());
    }
    const TaskContext rest{task.cancel, [&](double f, std::wstring_view s) { task.report(0.5 + f * 0.5, s); }};
    auto appended = exportImageOnce(one, 1, wim, info->header.compression, rest, kExportAllowDuplicates);
    std::filesystem::remove(one, ec);
    if (!appended) {
        return std::unexpected(appended.error());
    }
    const int added = static_cast<int>(info->images.size()) + 1;
    if (auto r = setImageText(wim, added, ImageText{name, description, std::nullopt}); !r) {
        return std::unexpected(r.error());
    }
    log::info("wim", std::format(L"edition {} of {} copied as {} \"{}\"", index, wim.wstring(), added, name));
    return added;
}

Result<int> captureImage(const std::filesystem::path& folderInput, const std::filesystem::path& wimInput, const ImageText& text,
                         WimCompression compression, const TaskContext& task) {
    const std::filesystem::path folder = nativePath(folderInput);
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::NotFound, L"the folder to capture is not there", folder.wstring());
    }
    if (compression == WimCompression::Lzms) {
        return fail(ErrorCode::InvalidArgument, L"capture to a WIM (LZX / XPRESS); an ESD is made from it afterwards", wim.wstring());
    }
    int before = 0;
    const bool exists = std::filesystem::exists(wim, ec);
    if (exists) {
        auto info = readInfo(wim);
        if (!info) {
            return std::unexpected(info.error());
        }
        if (!plainWim(info->header)) {
            return fail(ErrorCode::Unsupported, L"a capture is appended to a plain WIM only", wim.wstring());
        }
        before = static_cast<int>(info->images.size());
    }
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    log::info("wim", std::format(L"capture {} -> {} ({})", folder.wstring(), wim.wstring(), compressionName(compression)));
    {
        DWORD created = 0;
        WimHandle file{w, w->createFile(wim.c_str(), GENERIC_WRITE | GENERIC_READ, exists ? kOpenExisting : kCreateNew, 0,
                                        compressionCode(compression), &created)};
        if (!file.h) {
            return std::unexpected(lastError(L"create " + wim.wstring()));
        }
        const auto tempDir = wim.parent_path().empty() ? std::filesystem::temp_directory_path() : wim.parent_path();
        w->setTemporaryPath(file.h, tempDir.c_str());
        CallbackState state{&task};
        w->registerCallback(file.h, reinterpret_cast<FARPROC>(&onMessage), &state);
        WimHandle image{w, w->captureImage(file.h, folder.c_str(), 0)};
        const DWORD error = GetLastError();
        w->unregisterCallback(file.h, reinterpret_cast<FARPROC>(&onMessage));
        if (!image.h) {
            if (task.cancel.cancelled()) {
                return fail(ErrorCode::Cancelled, L"capture cancelled", wim.wstring());
            }
            return fail(ErrorCode::WimFailure, L"capture failed", folder.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
        }
    }
    const int added = before + 1;
    if (auto r = setImageText(wim, added, text); !r) {
        return std::unexpected(r.error());
    }
    task.report(1.0, L"capture");
    return added;
}

Result<void> captureReference(const std::filesystem::path& folderInput, const std::filesystem::path& wimInput,
                              const TaskContext& task) {
    const std::filesystem::path folder = nativePath(folderInput);
    const std::filesystem::path wim = nativePath(wimInput);
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    std::error_code ec;
    std::filesystem::remove(wim, ec);
    DWORD created = 0;
    WimHandle file{w, w->createFile(wim.c_str(), GENERIC_WRITE | GENERIC_READ, kCreateNew, 0,
                                    compressionCode(WimCompression::Xpress), &created)};
    if (!file.h) {
        return std::unexpected(lastError(L"create " + wim.wstring()));
    }
    const auto tempDir = wim.parent_path().empty() ? std::filesystem::temp_directory_path() : wim.parent_path();
    w->setTemporaryPath(file.h, tempDir.c_str());
    CallbackState state{&task};
    w->registerCallback(file.h, reinterpret_cast<FARPROC>(&onMessage), &state);
    constexpr DWORD kNoAclsNoRpFix = 0x10 | 0x20 | 0x100; // WIM_FLAG_NO_DIRACL | NO_FILEACL | NO_RP_FIX
    WimHandle image{w, w->captureImage(file.h, folder.c_str(), kNoAclsNoRpFix)};
    const DWORD error = GetLastError();
    w->unregisterCallback(file.h, reinterpret_cast<FARPROC>(&onMessage));
    if (!image.h) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"capture cancelled", wim.wstring());
        }
        return fail(ErrorCode::WimFailure, L"capture failed", folder.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    return {};
}

Result<void> setBootImage(const std::filesystem::path& wimInput, int index) {
    const std::filesystem::path wim = nativePath(wimInput);
    const UseLibrary use{wim};
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    WimHandle file{w, w->createFile(wim.c_str(), GENERIC_WRITE | GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!file.h) {
        return std::unexpected(lastError(L"open " + wim.wstring()));
    }
    if (!w->setBootImage(file.h, static_cast<DWORD>(index))) {
        return std::unexpected(lastError(std::format(L"set boot index {} of {}", index, wim.wstring())));
    }
    return {};
}

Result<void> applyImage(const std::filesystem::path& wimInput, int index, const std::filesystem::path& folderInput,
                        const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const std::filesystem::path folder = nativePath(folderInput);
    const UseLibrary use{wim};
    auto a = api();
    if (!a) {
        return std::unexpected(a.error());
    }
    const Api* w = *a;
    log::info("wim", std::format(L"apply {} [{}] -> {}", wim.wstring(), index, folder.wstring()));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    WimHandle file{w, w->createFile(wim.c_str(), GENERIC_READ, kOpenExisting, 0, 0, nullptr)};
    if (!file.h) {
        return std::unexpected(lastError(L"open " + wim.wstring()));
    }
    w->setTemporaryPath(file.h, folder.parent_path().c_str());
    WimHandle image{w, w->loadImage(file.h, static_cast<DWORD>(index))};
    if (!image.h) {
        return std::unexpected(lastError(std::format(L"load index {} of {}", index, wim.wstring())));
    }
    CallbackState state{&task};
    w->registerCallback(file.h, reinterpret_cast<FARPROC>(&onMessage), &state);
    const BOOL ok = w->applyImage(image.h, folder.c_str(), kApplyNoAcls);
    const DWORD error = GetLastError();
    w->unregisterCallback(file.h, reinterpret_cast<FARPROC>(&onMessage));
    if (!ok) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"apply cancelled", folder.wstring());
        }
        return fail(ErrorCode::WimFailure, L"apply failed", folder.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    task.report(1.0, L"export");
    return {};
}

} // namespace wl::core
