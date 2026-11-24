#include "core/net/SelfUpdate.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/net/Http.h"
#include "core/system/Hash.h"

#include <windows.h>


#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <vector>

namespace wl::core {

namespace {

constexpr std::uint64_t kMaxAssetBytes = 512ull << 20;
constexpr std::wstring_view kDownloadPrefix = L"https://github.com/shadesofdeath/WinLove/releases/download/";

bool plainUrl(std::wstring_view url) {
    return std::ranges::none_of(url, [](wchar_t c) { return c <= L' ' || c == L'"' || c == L'<' || c == L'>' || c == L'\\'; }) &&
           url.find(L"..") == std::wstring_view::npos;
}

// The host of an https address ("" when it is not one).
std::wstring httpsHost(std::wstring_view url) {
    constexpr std::wstring_view scheme = L"https://";
    if (url.size() <= scheme.size() || !text::istartsWith(url, scheme)) {
        return {};
    }
    std::wstring_view rest = url.substr(scheme.size());
    const std::size_t end = rest.find_first_of(L"/?#");
    std::wstring_view host = rest.substr(0, end);
    if (host.find(L'@') != std::wstring_view::npos) {
        return {}; // "user@host": not an address an answer may send
    }
    if (const std::size_t colon = host.find(L':'); colon != std::wstring_view::npos) {
        if (host.substr(colon + 1) != L"443") {
            return {};
        }
        host = host.substr(0, colon);
    }
    return text::lower(host);
}

std::wstring hex(std::uint32_t value, int digits) {
    return std::format(L"{:0{}X}", value, digits);
}

Error ioError(std::wstring message, const std::filesystem::path& path, DWORD code = GetLastError()) {
    return Error{ErrorCode::IoError, std::move(message), path.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))};
}

bool fileThere(const std::filesystem::path& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The version resource's ProductVersion and fixed file version.
void readVersion(const std::filesystem::path& file, ExeFacts& facts) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    if (size == 0) {
        return;
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) {
        return;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) && info && infoSize >= sizeof(*info)) {
        facts.fileVersion = std::format(L"{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                                        HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    }
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT bytes = 0;
    std::vector<std::wstring> blocks;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) &&
        translations) {
        for (UINT i = 0; i < bytes / sizeof(Translation); ++i) {
            blocks.push_back(hex(translations[i].language, 4) + hex(translations[i].codePage, 4));
        }
    }
    blocks.push_back(L"040904B0"); // what WinLove.rc writes
    for (const auto& block : blocks) {
        wchar_t* value = nullptr;
        UINT length = 0;
        const std::wstring key = L"\\StringFileInfo\\" + block + L"\\ProductVersion";
        if (VerQueryValueW(data.data(), key.c_str(), reinterpret_cast<void**>(&value), &length) && value && length > 0) {
            facts.productVersion = std::wstring(value, wcsnlen(value, length));
            break;
        }
    }
    facts.version = parseAppVersion(facts.productVersion);
}

} // namespace

std::optional<ReleaseAsset> pickUpdateAsset(const Release& release) {
    auto usable = [](const ReleaseAsset& a) {
        return a.uploaded && a.size > 0 && a.size <= kMaxAssetBytes && trustedAssetUrl(a.url) &&
               text::iendsWith(a.name, L".exe");
    };
    for (const auto& a : release.assets) {
        if (text::iequals(a.name, L"WinLove.exe") && usable(a)) {
            return a;
        }
    }
    for (const auto& a : release.assets) {
        const std::wstring name = text::lower(a.name);
        const bool x64 = name.find(L"x64") != std::wstring::npos || name.find(L"amd64") != std::wstring::npos;
        if (name.starts_with(L"winlove") && x64 && name.find(L"arm64") == std::wstring::npos && usable(a)) {
            return a;
        }
    }
    return std::nullopt;
}

bool trustedAssetUrl(std::wstring_view url) {
    if (!plainUrl(url)) {
        return false;
    }
    if (url.size() > kDownloadPrefix.size() && url.substr(0, kDownloadPrefix.size()) == kDownloadPrefix) {
        return true;
    }
    // Where GitHub sends a release download (objects / release-assets .githubusercontent.com).
    const std::wstring host = httpsHost(url);
    constexpr std::wstring_view storage = L".githubusercontent.com";
    return host.size() > storage.size() && host.ends_with(storage);
}

UpdatePaths updatePaths(const std::filesystem::path& exe) {
    return {exe, std::filesystem::path(exe.wstring() + L".new"), std::filesystem::path(exe.wstring() + L".old")};
}

Result<void> checkFolderWritable(const std::filesystem::path& folder) {
    const std::filesystem::path probe = folder / std::format(L".winlove-update-{}.tmp", GetCurrentProcessId());
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        return std::unexpected(Error{code == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::IoError,
                                     L"the program's folder cannot be written to", folder.wstring(),
                                     static_cast<std::int32_t>(HRESULT_FROM_WIN32(code))});
    }
    CloseHandle(file);
    return {};
}

Result<std::filesystem::path> downloadUpdate(const ReleaseAsset& asset, const std::filesystem::path& exe,
                                             std::wstring_view userAgent, const TaskContext& task,
                                             const std::function<void(std::uint64_t, std::uint64_t)>& bytes) {
    if (!trustedAssetUrl(asset.url)) {
        return fail(ErrorCode::AccessDenied, L"the update is not on the project's release downloads", asset.url);
    }
    const UpdatePaths paths = updatePaths(exe);
    HttpOptions options;
    options.userAgent = userAgent.empty() ? std::wstring(L"WinLove") : std::wstring(userAgent);
    options.noCookies = true;
    log::info("update", std::format(L"downloading {} ({} bytes) to {}", asset.url, asset.size, paths.staged.wstring()));
    auto done = httpDownload(asset.url, paths.staged, asset.size, task, bytes, [](std::wstring_view finalUrl) {
        return trustedAssetUrl(finalUrl);
    }, &options);
    if (!done) {
        return std::unexpected(done.error());
    }
    return paths.staged;
}

Result<ExeFacts> readExeFacts(const std::filesystem::path& file, bool hash, const TaskContext& task) {
    ExeFacts facts;
    std::error_code ec;
    facts.size = std::filesystem::file_size(file, ec);
    if (ec) {
        return std::unexpected(Error{ErrorCode::IoError, L"cannot read the file", file.wstring(), ec.value()});
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::unexpected(Error{ErrorCode::IoError, L"cannot open the file", file.wstring(), 0});
    }
    std::array<unsigned char, 4096> head{};
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    const auto got = static_cast<std::size_t>(in.gcount());
    auto u16 = [&](std::size_t at) -> std::uint16_t {
        return at + 2 <= got ? static_cast<std::uint16_t>(head[at] | (head[at + 1] << 8)) : 0;
    };
    auto u32 = [&](std::size_t at) -> std::uint32_t {
        return at + 4 <= got ? static_cast<std::uint32_t>(head[at] | (head[at + 1] << 8) | (head[at + 2] << 16) | (head[at + 3] << 24))
                             : 0;
    };
    if (got >= 0x40 && head[0] == 'M' && head[1] == 'Z') {
        const std::size_t pe = u32(0x3C);
        if (pe + 24 + 70 <= got && head[pe] == 'P' && head[pe + 1] == 'E' && head[pe + 2] == 0 && head[pe + 3] == 0) {
            facts.machine = u16(pe + 4);
            const std::uint16_t characteristics = u16(pe + 22);
            const std::size_t optional = pe + 24;
            facts.pe32Plus = u16(optional) == 0x20B;
            facts.subsystem = u16(optional + 68); // the same offset in PE32 and PE32+
            facts.pe = (characteristics & 0x0002) != 0 && (characteristics & 0x2000) == 0; // executable, not a DLL
        }
    }
    in.close();
    readVersion(file, facts);
    if (hash) {
        auto sha = sha256File(file, task);
        if (!sha) {
            return std::unexpected(sha.error());
        }
        facts.sha256 = *sha;
    }
    return facts;
}

Result<UpdateVerdict> verifyUpdateFile(const std::filesystem::path& file, const UpdateExpectation& expect, const TaskContext& task) {
    auto facts = readExeFacts(file, !expect.sha256.empty(), task);
    if (!facts) {
        return std::unexpected(facts.error());
    }
    UpdateVerdict v;
    v.facts = std::move(*facts);
    const auto& f = v.facts;
    auto failed = [&](VerifyFailure failure, std::wstring detail) {
        v.failure = failure;
        v.detail = std::move(detail);
        return v;
    };
    if (expect.size > 0 && f.size != expect.size) {
        return failed(VerifyFailure::Size, std::format(L"{} bytes, the release lists {}", f.size, expect.size));
    }
    if (!expect.sha256.empty() && !text::iequals(f.sha256, expect.sha256)) {
        return failed(VerifyFailure::Hash, L"SHA-256 " + f.sha256 + L", the release lists " + expect.sha256);
    }
    if (!f.pe) {
        return failed(VerifyFailure::NotPe, L"not a Windows program");
    }
    if (f.machine != 0x8664 || !f.pe32Plus) {
        return failed(VerifyFailure::NotX64, std::format(L"machine 0x{:04X}, not x64", f.machine));
    }
    if (f.subsystem != 2) {
        return failed(VerifyFailure::NotGui, std::format(L"subsystem {}, not a windowed program", f.subsystem));
    }
    if (!f.version) {
        return failed(VerifyFailure::NoVersion, L"no readable version resource (ProductVersion \"" + f.productVersion + L"\")");
    }
    if (expect.version && *f.version != *expect.version) {
        return failed(VerifyFailure::VersionMismatch, L"the file says " + f.productVersion + L", not the release's version");
    }
    return v;
}

const wchar_t* verifyFailureName(VerifyFailure failure) noexcept {
    switch (failure) {
    case VerifyFailure::Size: return L"size";
    case VerifyFailure::Hash: return L"sha256";
    case VerifyFailure::NotPe: return L"not-pe";
    case VerifyFailure::NotX64: return L"not-x64";
    case VerifyFailure::NotGui: return L"not-gui";
    case VerifyFailure::NoVersion: return L"no-version";
    case VerifyFailure::VersionMismatch: return L"version";
    }
    return L"?";
}

Result<void> swapInUpdate(const std::filesystem::path& exe, const std::filesystem::path& staged, const SwapHooks& hooks) {
    const UpdatePaths paths = updatePaths(exe);
    if (!fileThere(staged)) {
        return fail(ErrorCode::NotFound, L"the downloaded update is not there", staged.wstring());
    }
    if (!fileThere(exe)) {
        return fail(ErrorCode::NotFound, L"the program file is not there", exe.wstring());
    }
    // An earlier update's backup: its process has gone, or this cannot go on (nothing changed yet).
    if (fileThere(paths.backup) && !DeleteFileW(paths.backup.c_str())) {
        return std::unexpected(ioError(L"an earlier update's old file is still in use", paths.backup));
    }
    // A running exe cannot be written or deleted, but it can be renamed.
    if (!MoveFileExW(exe.c_str(), paths.backup.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return std::unexpected(ioError(L"cannot move the program aside", exe));
    }
    Result<void> between = hooks.afterBackup ? hooks.afterBackup() : Result<void>{};
    if (between && !MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_WRITE_THROUGH)) {
        between = std::unexpected(ioError(L"cannot move the update into place", staged));
    }
    if (!between) {
        // Back as it was: the old exe under its own name, the download left where it was.
        if (!MoveFileExW(paths.backup.c_str(), exe.c_str(), MOVEFILE_WRITE_THROUGH)) {
            log::error("update", L"the swap failed and the old program could not be put back: " + paths.backup.wstring());
            Error e = between.error();
            e.message += L"; the old program is at " + paths.backup.wstring();
            return std::unexpected(std::move(e));
        }
        log::warn("update", L"swap failed, the old program is back: " + describe(between.error()));
        return between;
    }
    log::info("update", L"update in place: " + exe.wstring() + L" (the old one is " + paths.backup.filename().wstring() + L")");
    return {};
}

Result<void> rollbackUpdate(const std::filesystem::path& exe) {
    const UpdatePaths paths = updatePaths(exe);
    if (!fileThere(paths.backup)) {
        return {};
    }
    if (fileThere(exe)) {
        DeleteFileW(paths.staged.c_str()); // a download that waited is not this one
        if (!MoveFileExW(exe.c_str(), paths.staged.c_str(), MOVEFILE_WRITE_THROUGH)) {
            return std::unexpected(ioError(L"cannot move the new program aside", exe));
        }
    }
    if (!MoveFileExW(paths.backup.c_str(), exe.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        MoveFileExW(paths.staged.c_str(), exe.c_str(), MOVEFILE_WRITE_THROUGH); // keep a program in place
        return std::unexpected(ioError(L"cannot put the old program back", paths.backup, code));
    }
    log::info("update", L"update rolled back: " + exe.wstring());
    return {};
}

bool removeUpdateBackup(const std::filesystem::path& exe) {
    const UpdatePaths paths = updatePaths(exe);
    if (!fileThere(paths.backup)) {
        return true;
    }
    if (DeleteFileW(paths.backup.c_str())) {
        log::info("update", L"the old program file is gone: " + paths.backup.wstring());
        return true;
    }
    return false;
}

} // namespace wl::core
