#include "core/system/Files.h"

#include "core/system/BackupFiles.h"

#include <windows.h>

namespace wl::core {

std::filesystem::path tempFolder() {
    wchar_t temp[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
    if (length == 0 || length > MAX_PATH) {
        std::error_code ec;
        return std::filesystem::temp_directory_path(ec);
    }
    return std::filesystem::path(temp);
}

bool isReparsePoint(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::uint64_t treeBytes(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        const auto size = std::filesystem::file_size(path, ec);
        return ec ? 0 : static_cast<std::uint64_t>(size);
    }
    // Folders through BackupFiles: never through a junction, and not cut short by one entry that
    // cannot be read (recursive_directory_iterator stops the whole walk there).
    return backupFolderSize(path);
}

Result<void> swapIntoPlace(const std::filesystem::path& original, const std::filesystem::path& fresh,
                           const std::filesystem::path& destination) {
    std::error_code ec;
    const std::filesystem::path old = original.wstring() + L".old";
    std::filesystem::remove(old, ec);
    std::filesystem::rename(original, old, ec);
    if (ec) {
        const auto code = ec.value();
        std::filesystem::remove(fresh, ec);
        return fail(ErrorCode::IoError, L"cannot replace the file (in use?)", original.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(code)));
    }
    std::filesystem::rename(fresh, destination, ec);
    if (ec) {
        const auto code = ec.value();
        std::filesystem::rename(old, original, ec); // put the original back
        std::filesystem::remove(fresh, ec);
        return fail(ErrorCode::IoError, L"cannot move the new file into place", destination.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(code)));
    }
    std::filesystem::remove(old, ec);
    return {};
}

} // namespace wl::core
