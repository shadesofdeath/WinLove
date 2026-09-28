#include "core/io/ByteSource.h"

#include <algorithm>

namespace wl::core {

Result<std::unique_ptr<DiskFile>> DiskFile::open(const std::filesystem::path& path) {
    auto file = std::make_unique<DiskFile>();
    file->m_path = path;
    // FILE_SHARE_READ|WRITE: the source may be open elsewhere (Explorer preview, another tool).
    file->m_handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file->m_handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        const auto code = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ErrorCode::NotFound
                          : error == ERROR_ACCESS_DENIED                                  ? ErrorCode::AccessDenied
                                                                                          : ErrorCode::IoError;
        return fail(code, L"cannot open file", path.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file->m_handle, &size);
    file->m_size = static_cast<std::uint64_t>(size.QuadPart);
    return file;
}

DiskFile::~DiskFile() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
    }
}

Result<void> DiskFile::read(std::uint64_t offset, std::span<std::byte> out) const {
    if (offset + out.size() > m_size) {
        return fail(ErrorCode::IoError, L"read past end of file", m_path.wstring());
    }
    std::size_t done = 0;
    while (done < out.size()) {
        // ReadFile takes 32-bit lengths; read in chunks of at most 64 MiB.
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 64u << 20));
        OVERLAPPED at{};
        const std::uint64_t position = offset + done;
        at.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
        at.OffsetHigh = static_cast<DWORD>(position >> 32);
        DWORD read = 0;
        if (!ReadFile(m_handle, out.data() + done, chunk, &read, &at) || read == 0) {
            return fail(ErrorCode::IoError, L"read failed", m_path.wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
        }
        done += read;
    }
    return {};
}

} // namespace wl::core
