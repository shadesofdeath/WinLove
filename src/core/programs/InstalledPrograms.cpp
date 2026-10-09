#include "core/programs/InstalledPrograms.h"

#include "base/Text.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <regex>
#include <set>

namespace wl::core {

namespace {

std::wstring readString(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return std::wstring(text::trim(value));
}

DWORD readDword(HKEY key, const wchar_t* name) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD) {
        return 0;
    }
    return value;
}

void readUninstall(HKEY root, REGSAM view, bool perUser, std::vector<InstalledProgram>& out) {
    HKEY uninstall = nullptr;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &uninstall) !=
        ERROR_SUCCESS) {
        return;
    }
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(uninstall, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY entry = nullptr;
        if (RegOpenKeyExW(uninstall, name, 0, KEY_READ | view, &entry) != ERROR_SUCCESS) {
            continue;
        }
        InstalledProgram p;
        p.key = name;
        p.name = readString(entry, L"DisplayName");
        p.publisher = readString(entry, L"Publisher");
        p.version = readString(entry, L"DisplayVersion");
        p.perUser = perUser;
        const bool listed = listedProgram(p.name, readDword(entry, L"SystemComponent"), readString(entry, L"ParentKeyName"),
                                          readString(entry, L"ReleaseType"));
        RegCloseKey(entry);
        if (listed) {
            out.push_back(std::move(p));
        }
    }
    RegCloseKey(uninstall);
}

bool isWordVersion(std::wstring_view w) {
    // "24.08", "v1.2", "2024", "1.0.0-beta": digits with dots, an optional leading v.
    if (w.starts_with(L'v') || w.starts_with(L'V')) {
        w.remove_prefix(1);
    }
    if (w.empty() || !std::iswdigit(w.front())) {
        return false;
    }
    return std::ranges::all_of(w, [](wchar_t c) { return std::iswdigit(c) || c == L'.' || c == L'-' || c == L'_'; }) ||
           w.find(L'.') != std::wstring_view::npos;
}

} // namespace

bool listedProgram(std::wstring_view displayName, std::uint32_t systemComponent, std::wstring_view parentKey,
                   std::wstring_view releaseType) {
    if (displayName.empty() || systemComponent == 1 || !parentKey.empty()) {
        return false;
    }
    const std::wstring type = text::lower(releaseType);
    return type != L"update" && type != L"hotfix" && type != L"security update" && type != L"service pack";
}

std::wstring nameArchitecture(std::wstring_view nameView) {
    const std::wstring name = text::lower(nameView);
    if (name.find(L"x64") != std::wstring::npos || name.find(L"64-bit") != std::wstring::npos ||
        name.find(L"64 bit") != std::wstring::npos || name.find(L"amd64") != std::wstring::npos) {
        return L"X64";
    }
    if (name.find(L"x86") != std::wstring::npos || name.find(L"32-bit") != std::wstring::npos ||
        name.find(L"32 bit") != std::wstring::npos) {
        return L"X86";
    }
    return {};
}

std::wstring normalizeProgramName(std::wstring_view nameView) {
    std::wstring name = text::lower(nameView);
    // Parenthesized parts: an architecture, a locale, a version — none is the name.
    static const std::wregex brackets{LR"(\([^)]*\)|\[[^\]]*\])"};
    name = std::regex_replace(name, brackets, L" ");
    std::wstring out;
    std::wstring word;
    auto flush = [&] {
        static constexpr std::array kDrop{L"x64", L"x86", L"amd64", L"arm64", L"64-bit", L"32-bit", L"bit", L"version"};
        if (!word.empty() && !isWordVersion(word) &&
            std::ranges::none_of(kDrop, [&](const wchar_t* d) { return word == d; })) {
            for (const wchar_t c : word) {
                if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c > 0x7F) {
                    out += c;
                }
            }
        }
        word.clear();
    };
    for (const wchar_t c : name) {
        if (std::iswspace(c) || c == L',') {
            flush();
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

std::wstring normalizePublisher(std::wstring_view publisher) {
    std::wstring name = text::lower(publisher);
    static const std::wregex legal{
        LR"(\b(incorporated|inc|corporation|corp|company|co|llc|ltd|limited|gmbh|ag|sa|srl|bv|pty|plc|foundation|software|team)\b\.?)"};
    name = std::regex_replace(name, legal, L" ");
    std::wstring out;
    for (const wchar_t c : name) {
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c > 0x7F) {
            out += c;
        }
    }
    return out;
}

std::vector<InstalledProgram> installedPrograms() {
    std::vector<InstalledProgram> all;
    readUninstall(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, false, all);
    readUninstall(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, false, all);
    readUninstall(HKEY_CURRENT_USER, 0, true, all);
    // The same program in both views or for the user and the machine: once.
    std::set<std::wstring> seen;
    std::vector<InstalledProgram> out;
    for (auto& p : all) {
        if (seen.insert(text::lower(p.key) + L"|" + text::lower(p.name)).second) {
            out.push_back(std::move(p));
        }
    }
    return out;
}

std::vector<InstalledMatch> matchInstalled(const WingetIndex& index, const std::vector<InstalledProgram>& installed) {
    std::map<std::wstring, InstalledMatch> byId; // lower-case id → the match
    for (const auto& p : installed) {
        std::optional<InstalledMatch> match;
        // 1. The Uninstall key: an MSI product code, or the name the installer gave it.
        if (const auto byCode = index.byProductCode(p.key); byCode.size() == 1) {
            match = InstalledMatch{p, byCode.front(), true};
        }
        // 2. The normalized name (with the architecture, as winget keeps both forms), the
        //    publisher deciding between several.
        if (!match) {
            const std::wstring norm = normalizeProgramName(p.name);
            if (norm.size() < 2) {
                continue;
            }
            std::vector<std::wstring> names{norm};
            if (const auto arch = nameArchitecture(p.name); !arch.empty()) {
                names.push_back(norm + L"(" + arch + L")");
            }
            const auto found = index.byNormalizedName(names, normalizePublisher(p.publisher));
            std::vector<WingetPackage> samePublisher;
            for (const auto& [package, publisherMatches] : found) {
                if (publisherMatches) {
                    samePublisher.push_back(package);
                }
            }
            if (samePublisher.size() == 1) {
                match = InstalledMatch{p, samePublisher.front(), false};
            } else if (found.size() == 1 && p.publisher.empty()) {
                match = InstalledMatch{p, found.front().first, false};
            }
        }
        if (match) {
            const std::wstring key = text::lower(match->package.id);
            if (!byId.contains(key) || (match->byProductCode && !byId[key].byProductCode)) {
                byId[key] = std::move(*match);
            }
        }
    }
    std::vector<InstalledMatch> out;
    for (auto& [id, m] : byId) {
        out.push_back(std::move(m));
    }
    std::ranges::sort(out, [](const InstalledMatch& a, const InstalledMatch& b) {
        return text::fold(a.package.name) < text::fold(b.package.name);
    });
    return out;
}

} // namespace wl::core
