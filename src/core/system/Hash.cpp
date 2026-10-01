#include "core/system/Hash.h"

#include <cwctype>
#include <fstream>
#include <optional>
#include <vector>

namespace wl::core {

std::wstring normalizeSha256(std::wstring_view text) {
    std::wstring t(text);
    for (auto& c : t) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    if (const auto colon = t.find(L':'); colon != std::wstring::npos) {
        t = t.substr(colon + 1); // "SHA256: …"
    }
    std::wstring hex;
    for (const wchar_t c : t) {
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')) {
            hex.push_back(c);
        } else if (c != L' ' && c != L'-' && c != L'\t' && c != L'\r' && c != L'\n') {
            return {};
        }
    }
    return hex.size() == 64 ? hex : std::wstring();
}

} // namespace wl::core
