#include "core/system/DeviceClasses.h"

#include <windows.h>

#include <combaseapi.h>
#include <setupapi.h>

#include <cwctype>
#include <map>
#include <mutex>
#include <vector>

namespace wl::core {

namespace {

std::wstring upper(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return out;
}

std::wstring guidText(const GUID& g) {
    wchar_t buffer[64] = {};
    StringFromGUID2(g, buffer, 64);
    return upper(buffer);
}

std::wstring expand(const std::wstring& text) {
    std::wstring out(MAX_PATH * 2, L'\0');
    const DWORD n = ExpandEnvironmentStringsW(text.c_str(), out.data(), static_cast<DWORD>(out.size()));
    if (n == 0 || n > out.size()) {
        return text;
    }
    out.resize(n - 1);
    return out;
}

// "%SystemRoot%\System32\setupapi.dll,-5" → file, index.
void iconFromPath(const std::wstring& path, DeviceClassInfo& info) {
    const auto comma = path.rfind(L',');
    if (comma == std::wstring::npos) {
        info.iconFile = expand(path);
        info.iconIndex = 0;
        return;
    }
    info.iconFile = expand(path.substr(0, comma));
    info.iconIndex = _wtoi(path.c_str() + comma + 1);
}

void readIcon(const GUID& guid, DeviceClassInfo& info) {
    HKEY key = SetupDiOpenClassRegKey(&guid, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, L"IconPath", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 2 &&
        (type == REG_MULTI_SZ || type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, L'\0');
        if (RegQueryValueExW(key, L"IconPath", nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size) == ERROR_SUCCESS) {
            iconFromPath(data.data(), info); // the first string of a multi-string
        }
    }
    if (info.iconFile.empty()) {
        size = 0;
        if (RegQueryValueExW(key, L"Icon", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 2 && type == REG_SZ) {
            std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, L'\0');
            if (RegQueryValueExW(key, L"Icon", nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size) == ERROR_SUCCESS) {
                // A resource of setupapi.dll ("-5"), as Device Manager reads it.
                info.iconFile = expand(L"%SystemRoot%\\System32\\setupapi.dll");
                info.iconIndex = _wtoi(data.data());
            }
        }
    }
    RegCloseKey(key);
}

} // namespace

const DeviceClassInfo& deviceClassInfo(std::wstring_view classGuid, std::wstring_view className) {
    static std::mutex lock;
    static std::map<std::wstring, DeviceClassInfo> cache;
    const std::wstring cacheKey = upper(classGuid) + L"|" + upper(className);
    std::scoped_lock guard(lock);
    if (const auto it = cache.find(cacheKey); it != cache.end()) {
        return it->second;
    }
    DeviceClassInfo info;
    GUID guid{};
    bool known = false;
    if (!classGuid.empty()) {
        known = CLSIDFromString(std::wstring(classGuid).c_str(), &guid) == NOERROR;
    }
    if (!known && !className.empty()) {
        DWORD count = 0;
        GUID found[4]{};
        known = SetupDiClassGuidsFromNameW(std::wstring(className).c_str(), found, 4, &count) && count > 0;
        guid = found[0];
    }
    if (known) {
        info.guid = guidText(guid);
        wchar_t description[LINE_LEN] = {};
        if (SetupDiGetClassDescriptionW(&guid, description, LINE_LEN, nullptr)) {
            info.description = description;
        }
        readIcon(guid, info);
    }
    return cache.emplace(cacheKey, std::move(info)).first->second;
}

} // namespace wl::core
