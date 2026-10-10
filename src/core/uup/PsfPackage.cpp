#include "core/uup/PsfPackage.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/wim/WimGapi.h"

#include <windows.h>

#include <bcrypt.h>
#include <msdelta.h>

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

namespace wl::core::uup {

namespace {

// "\\?\C:\…": package paths go past MAX_PATH (WinSxS names are long).
std::wstring longPath(const std::filesystem::path& path) {
    std::wstring p = nativePath(path).wstring();
    return p.starts_with(L"\\\\?\\") ? p : L"\\\\?\\" + p;
}

bool ensureFolder(const std::filesystem::path& folder) {
    const std::wstring full = longPath(folder);
    if (CreateDirectoryW(full.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (GetLastError() != ERROR_PATH_NOT_FOUND || folder.parent_path() == folder) {
        return false;
    }
    return ensureFolder(folder.parent_path()) &&
           (CreateDirectoryW(full.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
}

// A name inside the package: no drive, no "..", no leading separator.
std::optional<std::wstring> safeName(std::wstring name) {
    std::ranges::replace(name, L'/', L'\\');
    while (name.starts_with(L'\\')) {
        name.erase(0, 1);
    }
    if (name.empty() || name.find(L':') != std::wstring::npos) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at <= name.size();) {
        const std::size_t end = std::min(name.find(L'\\', at), name.size());
        const std::wstring_view part(name.data() + at, end - at);
        if (part.empty() || part == L"." || part == L"..") {
            return std::nullopt;
        }
        at = end + 1;
    }
    return name;
}

// msdelta.dll / UpdateCompression.dll: the same two functions.
struct DeltaApi {
    using Apply = BOOL(WINAPI*)(DELTA_FLAG_TYPE, DELTA_INPUT, DELTA_INPUT, LPDELTA_OUTPUT);
    using Free = BOOL(WINAPI*)(LPVOID);
    Apply apply = nullptr;
    Free free = nullptr;
};

// mspatcha.dll (PA19).
using ApplyPatchByBuffers = BOOL(WINAPI*)(PBYTE, ULONG, PBYTE, ULONG, PBYTE*, ULONG, ULONG*, FILETIME*, ULONG, PVOID, PVOID);

std::optional<DeltaApi> deltaApi(const wchar_t* dll, const std::filesystem::path& fallbackFolder) {
    HMODULE module = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module && !fallbackFolder.empty()) {
        const auto other = nativePath(fallbackFolder / dll);
        module = LoadLibraryExW(other.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!module) {
        return std::nullopt;
    }
    DeltaApi api;
    api.apply = reinterpret_cast<DeltaApi::Apply>(GetProcAddress(module, "ApplyDeltaB"));
    api.free = reinterpret_cast<DeltaApi::Free>(GetProcAddress(module, "DeltaFree"));
    if (!api.apply || !api.free) {
        return std::nullopt;
    }
    return api;
}

class Handle {
public:
    explicit Handle(HANDLE h) : m_h(h) {}
    ~Handle() {
        if (valid()) {
            CloseHandle(m_h);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] bool valid() const { return m_h && m_h != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const { return m_h; }

private:
    HANDLE m_h;
};

bool readAt(HANDLE file, std::uint64_t offset, std::byte* into, std::uint32_t length) {
    while (length > 0) {
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD got = 0;
        if (!ReadFile(file, into, length, &got, &at) || got == 0) {
            return false;
        }
        offset += got;
        into += got;
        length -= got;
    }
    return true;
}

bool writeAll(HANDLE file, const void* data, std::size_t size) {
    const auto* p = static_cast<const std::byte*>(data);
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        DWORD wrote = 0;
        if (!WriteFile(file, p, chunk, &wrote, nullptr) || wrote == 0) {
            return false;
        }
        p += wrote;
        size -= wrote;
    }
    return true;
}

std::string sha256Hex(const void* data, std::size_t size) {
    std::array<UCHAR, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)),
                                   static_cast<ULONG>(size), digest.data(), static_cast<ULONG>(digest.size())))) {
        return {};
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string hex;
    for (const UCHAR b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
    }
    return hex;
}

const char* kindName(PsfEntry::Kind kind) {
    switch (kind) {
    case PsfEntry::Kind::Pa19: return "PA19";
    case PsfEntry::Kind::Pa30: return "PA30";
    case PsfEntry::Kind::Pa31: return "PA31";
    case PsfEntry::Kind::Raw: break;
    }
    return "RAW";
}

} // namespace

PsfEntry::Kind psfKind(std::string_view type, std::string_view firstBytes) noexcept {
    auto is = [&](std::string_view name) {
        return type.size() == name.size() &&
               std::ranges::equal(type, name, [](char a, char b) { return (a & ~0x20) == (b & ~0x20); });
    };
    if (is("RAW")) {
        return PsfEntry::Kind::Raw;
    }
    if (is("PA19")) {
        return PsfEntry::Kind::Pa19;
    }
    if (is("PA30")) {
        return PsfEntry::Kind::Pa30;
    }
    if (is("PA31")) {
        return PsfEntry::Kind::Pa31;
    }
    const std::string_view magic = firstBytes.substr(0, 4);
    return magic == "PA19"   ? PsfEntry::Kind::Pa19
           : magic == "PA30" ? PsfEntry::Kind::Pa30
           : magic == "PA31" ? PsfEntry::Kind::Pa31
                             : PsfEntry::Kind::Raw;
}

Result<std::vector<PsfEntry>> parsePsfIndex(std::string_view xml) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return fail(ErrorCode::ParseError, L"the package's file index (express.psf.cix.xml) is not XML");
    }
    const auto files = doc.child("Container").child("Files");
    if (!files) {
        return fail(ErrorCode::ParseError, L"the package's file index has no Container/Files");
    }
    std::vector<PsfEntry> entries;
    for (const auto& file : files.children("File")) {
        const auto source = file.child("Delta").child("Source");
        auto name = safeName(utf8::toWide(file.attribute("name").value()));
        if (!name) {
            return fail(ErrorCode::ParseError, L"a file of the package has a name outside its folder",
                        utf8::toWide(file.attribute("name").value()));
        }
        if (!source) {
            return fail(ErrorCode::ParseError, L"a file of the package has no Delta/Source", *name);
        }
        PsfEntry e;
        e.name = std::move(*name);
        e.offset = source.attribute("offset").as_ullong();
        e.length = source.attribute("length").as_uint();
        e.kind = psfKind(source.attribute("type").value(), {});
        e.time = file.attribute("time").as_ullong();
        for (const auto& hash : file.children("Hash")) {
            if (std::string_view(hash.attribute("alg").value()) == "SHA256") {
                e.sha256 = hash.attribute("value").value();
                std::ranges::transform(e.sha256, e.sha256.begin(), [](char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; });
            }
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

Result<void> expandPsf(const std::filesystem::path& psf, const std::vector<PsfEntry>& entriesIn,
                       const std::filesystem::path& folder, const TaskContext& task,
                       const std::filesystem::path& fallbackDlls) {
    std::vector<PsfEntry> entries = entriesIn;
    std::ranges::sort(entries, {}, &PsfEntry::offset); // the .psf read front to back
    std::uint64_t total = 0;
    // UpdateCompression.dll first for PA30 too: msdelta.dll refuses the deltas of 24H2's updates
    // (flags 0xA060000: ERROR_INVALID_DATA) that it applies.
    std::optional<DeltaApi> pa30, pa31;
    ApplyPatchByBuffers pa19 = nullptr;
    for (const auto& e : entries) {
        total += e.length;
        if (e.kind == PsfEntry::Kind::Pa30 && !pa30) {
            pa30 = deltaApi(L"UpdateCompression.dll", fallbackDlls);
            if (!pa30) {
                log::warn("uup", L"UpdateCompression.dll not found: msdelta.dll applies the PA30 deltas");
                pa30 = deltaApi(L"msdelta.dll", {});
            }
            if (!pa30) {
                return fail(ErrorCode::Unsupported, L"msdelta.dll could not be loaded (PA30 deltas)", e.name);
            }
        } else if (e.kind == PsfEntry::Kind::Pa31 && !pa31) {
            pa31 = deltaApi(L"UpdateCompression.dll", fallbackDlls);
            if (!pa31) {
                return fail(ErrorCode::Unsupported,
                            L"PA31 deltas need UpdateCompression.dll (Windows 11 24H2 or later, or the image's own)", e.name);
            }
        } else if (e.kind == PsfEntry::Kind::Pa19 && !pa19) {
            const HMODULE m = LoadLibraryExW(L"mspatcha.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            pa19 = m ? reinterpret_cast<ApplyPatchByBuffers>(GetProcAddress(m, "ApplyPatchToFileByBuffers")) : nullptr;
            if (!pa19) {
                return fail(ErrorCode::Unsupported, L"mspatcha.dll could not be loaded (PA19 deltas)", e.name);
            }
        }
    }
    log::info("uup", std::format(L"expand {} file(s), {} bytes of {} -> {}", entries.size(), total, psf.wstring(),
                                 folder.wstring()));
    if (!ensureFolder(folder)) {
        return fail(ErrorCode::IoError, L"cannot create the package folder", folder.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }

    std::mutex lock; // error, report
    std::optional<Error> error;
    std::atomic<std::size_t> next{0};
    std::atomic<std::uint64_t> done{0};
    std::atomic<bool> stop{false};
    auto lastReport = std::chrono::steady_clock::now();
    auto setError = [&](Error e) {
        const std::lock_guard guard(lock);
        if (!error) {
            error = std::move(e);
        }
        stop = true;
    };
    auto work = [&] {
        Handle in(CreateFileW(longPath(psf).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!in.valid()) {
            setError(Error{ErrorCode::IoError, L"cannot open the package's payload", psf.wstring(),
                           static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()))});
            return;
        }
        std::vector<std::byte> buffer;
        for (std::size_t i = next++; i < entries.size() && !stop; i = next++) {
            if (task.cancel.cancelled()) {
                setError(Error{ErrorCode::Cancelled, L"expanding the update cancelled", folder.wstring()});
                return;
            }
            const PsfEntry& e = entries[i];
            buffer.resize(e.length);
            if (e.length && !readAt(in.get(), e.offset, buffer.data(), e.length)) {
                setError(Error{ErrorCode::IoError, std::format(L"cannot read {} bytes at {} of the payload", e.length, e.offset),
                               e.name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()))});
                return;
            }
            const void* data = buffer.data();
            std::size_t size = buffer.size();
            DELTA_OUTPUT out{};
            PBYTE patched = nullptr;
            const DeltaApi* api = e.kind == PsfEntry::Kind::Pa30 ? &*pa30 : e.kind == PsfEntry::Kind::Pa31 ? &*pa31 : nullptr;
            if (api) {
                DELTA_INPUT none{};
                DELTA_INPUT delta{};
                delta.lpcStart = buffer.data();
                delta.uSize = buffer.size();
                delta.Editable = FALSE;
                if (!api->apply(DELTA_FLAG_NONE, none, delta, &out)) {
                    setError(Error{ErrorCode::IoError, std::format(L"a {} delta of the update would not apply",
                                                                   utf8::toWide(kindName(e.kind))),
                                   e.name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()))});
                    return;
                }
                data = out.lpStart;
                size = out.uSize;
            } else if (e.kind == PsfEntry::Kind::Pa19) {
                ULONG actual = 0;
                if (!pa19(reinterpret_cast<PBYTE>(buffer.data()), e.length, nullptr, 0, &patched, 0, &actual, nullptr, 0,
                          nullptr, nullptr)) {
                    setError(Error{ErrorCode::IoError, L"a PA19 delta of the update would not apply", e.name,
                                   static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()))});
                    return;
                }
                data = patched;
                size = actual;
            }
            const auto target = folder / e.name;
            if (!e.sha256.empty() && sha256Hex(data, size) != e.sha256) {
                if (out.lpStart) {
                    api->free(out.lpStart);
                }
                if (patched) {
                    VirtualFree(patched, 0, MEM_RELEASE);
                }
                setError(Error{ErrorCode::IoError,
                               std::format(L"a file of the update does not match its SHA-256 ({})", utf8::toWide(kindName(e.kind))),
                               e.name});
                return;
            }
            bool ok = ensureFolder(target.parent_path());
            if (ok) {
                Handle file(CreateFileW(longPath(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr));
                ok = file.valid() && writeAll(file.get(), data, size);
                if (ok && e.time) {
                    FILETIME ft{static_cast<DWORD>(e.time & 0xFFFFFFFFu), static_cast<DWORD>(e.time >> 32)};
                    SetFileTime(file.get(), nullptr, nullptr, &ft);
                }
            }
            const DWORD why = GetLastError();
            if (out.lpStart) {
                api->free(out.lpStart);
            }
            if (patched) {
                VirtualFree(patched, 0, MEM_RELEASE);
            }
            if (!ok) {
                setError(Error{ErrorCode::IoError, L"cannot write a file of the update", target.wstring(),
                               static_cast<std::int32_t>(HRESULT_FROM_WIN32(why))});
                return;
            }
            done += e.length;
            const std::lock_guard guard(lock);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastReport >= std::chrono::milliseconds(200)) {
                lastReport = now;
                task.report(total ? static_cast<double>(done.load()) / static_cast<double>(total) : 0.0, L"expand");
            }
        }
    };
    {
        std::vector<std::jthread> threads;
        const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        for (unsigned t = 0; t < count; ++t) {
            threads.emplace_back(work);
        }
    }
    if (error) {
        return std::unexpected(*error);
    }
    task.report(1.0, L"expand");
    return {};
}

Result<std::filesystem::path> expandUpdatePackage(const std::filesystem::path& container, const std::filesystem::path& folder,
                                                  const TaskContext& task, const std::filesystem::path& fallbackDlls) {
    auto part = [&task](double from, double to) {
        return TaskContext{task.cancel, [&task, from, to](double f, std::wstring_view stage) {
                               task.report(from + (to - from) * std::clamp(f, 0.0, 1.0), stage);
                           }};
    };
    if (auto r = applyImage(container, 1, folder, part(0.0, 0.1)); !r) {
        return std::unexpected(r.error());
    }
    // 26100.9441+: the manifests' own ESD beside the container (…-arm64.mumx.esd).
    auto mumx = container;
    mumx.replace_extension(L".mumx.esd");
    std::error_code ec;
    if (std::filesystem::is_regular_file(mumx, ec)) {
        if (auto r = applyImage(mumx, 1, folder, part(0.1, 0.15)); !r) {
            return std::unexpected(r.error());
        }
    }
    std::filesystem::path index = folder / L"express.psf.cix.xml";
    if (!std::filesystem::is_regular_file(index, ec)) {
        index.clear();
        for (const auto& item : std::filesystem::directory_iterator(folder, ec)) {
            if (text::lower(item.path().filename().wstring()).ends_with(L".psf.cix.xml")) {
                index = item.path();
                break;
            }
        }
    }
    if (index.empty()) {
        return fail(ErrorCode::NotFound, L"the update has no file index (*.psf.cix.xml)", container.wstring());
    }
    std::ifstream in(index, std::ios::binary);
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto entries = parsePsfIndex(xml);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    auto psf = container;
    psf.replace_extension(L".psf");
    if (auto r = expandPsf(psf, *entries, folder, part(0.15, 1.0), fallbackDlls); !r) {
        return std::unexpected(r.error());
    }
    const auto mum = folder / L"update.mum";
    if (!std::filesystem::is_regular_file(mum, ec)) {
        return fail(ErrorCode::NotFound, L"the expanded update has no update.mum", folder.wstring());
    }
    return mum;
}

} // namespace wl::core::uup
