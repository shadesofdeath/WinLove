#include "core/image/Services.h"

#include "base/Log.h"
#include "core/image/OfflineHive.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <set>

namespace wl::core {

namespace {

constexpr std::uint32_t kWin32Service = 0x10 | 0x20; // own / share process (templates 0x50/0x60 included)

bool iequals(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                                        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring hivePath(const std::filesystem::path& mountDir) {
    return (mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM").wstring();
}

std::wstring controlSet(const OfflineHive& hive) {
    std::uint32_t current = 1;
    if (auto select = RegKey::open(hive.root(), L"Select")) {
        current = select->dword(L"Current").value_or(1);
    }
    return std::format(L"ControlSet{:03}\\Services", current);
}

// Resolves "@file,-id" against the image's files; plain names pass through.
std::wstring resolveText(const std::wstring& value, const std::filesystem::path& mountDir) {
    if (value.empty() || value.front() != L'@') {
        return value;
    }
    const std::wstring source = offlineResourcePath(value, mountDir);
    wchar_t buffer[512] = {};
    if (SUCCEEDED(SHLoadIndirectString(source.c_str(), buffer, static_cast<UINT>(std::size(buffer)), nullptr))) {
        return buffer;
    }
    return {};
}

// The image's UI language: the System32 "ll-CC" folder that holds kernel32.dll.mui.
std::wstring imageLanguage(const std::filesystem::path& mountDir) {
    std::error_code ec;
    const auto system32 = mountDir / L"Windows" / L"System32";
    for (auto it = std::filesystem::directory_iterator(system32, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        if (name.size() == 5 && name[2] == L'-' && it->is_directory(ec) &&
            std::filesystem::exists(it->path() / L"kernel32.dll.mui", ec)) {
            return name;
        }
    }
    return {};
}

// While alive, MUI lookups on this thread prefer `language` (the image's), then the defaults.
class PreferredUiLanguage {
public:
    explicit PreferredUiLanguage(const std::wstring& language) {
        if (language.empty()) {
            return;
        }
        std::wstring list = language;
        list.push_back(L'\0'); // double-NUL terminated list
        m_set = SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, list.c_str(), nullptr) != FALSE;
    }
    ~PreferredUiLanguage() {
        if (m_set) {
            SetThreadPreferredUILanguages(0, nullptr, nullptr); // back to the process defaults
        }
    }
    PreferredUiLanguage(const PreferredUiLanguage&) = delete;
    PreferredUiLanguage& operator=(const PreferredUiLanguage&) = delete;

private:
    bool m_set = false;
};

StartType startOf(const RegKey& key) {
    switch (key.dword(L"Start").value_or(3)) {
    case 0: return StartType::Boot;
    case 1: return StartType::System;
    case 2: return key.dword(L"DelayedAutostart").value_or(0) != 0 ? StartType::AutoDelayed : StartType::Auto;
    case 4: return StartType::Disabled;
    default: return StartType::Manual;
    }
}

} // namespace

const wchar_t* startTypeKey(StartType type) noexcept {
    switch (type) {
    case StartType::Boot: return L"boot";
    case StartType::System: return L"system";
    case StartType::Auto: return L"auto";
    case StartType::AutoDelayed: return L"autoDelayed";
    case StartType::Manual: return L"manual";
    case StartType::Disabled: return L"disabled";
    }
    return L"manual";
}

std::optional<StartType> startTypeFromKey(std::wstring_view key) noexcept {
    for (const auto type : {StartType::Boot, StartType::System, StartType::Auto, StartType::AutoDelayed,
                            StartType::Manual, StartType::Disabled}) {
        if (key == startTypeKey(type)) {
            return type;
        }
    }
    return std::nullopt;
}

std::wstring offlineResourcePath(std::wstring_view value, const std::filesystem::path& mountDir) {
    std::wstring out(value);
    const std::pair<std::wstring_view, std::filesystem::path> vars[] = {
        {L"%SystemRoot%", mountDir / L"Windows"},
        {L"%windir%", mountDir / L"Windows"},
        {L"%ProgramFiles%", mountDir / L"Program Files"},
        {L"%ProgramFiles(x86)%", mountDir / L"Program Files (x86)"},
        {L"%SystemDrive%", mountDir},
    };
    for (const auto& [name, path] : vars) {
        for (std::size_t i = 0; i + name.size() <= out.size(); ++i) {
            if (iequals(std::wstring_view(out).substr(i, name.size()), name)) {
                std::wstring target = path.wstring();
                if (!target.empty() && target.back() == L'\\') {
                    target.pop_back();
                }
                out.replace(i, name.size(), target);
                i += target.size();
            }
        }
    }
    // "@system32\x.dll,-1" (relative) → under the image's Windows folder.
    if (out.size() > 1 && out.front() == L'@' && std::filesystem::path(out.substr(1)).is_relative()) {
        out = L"@" + (mountDir / L"Windows" / out.substr(1)).wstring();
    }
    return out;
}

Result<std::vector<ServiceEntry>> readServices(const std::filesystem::path& mountDir) {
    // A Turkish image on an English host: resolve names from the image's tr-TR .mui files.
    const PreferredUiLanguage language(imageLanguage(mountDir));
    auto hive = OfflineHive::load(hivePath(mountDir));
    if (!hive) {
        return std::unexpected(hive.error());
    }
    std::vector<ServiceEntry> services;
    {
        auto root = RegKey::open(hive->root(), controlSet(*hive));
        if (!root) {
            return std::unexpected(root.error());
        }
        for (const auto& name : root->subkeys()) {
            auto key = RegKey::open(root->get(), name);
            if (!key) {
                continue;
            }
            const std::uint32_t type = key->dword(L"Type").value_or(0);
            if ((type & kWin32Service) == 0) {
                continue;
            }
            ServiceEntry s;
            s.name = name;
            s.type = type;
            s.start = startOf(*key);
            s.displayName = resolveText(key->string(L"DisplayName").value_or(L""), mountDir);
            if (s.displayName.empty()) {
                s.displayName = name;
            }
            s.description = resolveText(key->string(L"Description").value_or(L""), mountDir);
            s.imagePath = key->string(L"ImagePath").value_or(L"");
            s.account = key->string(L"ObjectName").value_or(L"");
            s.dependsOn = key->multiString(L"DependOnService");
            services.push_back(std::move(s));
        }
    } // all keys closed before the hive unloads
    std::ranges::sort(services, [](const ServiceEntry& a, const ServiceEntry& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.displayName.c_str(), -1,
                               b.displayName.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    log::info("reg", std::format(L"{} services read from {}", services.size(), mountDir.wstring()));
    return services;
}

std::vector<RegistryWrite> serviceStartWrites(const std::wstring& name, StartType start) {
    std::uint32_t value = 3;
    switch (start) {
    case StartType::Boot: value = 0; break;
    case StartType::System: value = 1; break;
    case StartType::Auto:
    case StartType::AutoDelayed: value = 2; break;
    case StartType::Manual: value = 3; break;
    case StartType::Disabled: value = 4; break;
    }
    auto dword = [&](const wchar_t* valueName, std::uint32_t data) {
        RegistryWrite w;
        w.key = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\" + name;
        w.name = valueName;
        w.type = REG_DWORD;
        w.data.resize(4);
        std::memcpy(w.data.data(), &data, 4);
        return w;
    };
    return {dword(L"Start", value), dword(L"DelayedAutostart", start == StartType::AutoDelayed ? 1u : 0u)};
}

Result<void> setServiceStart(const std::filesystem::path& mountDir, const std::wstring& name, StartType start) {
    if (name.empty() || name.find(L'\\') != std::wstring::npos) {
        return fail(ErrorCode::InvalidArgument, L"bad service name", name);
    }
    {
        auto services = readServices(mountDir);
        if (!services) {
            return std::unexpected(services.error());
        }
        if (std::ranges::none_of(*services, [&](const ServiceEntry& s) { return _wcsicmp(s.name.c_str(), name.c_str()) == 0; })) {
            return fail(ErrorCode::NotFound, L"no such service in the image", name);
        }
    }
    OfflineRegistry registry(mountDir);
    for (const auto& write : serviceStartWrites(name, start)) {
        if (auto r = registry.apply(write); !r) {
            return r;
        }
    }
    log::info("reg", std::format(L"service {} start → {}", name, startTypeKey(start)));
    return {};
}

std::vector<std::wstring> dependentsOf(const std::vector<ServiceEntry>& services, std::wstring_view name) {
    std::vector<std::wstring> result;
    std::vector<std::wstring> frontier{std::wstring(name)};
    std::set<std::wstring> seen;
    while (!frontier.empty()) {
        const std::wstring current = std::move(frontier.back());
        frontier.pop_back();
        for (const auto& s : services) {
            const bool depends = std::ranges::any_of(s.dependsOn, [&](const std::wstring& d) { return iequals(d, current); });
            if (depends && seen.insert(s.name).second) {
                result.push_back(s.name);
                frontier.push_back(s.name);
            }
        }
    }
    return result;
}

} // namespace wl::core
