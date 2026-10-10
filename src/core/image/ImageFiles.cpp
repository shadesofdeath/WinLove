#include "core/image/ImageFiles.h"

#include "base/Encoding.h"
#include "base/Utf8.h"

#include "base/Log.h"
#include "core/image/SystemComponents.h"
#include "core/system/Files.h"
#include "core/system/Handle.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <fstream>
#include <string>

namespace wl::core {

namespace {

// Lower case, backslashes: what the accepted roots are compared with.
std::wstring normalized(std::wstring_view path) {
    std::wstring out(path);
    for (auto& c : out) {
        c = c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

constexpr std::array<std::wstring_view, 2> kRoots = {L"users\\default\\", L"programdata\\"};

// The accepted root `path` is under ("Users\Default"), empty when it is under none.
std::wstring_view rootOf(std::wstring_view normalizedPath) {
    for (const auto root : kRoots) {
        if (normalizedPath.starts_with(root)) {
            return root.substr(0, root.size() - 1);
        }
    }
    return {};
}

} // namespace

Result<void> validateImageFile(std::wstring_view relative, std::size_t bytes) {
    // The path rules are the ones of a component recipe.
    ComponentRecipe asRecipe;
    asRecipe.paths.emplace_back(relative);
    if (auto ok = validateComponentRecipe(asRecipe); !ok) {
        return ok;
    }
    if (rootOf(normalized(relative)).empty()) {
        return fail(ErrorCode::InvalidArgument, L"files are only written under Users\\Default or ProgramData",
                    std::wstring(relative));
    }
    if (bytes > kImageFileLimit) {
        return fail(ErrorCode::InvalidArgument, L"file is too large for a configuration file", std::wstring(relative));
    }
    return {};
}

namespace {

// <mountDir>\<relative> ready to be written: folders made, a file in its place made replaceable.
Result<std::filesystem::path> prepareTarget(const std::filesystem::path& mountDir, std::wstring_view relative) {
    const std::wstring path = normalized(relative);
    std::error_code ec;
    if (!std::filesystem::is_directory(mountDir / std::wstring(rootOf(path)), ec)) {
        return fail(ErrorCode::NotFound, L"the image has no such folder", (mountDir / std::wstring(rootOf(path))).wstring());
    }
    auto target = resolveImagePath(mountDir, relative); // refuses a link on the way
    if (!target) {
        return std::unexpected(target.error());
    }
    std::filesystem::create_directories(target->parent_path(), ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot create folder", target->parent_path().wstring(), ec.value());
    }
    // A read-only or hidden file of the image in its place is replaced all the same.
    if (const DWORD attributes = GetFileAttributesW(target->c_str()); attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            return fail(ErrorCode::InvalidArgument, L"a folder is in the file's place", target->wstring());
        }
        SetFileAttributesW(target->c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    return target;
}

} // namespace

namespace {

void enableBackupRestore() {
    static const bool once = [] {
        (void)enablePrivilege(SE_BACKUP_NAME);
        (void)enablePrivilege(SE_RESTORE_NAME);
        return true;
    }();
    (void)once;
}

} // namespace

std::string imageFileBytes(std::wstring_view value) {
    constexpr std::wstring_view kPrefix = L"base64:";
    if (value.starts_with(kPrefix)) {
        const auto bytes = base64Decode(utf8::fromWide(value.substr(kPrefix.size())));
        return std::string(bytes.begin(), bytes.end());
    }
    return utf8::fromWide(value);
}

Result<void> unlinkImageFile(const std::filesystem::path& mountDir, std::wstring_view relative) {
    auto target = resolveImagePath(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    enableBackupRestore();
    UniqueHandle file{CreateFileW(target->c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        return fail(ErrorCode::IoError, L"cannot open the image's file to replace it", target->wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    FILE_DISPOSITION_INFO_EX ex{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    if (!SetFileInformationByHandle(file.get(), FileDispositionInfoEx, &ex, sizeof(ex))) {
        FILE_DISPOSITION_INFO plain{TRUE};
        SetFileAttributesW(target->c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!SetFileInformationByHandle(file.get(), FileDispositionInfo, &plain, sizeof(plain))) {
            return fail(ErrorCode::IoError, L"cannot remove the image's file", target->wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
        }
    }
    return {};
}

Result<void> replaceImageFile(const std::filesystem::path& mountDir, std::wstring_view relative, std::string_view bytes) {
    auto target = resolveImagePath(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (auto removed = unlinkImageFile(mountDir, relative); !removed) {
        return removed;
    }
    enableBackupRestore();
    UniqueHandle file{CreateFileW(target->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file) {
        return fail(ErrorCode::IoError, L"cannot create the file in the image", target->wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    std::size_t done = 0;
    while (done < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 24));
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data() + done, chunk, &written, nullptr) || written != chunk) {
            return fail(ErrorCode::IoError, L"cannot write the file in the image", target->wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
        }
        done += written;
    }
    return {};
}

Result<void> replaceImageFileFrom(const std::filesystem::path& mountDir, std::wstring_view relative,
                                  const std::filesystem::path& source, std::uint32_t attributes, const TaskContext& task) {
    auto target = resolveImagePath(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    UniqueHandle in{CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    LARGE_INTEGER size{};
    if (!in || !GetFileSizeEx(in.get(), &size)) {
        return fail(ErrorCode::IoError, L"cannot read the file to put into the image", source.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    if (auto removed = unlinkImageFile(mountDir, relative); !removed) {
        return removed;
    }
    enableBackupRestore();
    UniqueHandle out{CreateFileW(target->c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!out) {
        return fail(ErrorCode::IoError, L"cannot create the file in the image", target->wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    std::vector<char> buffer(4u << 20);
    std::uint64_t done = 0;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(in.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            return fail(ErrorCode::IoError, L"cannot read the file to put into the image", source.wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
        }
        if (got == 0) {
            break;
        }
        DWORD written = 0;
        if (!WriteFile(out.get(), buffer.data(), got, &written, nullptr) || written != got) {
            return fail(ErrorCode::IoError, L"cannot write the file in the image", target->wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
        }
        done += written;
        task.report(size.QuadPart > 0 ? static_cast<double>(done) / static_cast<double>(size.QuadPart) : 1.0);
    }
    if (attributes != 0) {
        FILE_BASIC_INFO basic{};
        if (GetFileInformationByHandleEx(out.get(), FileBasicInfo, &basic, sizeof(basic))) {
            basic.FileAttributes = attributes;
            SetFileInformationByHandle(out.get(), FileBasicInfo, &basic, sizeof(basic));
        }
    }
    log::info("file", std::format(L"replaced {} ({} bytes)", target->wstring(), done));
    return {};
}

Result<void> writeImageFile(const std::filesystem::path& mountDir, std::wstring_view relative, std::string_view content) {
    if (auto ok = validateImageFile(relative, content.size()); !ok) {
        return ok;
    }
    const auto target = prepareTarget(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (auto written = replaceImageFile(mountDir, relative, content); !written) {
        return written;
    }
    log::info("file", std::format(L"wrote {} ({} bytes)", target->wstring(), content.size()));
    return {};
}

Result<bool> imageFileHas(const std::filesystem::path& mountDir, std::wstring_view relative, std::string_view content) {
    if (auto ok = validateImageFile(relative, content.size()); !ok) {
        return std::unexpected(ok.error());
    }
    auto target = resolveImagePath(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(*target, ec);
    if (ec || size != content.size()) {
        return false; // missing or different
    }
    std::ifstream in(*target, std::ios::binary);
    std::string data(content.size(), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(in) && data == content;
}

Result<void> copyImageFile(const std::filesystem::path& mountDir, std::wstring_view relative,
                           const std::filesystem::path& source) {
    if (auto ok = validateImageFile(relative, 0); !ok) {
        return ok;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        return fail(ErrorCode::NotFound, L"the file to copy into the image is not there", source.wstring());
    }
    const std::uintmax_t size = std::filesystem::file_size(source, ec);
    if (ec || size > kImageCopyLimit) {
        return fail(ErrorCode::InvalidArgument, L"file is too large to copy into the image", source.wstring());
    }
    const auto target = prepareTarget(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    {
        std::ifstream in(source, std::ios::binary);
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            return fail(ErrorCode::IoError, L"cannot read the file to copy into the image", source.wstring());
        }
    }
    if (auto written = replaceImageFile(mountDir, relative, bytes); !written) {
        return written;
    }
    log::info("file", std::format(L"copied {} -> {} ({} bytes)", source.wstring(), target->wstring(), size));
    return {};
}

Result<void> writeImageCopy(const std::filesystem::path& mountDir, std::wstring_view relative, std::string_view bytes) {
    if (auto ok = validateImageFile(relative, 0); !ok) {
        return ok;
    }
    if (bytes.size() > kImageCopyLimit) {
        return fail(ErrorCode::InvalidArgument, L"file is too large to copy into the image", std::wstring(relative));
    }
    const auto target = prepareTarget(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (auto written = replaceImageFile(mountDir, relative, bytes); !written) {
        return written;
    }
    log::info("file", std::format(L"wrote {} ({} bytes)", target->wstring(), bytes.size()));
    return {};
}

// ---- D-051 ----------------------------------------------------------------------------------

namespace {

// What only Windows may put there (lower case, backslashes, no trailing slash).
constexpr std::array<std::wstring_view, 8> kForbidden = {
    L"windows\\system32\\config", L"windows\\winsxs",       L"windows\\servicing", L"program files\\windowsapps",
    L"boot",                      L"system volume information", L"$recycle.bin",  L"windows\\system32\\drivers",
};

bool under(std::wstring_view path, std::wstring_view folder) {
    return path == folder || (path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == L'\\');
}

} // namespace

Result<void> validateTreeTarget(std::wstring_view relative) {
    ComponentRecipe asRecipe;
    asRecipe.paths.emplace_back(relative);
    if (auto ok = validateComponentRecipe(asRecipe); !ok) {
        return ok;
    }
    std::wstring path = normalized(relative);
    while (!path.empty() && path.back() == L'\\') {
        path.pop_back();
    }
    if (path.empty()) {
        return fail(ErrorCode::InvalidArgument, L"a place in the image is needed, not its root", std::wstring(relative));
    }
    for (const auto folder : kForbidden) {
        if (under(path, folder) || under(folder, path)) {
            return fail(ErrorCode::InvalidArgument, L"only Windows may write there", std::wstring(relative));
        }
    }
    return {};
}

bool treeTargetRisky(std::wstring_view relative) {
    const std::wstring path = normalized(relative);
    return under(path, L"windows") || under(path, L"program files") || under(path, L"program files (x86)");
}

std::uint64_t treeSize(const std::filesystem::path& source) {
    return treeBytes(source);
}

Result<std::uint64_t> copyImageTree(const std::filesystem::path& mountDir, std::wstring_view relative,
                                    const std::filesystem::path& source, const TaskContext& task) {
    if (auto ok = validateTreeTarget(relative); !ok) {
        return std::unexpected(ok.error());
    }
    std::error_code ec;
    const bool file = std::filesystem::is_regular_file(source, ec);
    if (!file && !std::filesystem::is_directory(source, ec)) {
        return fail(ErrorCode::NotFound, L"the file or folder to copy is not there", source.wstring());
    }
    auto target = resolveImagePath(mountDir, relative); // refuses a link on the way
    if (!target) {
        return std::unexpected(target.error());
    }
    const std::uint64_t total = std::max<std::uint64_t>(treeSize(source), 1);
    std::uint64_t done = 0;
    auto copyOne = [&](const std::filesystem::path& from, const std::filesystem::path& to) -> Result<void> {
        if (auto r = task.cancel.check(to.wstring()); !r) {
            return r;
        }
        std::filesystem::create_directories(to.parent_path(), ec);
        if (const DWORD a = GetFileAttributesW(to.c_str()); a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(to.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        }
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot copy into the image", to.wstring(), ec.value());
        }
        done += treeBytes(from);
        task.report(static_cast<double>(done) / static_cast<double>(total), L"copy");
        return {};
    };
    if (file) {
        if (auto r = copyOne(source, *target); !r) {
            return std::unexpected(r.error());
        }
    } else {
        std::filesystem::create_directories(*target, ec);
        for (auto it = std::filesystem::recursive_directory_iterator(source, ec); !ec && it != std::filesystem::end(it);
             it.increment(ec)) {
            // A link inside the source is copied as what it points at only when it is a plain file.
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const auto rel = std::filesystem::relative(it->path(), source, ec);
            // resolveImagePath checked the target folder only: a sub-folder the source shares with
            // the image may be a junction there ("Application Data" under Users\Default points at
            // the HOST's profile). Nothing is written through one. Folders only: a file of a mounted
            // WIM is a reparse point of the WIM's own until it is written.
            std::filesystem::path to = *target;
            for (const auto& part : rel.parent_path()) {
                to /= part;
                if (isReparsePoint(to)) {
                    return fail(ErrorCode::InvalidArgument, L"a folder in the image is a link (it may leave the image)",
                                to.wstring());
                }
            }
            to /= rel.filename();
            if (auto r = copyOne(it->path(), to); !r) {
                return std::unexpected(r.error());
            }
        }
        if (ec) {
            return fail(ErrorCode::IoError, L"could not read the folder to copy", source.wstring(), ec.value());
        }
    }
    log::info("file", std::format(L"copied {} -> {} ({} bytes)", source.wstring(), target->wstring(), done));
    return done;
}

} // namespace wl::core
