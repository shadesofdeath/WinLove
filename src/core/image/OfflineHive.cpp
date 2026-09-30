#include "core/image/OfflineHive.h"

#include "base/Log.h"
#include "core/system/Privileges.h"

#include <atomic>
#include <format>
#include <thread>

namespace wl::core {

namespace {

Error regError(LSTATUS status, std::wstring what, std::wstring detail) {
    const auto code = status == ERROR_ACCESS_DENIED || status == ERROR_PRIVILEGE_NOT_HELD ? ErrorCode::AccessDenied
                      : status == ERROR_FILE_NOT_FOUND                                  ? ErrorCode::NotFound
                                                                                        : ErrorCode::IoError;
    return Error{code, std::move(what), std::move(detail), static_cast<std::int32_t>(HRESULT_FROM_WIN32(status))};
}

} // namespace

RegKey& RegKey::operator=(RegKey&& other) noexcept {
    if (this != &other) {
        if (m_key) {
            RegCloseKey(m_key);
        }
        m_key = std::exchange(other.m_key, nullptr);
    }
    return *this;
}

RegKey::~RegKey() {
    if (m_key) {
        RegCloseKey(m_key);
    }
}

Result<RegKey> RegKey::open(HKEY parent, const std::wstring& path, bool write) {
    HKEY key = nullptr;
    const REGSAM access = KEY_READ | (write ? KEY_SET_VALUE : 0);
    if (const LSTATUS status = RegOpenKeyExW(parent, path.c_str(), 0, access, &key); status != ERROR_SUCCESS) {
        return std::unexpected(regError(status, L"could not open registry key", path));
    }
    return RegKey(key);
}

std::vector<std::wstring> RegKey::subkeys() const {
    std::vector<std::wstring> names;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(m_key, i, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status == ERROR_SUCCESS) {
            names.emplace_back(name, length);
        }
    }
    return names;
}

std::optional<std::uint32_t> RegKey::dword(const wchar_t* name) const {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(m_key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD) {
        return std::nullopt;
    }
    return value;
}

namespace {

std::optional<std::wstring> readRaw(HKEY key, const wchar_t* name, DWORD& type) {
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::wstring text(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    text.resize(size / sizeof(wchar_t));
    return text;
}

} // namespace

std::optional<std::wstring> RegKey::string(const wchar_t* name) const {
    DWORD type = 0;
    auto text = readRaw(m_key, name, type);
    if (!text || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return std::nullopt;
    }
    while (!text->empty() && text->back() == L'\0') {
        text->pop_back();
    }
    return text;
}

std::vector<std::wstring> RegKey::multiString(const wchar_t* name) const {
    DWORD type = 0;
    const auto text = readRaw(m_key, name, type);
    std::vector<std::wstring> items;
    if (!text || type != REG_MULTI_SZ) {
        return items;
    }
    std::size_t start = 0;
    while (start < text->size()) {
        const std::size_t end = text->find(L'\0', start);
        const std::size_t stop = end == std::wstring::npos ? text->size() : end;
        if (stop > start) {
            items.push_back(text->substr(start, stop - start));
        }
        start = stop + 1;
    }
    return items;
}

Result<void> RegKey::setDword(const wchar_t* name, std::uint32_t value) {
    const DWORD data = value;
    if (const LSTATUS status =
            RegSetValueExW(m_key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
        status != ERROR_SUCCESS) {
        return fail(ErrorCode::IoError, L"could not write registry value", name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
    }
    return {};
}

Result<void> RegKey::deleteValue(const wchar_t* name) {
    const LSTATUS status = RegDeleteValueW(m_key, name);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        return fail(ErrorCode::IoError, L"could not delete registry value", name, static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
    }
    return {};
}

bool deleteKeyByHandle(HKEY key) noexcept {
    using NtDeleteKeyFn = LONG(NTAPI*)(HANDLE);
    static const auto ntDeleteKey =
        reinterpret_cast<NtDeleteKeyFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtDeleteKey"));
    return ntDeleteKey && ntDeleteKey(key) >= 0;
}

Result<OfflineHive> OfflineHive::load(const std::filesystem::path& file) {
    static std::atomic<int> counter{0};
    for (const wchar_t* privilege : {SE_BACKUP_NAME, SE_RESTORE_NAME}) {
        if (auto enabled = enablePrivilege(privilege); !enabled) {
            return std::unexpected(enabled.error());
        }
    }
    const std::wstring name = std::format(L"WinLove_{}_{}_{}", file.filename().wstring(), GetCurrentProcessId(), ++counter);
    if (const LSTATUS status = RegLoadKeyW(HKEY_LOCAL_MACHINE, name.c_str(), file.c_str()); status != ERROR_SUCCESS) {
        return std::unexpected(regError(status, L"could not load registry hive", file.wstring()));
    }
    auto root = RegKey::open(HKEY_LOCAL_MACHINE, name, true);
    if (!root) {
        RegUnLoadKeyW(HKEY_LOCAL_MACHINE, name.c_str());
        return std::unexpected(root.error());
    }
    log::debug("reg", L"loaded hive " + file.wstring() + L" as HKLM\\" + name);
    return OfflineHive(name, std::move(*root));
}

OfflineHive::OfflineHive(OfflineHive&& other) noexcept
    : m_name(std::exchange(other.m_name, {})), m_root(std::move(other.m_root)) {}

OfflineHive::~OfflineHive() {
    if (m_name.empty()) {
        return;
    }
    if (m_root) {
        RegFlushKey(m_root.get());
    }
    m_root = RegKey();
    // A just-closed handle can keep the hive busy for a moment; retry briefly.
    LSTATUS status = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 10; ++attempt) {
        status = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, m_name.c_str());
        if (status == ERROR_SUCCESS) {
            log::debug("reg", L"unloaded HKLM\\" + m_name);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    log::error("reg", std::format(L"could not unload HKLM\\{} ({}) — it is unloaded before unmount", m_name, status));
}

} // namespace wl::core
