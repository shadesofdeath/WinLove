#include "base/Text.h"

#include <windows.h>

#include <cwctype>

namespace wl::text {

namespace {

bool isSpace(wchar_t c) noexcept {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

int ordinalCompare(std::wstring_view a, std::wstring_view b) noexcept {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
}

} // namespace

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

std::wstring fold(std::wstring_view text) {
    std::wstring out(text);
    if (out.empty()) {
        return out;
    }
    // Before the lower-casing: the invariant mapping would turn "I" into "i" but leave "ı" alone.
    for (auto& c : out) {
        if (c == L'I' || c == L'İ' || c == L'ı') {
            c = L'i';
        }
    }
    std::wstring lower(out.size(), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, out.data(), static_cast<int>(out.size()),
                                      lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0);
    if (written == static_cast<int>(out.size())) { // marks in the original text rely on equal lengths
        out = std::move(lower);
    }
    for (auto& c : out) {
        switch (c) {
        case L'ç': c = L'c'; break; // ç
        case L'ğ': c = L'g'; break; // ğ
        case L'ö': c = L'o'; break; // ö
        case L'ş': c = L's'; break; // ş
        case L'ü': c = L'u'; break; // ü
        default: break;
        }
    }
    return out;
}

std::wstring upper(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return out;
}

bool iequals(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && ordinalCompare(a, b) == CSTR_EQUAL;
}

bool istartsWith(std::wstring_view text, std::wstring_view prefix) noexcept {
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

bool iendsWith(std::wstring_view text, std::wstring_view suffix) noexcept {
    return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

std::wstring_view trim(std::wstring_view text) noexcept {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && isSpace(static_cast<wchar_t>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(static_cast<wchar_t>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace wl::text
