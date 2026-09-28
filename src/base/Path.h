#pragma once
// Paths handed to Windows imaging APIs must be absolute and use backslashes only: DISM rejects
// mixed separators ("C:/Lab/iso\sources/install.wim" → 0x80070057 Invalid ImageFilePath).
#include <filesystem>
#include <system_error>

namespace wl {

// Absolute, lexically normal, preferred separators. Never throws (falls back to the input).
[[nodiscard]] inline std::filesystem::path nativePath(const std::filesystem::path& path) {
    if (path.empty()) {
        return path;
    }
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    return absolute.lexically_normal().make_preferred();
}

} // namespace wl
