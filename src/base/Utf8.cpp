#include "base/Utf8.h"

#include <windows.h>

namespace wl::utf8 {

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = static_cast<int>(utf8.size());
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, wide.data(), length);
    return wide;
}

std::string fromWide(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = static_cast<int>(wide.size());
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

} // namespace wl::utf8
