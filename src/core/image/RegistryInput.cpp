#include "core/image/RegistryInput.h"

#include <windows.h>

#include <cstring>
#include <cwctype>
#include <format>
#include <optional>
#include <vector>

namespace wl::core {

namespace {

std::wstring_view trimmed(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::uint8_t> bytesOf(std::wstring_view text) {
    std::vector<std::uint8_t> out(text.size() * sizeof(wchar_t));
    if (!text.empty()) {
        std::memcpy(out.data(), text.data(), out.size());
    }
    return out;
}

void appendText(std::vector<std::uint8_t>& out, std::wstring_view text) {
    const auto bytes = bytesOf(text);
    out.insert(out.end(), bytes.begin(), bytes.end());
    out.push_back(0);
    out.push_back(0);
}

std::optional<std::uint64_t> number(std::wstring_view text, std::uint64_t max) {
    text = trimmed(text);
    int base = 10;
    if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 20) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const wchar_t c : text) {
        int digit = -1;
        if (c >= L'0' && c <= L'9') {
            digit = c - L'0';
        } else if (base == 16 && c >= L'a' && c <= L'f') {
            digit = c - L'a' + 10;
        } else if (base == 16 && c >= L'A' && c <= L'F') {
            digit = c - L'A' + 10;
        }
        if (digit < 0) {
            return std::nullopt;
        }
        if (value > (max - static_cast<std::uint64_t>(digit)) / static_cast<std::uint64_t>(base)) {
            return std::nullopt;
        }
        value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
    }
    return value;
}

std::optional<std::vector<std::uint8_t>> hexList(std::wstring_view text) {
    std::vector<std::uint8_t> out;
    std::wstring token;
    auto flush = [&]() -> bool {
        if (token.empty()) {
            return true;
        }
        if (token.size() > 2 || !std::iswxdigit(token[0]) || (token.size() == 2 && !std::iswxdigit(token[1]))) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(std::wcstoul(token.c_str(), nullptr, 16)));
        token.clear();
        return true;
    };
    for (const wchar_t c : text) {
        if (c == L' ' || c == L',' || c == L'\t') {
            if (!flush()) {
                return std::nullopt;
            }
        } else {
            token.push_back(c);
        }
    }
    if (!flush()) {
        return std::nullopt;
    }
    return out;
}

std::wstring textOf(const std::vector<std::uint8_t>& data) {
    std::wstring text(data.size() / sizeof(wchar_t), L'\0');
    if (!text.empty()) {
        std::memcpy(text.data(), data.data(), text.size() * sizeof(wchar_t));
    }
    return text;
}

} // namespace

Result<RegistryWrite> registryWriteFromInput(std::wstring_view key, std::wstring_view name, RegValueType type,
                                             std::wstring_view data) {
    RegistryWrite w;
    w.key = normalizeRegistryKey(trimmed(key));
    if (w.key.empty() || w.key.find(L'\\') == std::wstring::npos) {
        return fail(ErrorCode::ParseError, L"bad registry key", std::wstring(key));
    }
    if (w.key.find(L"::") != std::wstring::npos) {
        return fail(ErrorCode::ParseError, L"\"::\" is not allowed in a key", std::wstring(key));
    }
    w.name = std::wstring(name);
    switch (type) {
    case RegValueType::String:
    case RegValueType::ExpandString:
        w.type = type == RegValueType::String ? REG_SZ : REG_EXPAND_SZ;
        w.data = bytesOf(data);
        w.data.push_back(0);
        w.data.push_back(0);
        return w;
    case RegValueType::MultiString: {
        w.type = REG_MULTI_SZ;
        // An empty string would end the list early: empty parts are dropped.
        std::wstring part;
        auto flush = [&] {
            if (!part.empty()) {
                appendText(w.data, part);
            }
            part.clear();
        };
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] == L';' && i + 1 < data.size() && data[i + 1] == L';') {
                part.push_back(L';');
                ++i;
            } else if (data[i] == L';') {
                flush();
            } else {
                part.push_back(data[i]);
            }
        }
        flush();
        w.data.push_back(0);
        w.data.push_back(0);
        return w;
    }
    case RegValueType::Dword: {
        const auto v = number(data, 0xFFFFFFFFull);
        if (!v) {
            return fail(ErrorCode::ParseError, L"not a 32-bit number", std::wstring(data));
        }
        const auto d = static_cast<std::uint32_t>(*v);
        w.type = REG_DWORD;
        w.data.resize(4);
        std::memcpy(w.data.data(), &d, 4);
        return w;
    }
    case RegValueType::Qword: {
        const auto v = number(data, ~0ull);
        if (!v) {
            return fail(ErrorCode::ParseError, L"not a 64-bit number", std::wstring(data));
        }
        w.type = REG_QWORD;
        w.data.resize(8);
        std::memcpy(w.data.data(), &*v, 8);
        return w;
    }
    case RegValueType::Binary: {
        auto bytes = hexList(data);
        if (!bytes) {
            return fail(ErrorCode::ParseError, L"not hex bytes", std::wstring(data));
        }
        w.type = REG_BINARY;
        w.data = std::move(*bytes);
        return w;
    }
    case RegValueType::DeleteValue:
        w.kind = RegistryWrite::Kind::DeleteValue;
        return w;
    case RegValueType::DeleteKey:
        w.kind = RegistryWrite::Kind::DeleteKey;
        w.name.clear();
        return w;
    }
    return fail(ErrorCode::InvalidArgument, L"unknown value type", L"");
}

RegValueInput registryWriteInput(const RegistryWrite& write) {
    switch (write.kind) {
    case RegistryWrite::Kind::DeleteValue: return {RegValueType::DeleteValue, {}};
    case RegistryWrite::Kind::DeleteKey: return {RegValueType::DeleteKey, {}};
    case RegistryWrite::Kind::CreateKey: return {RegValueType::Binary, {}};
    case RegistryWrite::Kind::Set: break;
    }
    const auto& d = write.data;
    if ((write.type == REG_SZ || write.type == REG_EXPAND_SZ) && d.size() % 2 == 0) {
        std::wstring text = textOf(d);
        while (!text.empty() && text.back() == L'\0') {
            text.pop_back();
        }
        return {write.type == REG_SZ ? RegValueType::String : RegValueType::ExpandString, std::move(text)};
    }
    if (write.type == REG_MULTI_SZ && d.size() % 2 == 0) {
        const std::wstring all = textOf(d);
        std::wstring out;
        std::size_t start = 0;
        bool first = true;
        while (start < all.size()) {
            const std::size_t end = all.find(L'\0', start);
            const std::wstring part = all.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (part.empty()) {
                break; // the closing empty string
            }
            if (!first) {
                out += L';';
            }
            first = false;
            for (const wchar_t c : part) {
                out += c == L';' ? std::wstring(L";;") : std::wstring(1, c);
            }
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
        return {RegValueType::MultiString, std::move(out)};
    }
    if (write.type == REG_DWORD && d.size() == 4) {
        std::uint32_t v = 0;
        std::memcpy(&v, d.data(), 4);
        return {RegValueType::Dword, std::format(L"0x{:x}", v)};
    }
    if (write.type == REG_QWORD && d.size() == 8) {
        std::uint64_t v = 0;
        std::memcpy(&v, d.data(), 8);
        return {RegValueType::Qword, std::format(L"0x{:x}", v)};
    }
    std::wstring hex;
    for (const auto b : d) {
        if (!hex.empty()) {
            hex += L' ';
        }
        hex += std::format(L"{:02x}", b);
    }
    return {RegValueType::Binary, std::move(hex)};
}

} // namespace wl::core
