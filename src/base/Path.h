#pragma once
// Paths handed to Windows imaging APIs must be absolute and use backslashes only: DISM rejects
// mixed separators ("C:/Lab/iso\sources/install.wim" → 0x80070057 Invalid ImageFilePath).
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
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

// "\\?\C:\x" → "C:\x", "\\?\UNC\srv\share" → "\\srv\share": the form people read and the rest of
// the app compares. DISM and wimgapi answer with the prefix when the tool that mounted gave it one
// (2026-11-11, another tool's mount). Kept when the plain path would be too long to open without it.
[[nodiscard]] inline std::filesystem::path withoutLongPathPrefix(const std::filesystem::path& path) {
    const std::wstring& s = path.native();
    constexpr std::wstring_view kPrefix = L"\\\\?\\";
    constexpr std::wstring_view kUnc = L"\\\\?\\UNC\\";
    std::wstring plain;
    if (s.size() > kUnc.size() && _wcsnicmp(s.c_str(), kUnc.data(), kUnc.size()) == 0) {
        plain = L"\\\\" + s.substr(kUnc.size());
    } else if (s.size() > kPrefix.size() + 2 && s.compare(0, kPrefix.size(), kPrefix) == 0 && s[kPrefix.size() + 1] == L':') {
        plain = s.substr(kPrefix.size());
    } else {
        return path;
    }
    constexpr std::size_t kMaxPlain = 247; // MAX_PATH less a 8.3 file name: a folder Win32 still opens
    return plain.size() <= kMaxPlain ? std::filesystem::path(std::move(plain)) : path;
}

} // namespace wl
