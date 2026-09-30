#include "core/image/ImageFiles.h"

#include "base/Log.h"
#include "core/image/SystemComponents.h"

#include <windows.h>

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

Result<void> writeImageFile(const std::filesystem::path& mountDir, std::wstring_view relative, std::string_view content) {
    if (auto ok = validateImageFile(relative, content.size()); !ok) {
        return ok;
    }
    const auto target = prepareTarget(mountDir, relative);
    if (!target) {
        return std::unexpected(target.error());
    }
    {
        std::ofstream out(*target, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            return fail(ErrorCode::IoError, L"cannot write file", target->wstring(), static_cast<std::int32_t>(GetLastError()));
        }
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
    std::filesystem::copy_file(source, *target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot copy file into the image", target->wstring(), ec.value());
    }
    SetFileAttributesW(target->c_str(), FILE_ATTRIBUTE_NORMAL); // a read-only source stays replaceable in the image
    log::info("file", std::format(L"copied {} -> {} ({} bytes)", source.wstring(), target->wstring(), size));
    return {};
}

} // namespace wl::core
