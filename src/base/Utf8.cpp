#include "base/Utf8.h"

#include <windows.h>

#include <cstring>

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

std::wstring decodeText(std::string_view bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring text((bytes.size() - 2) / 2, L'\0');
        std::memcpy(text.data(), bytes.data() + 2, text.size() * sizeof(wchar_t));
        return text;
    }
    const bool bom = bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
                     static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF;
    const std::string_view body = bytes.substr(bom ? 3 : 0);
    if (body.empty()) {
        return {};
    }
    const int size = static_cast<int>(body.size());
    int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, body.data(), size, nullptr, 0);
    const UINT cp = n > 0 ? CP_UTF8 : CP_ACP;
    n = ::MultiByteToWideChar(cp, 0, body.data(), size, nullptr, 0);
    std::wstring text(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    ::MultiByteToWideChar(cp, 0, body.data(), size, text.data(), n);
    return text;
}

} // namespace wl::utf8
