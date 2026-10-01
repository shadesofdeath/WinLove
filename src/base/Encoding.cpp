#include "base/Encoding.h"

namespace wl {

std::vector<std::uint8_t> base64Decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0) {
            if (c == '=') {
                break;
            }
            return {};
        }
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::wstring hexLower(std::span<const std::uint8_t> bytes) {
    constexpr wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

std::wstring xmlEscape(std::wstring_view text, bool apostrophe) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'':
            if (apostrophe) {
                out += L"&apos;";
            } else {
                out.push_back(c);
            }
            break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

} // namespace wl
