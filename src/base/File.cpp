#include "base/File.h"

#include <windows.h>

#include <fstream>
#include <iterator>

namespace wl {

Result<std::string> readFileBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(file, ec);
        return fail(exists ? ErrorCode::IoError : ErrorCode::NotFound, exists ? L"could not read the file" : L"no such file",
                    file.wstring());
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
