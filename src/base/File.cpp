#include "base/File.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace wl {

Result<std::string> readFileBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(file, ec);
        return fail(exists ? ErrorCode::IoError : ErrorCode::NotFound, exists ? L"could not read the file" : L"no such file",
                    file.wstring());
    }
    // The whole file in one read when its size is known; byte by byte through the iterator took
    // most of a second for a .reg or catalog of tens of MB (audit BASE-03).
    std::string bytes;
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(file, sizeError);
    if (!sizeError && size > 0 && size <= static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        bytes.resize(static_cast<std::size_t>(size));
        in.read(bytes.data(), static_cast<std::streamsize>(size));
        bytes.resize(static_cast<std::size_t>(in.gcount())); // shorter than announced: what was there
    }
    if (in.good()) {
        // No size (not a plain file), or the file grew meanwhile: the rest, as before.
        bytes.append(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (in.bad()) {
        return fail(ErrorCode::IoError, L"could not read the file", file.wstring());
    }
    return bytes;
}

Result<void> writeFileAtomic(const std::filesystem::path& file, std::string_view bytes) {
    std::filesystem::path temp = file;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::IoError, L"could not create the file", temp.wstring());
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return fail(ErrorCode::IoError, L"could not write the file (disk full?)", temp.wstring());
        }
    }
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return fail(ErrorCode::IoError, L"could not replace the file", file.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    return {};
}

} // namespace wl
