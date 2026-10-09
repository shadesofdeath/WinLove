#include "core/image/RegistryRead.h"

#include "base/Log.h"
#include "core/image/OffReg.h"
#include "core/image/OfflineHive.h"

#include <windows.h>

#include <format>
#include <mutex>

namespace wl::core {

namespace {

const OffRegApi& offreg() {
    return offregApi();
}

bool isStringType(std::uint32_t type) noexcept {
    return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

// Byte count without the trailing UTF-16 nulls of a string value.
std::size_t trimmedSize(const std::vector<std::uint8_t>& data) {
    std::size_t size = data.size() & ~std::size_t{1};
    while (size >= 2 && data[size - 1] == 0 && data[size - 2] == 0) {
        size -= 2;
    }
    return size;
}

} // namespace

bool sameRegistryData(const RegistryWrite& write, const RegistryData& data) {
    if (write.type != data.type) {
        return false;
    }
    if (isStringType(write.type)) {
        const std::size_t a = trimmedSize(write.data);
        const std::size_t b = trimmedSize(data.data);
        return a == b && std::equal(write.data.begin(), write.data.begin() + static_cast<std::ptrdiff_t>(a), data.data.begin());
    }
    return write.data == data.data;
}

struct OfflineRegistryReader::Hive {
    ORHKEY handle = nullptr;
    std::wstring controlSet; // SYSTEM: "ControlSet001"
    ~Hive() {
        if (handle) {
            offreg().closeHive(handle);
        }
    }
};

OfflineRegistryReader::OfflineRegistryReader(std::filesystem::path mountDir) : m_mountDir(std::move(mountDir)) {}

OfflineRegistryReader::~OfflineRegistryReader() = default;

Result<OfflineRegistryReader::Hive*> OfflineRegistryReader::hive(OfflineHiveFile file) {
    if (const auto it = m_hives.find(file); it != m_hives.end()) {
        return it->second.get();
    }
    const auto& api = offreg();
    if (!api.ready()) {
        return fail(ErrorCode::Unsupported, L"offreg.dll is not available");
    }
    const auto path = hiveFilePath(m_mountDir, file);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        m_hives.emplace(file, nullptr); // e.g. no UsrClass.dat in the default profile
        return static_cast<Hive*>(nullptr);
    }
    auto opened = std::make_unique<Hive>();
    if (const DWORD status = api.openHive(path.c_str(), &opened->handle); status != ERROR_SUCCESS) {
        opened->handle = nullptr;
        return std::unexpected(registryError(status, L"could not open the hive file", path.wstring()));
    }
    if (file == OfflineHiveFile::System) {
        DWORD current = 1;
        DWORD type = 0;
        DWORD size = sizeof(current);
        if (api.getValue(opened->handle, L"Select", L"Current", &type, &current, &size) != ERROR_SUCCESS ||
            type != REG_DWORD) {
            current = 1;
        }
        opened->controlSet = std::format(L"ControlSet{:03}", current);
    }
    auto* raw = opened.get();
    m_hives.emplace(file, std::move(opened));
    return raw;
}

Result<void*> OfflineRegistryReader::openKey(std::wstring_view key) {
    auto mapped = mapOfflineKey(key);
    if (!mapped) {
        return std::unexpected(mapped.error());
    }
    auto h = hive(mapped->hive);
    if (!h) {
        return std::unexpected(h.error());
    }
    if (!*h) {
        return static_cast<void*>(nullptr);
    }
    std::wstring path = mapped->path;
    if (mapped->hive == OfflineHiveFile::System) {
        constexpr std::wstring_view ccs = L"CurrentControlSet";
        if (path.size() >= ccs.size() && _wcsnicmp(path.c_str(), ccs.data(), ccs.size()) == 0 &&
            (path.size() == ccs.size() || path[ccs.size()] == L'\\')) {
            path = (*h)->controlSet + path.substr(ccs.size());
        }
    }
    ORHKEY opened = nullptr;
    const DWORD status = offreg().openKey((*h)->handle, path.c_str(), &opened);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        return static_cast<void*>(nullptr);
    }
    if (status != ERROR_SUCCESS) {
        return std::unexpected(registryError(status, L"could not open registry key", std::wstring(key)));
    }
    return opened;
}

void OfflineRegistryReader::closeKey(void* key) noexcept {
    if (key) {
        offreg().closeKey(key);
    }
}

Result<std::optional<RegistryData>> OfflineRegistryReader::value(std::wstring_view key, std::wstring_view name) {
    auto opened = openKey(key);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    if (!*opened) {
        return std::optional<RegistryData>{};
    }
    struct Closer {
        void* key;
        ~Closer() { closeKey(key); }
    } closer{*opened};
    const std::wstring valueName(name);
    RegistryData data;
    DWORD type = 0;
    DWORD size = 0;
    DWORD status = offreg().getValue(*opened, nullptr, valueName.c_str(), &type, nullptr, &size);
    for (int attempt = 0; status == ERROR_SUCCESS || status == ERROR_MORE_DATA; ++attempt) {
        data.data.resize(size);
        status = offreg().getValue(*opened, nullptr, valueName.c_str(), &type, data.data.empty() ? nullptr : data.data.data(), &size);
        if (status == ERROR_SUCCESS) {
            data.data.resize(size);
            data.type = type;
            return std::optional<RegistryData>{std::move(data)};
        }
        if (attempt == 3) {
            break;
        }
    }
    if (status == ERROR_FILE_NOT_FOUND) {
        return std::optional<RegistryData>{};
    }
    return std::unexpected(registryError(status, L"could not read registry value", std::format(L"{}::{}", key, name)));
}

Result<bool> OfflineRegistryReader::keyExists(std::wstring_view key) {
    auto opened = openKey(key);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    closeKey(*opened);
    return *opened != nullptr;
}

namespace {

// "HKLM\Software\X" → the open root and "Software\X"; nullptr for a root this PC has no handle for.
HKEY liveRoot(std::wstring_view key, std::wstring& rest) {
    const auto slash = key.find(L'\\');
    const std::wstring root(key.substr(0, slash));
    rest = slash == std::wstring_view::npos ? std::wstring() : std::wstring(key.substr(slash + 1));
    if (root == L"HKLM") return HKEY_LOCAL_MACHINE;
    if (root == L"HKCU") return HKEY_CURRENT_USER;
    if (root == L"HKU") return HKEY_USERS;
    if (root == L"HKCR") return HKEY_CLASSES_ROOT;
    if (root == L"HKCC") return HKEY_CURRENT_CONFIG;
    return nullptr;
}

} // namespace

bool liveRegistryHolds(const RegistryWrite& write) {
    std::wstring path;
    const HKEY root = liveRoot(normalizeRegistryKey(write.key), path);
    if (!root) {
        return false;
    }
    HKEY key = nullptr;
    const bool exists = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS;
    auto close = [&] {
        if (key) {
            RegCloseKey(key);
        }
    };
    switch (write.kind) {
    case RegistryWrite::Kind::CreateKey: close(); return exists;
    case RegistryWrite::Kind::DeleteKey: close(); return !exists;
    case RegistryWrite::Kind::DeleteValue: {
        const bool has = exists && RegQueryValueExW(key, write.name.empty() ? nullptr : write.name.c_str(), nullptr, nullptr,
                                                    nullptr, nullptr) == ERROR_SUCCESS;
        close();
        return !has;
    }
    case RegistryWrite::Kind::Set: {
        if (!exists) {
            return false;
        }
        DWORD type = 0;
        DWORD bytes = 0;
        const wchar_t* name = write.name.empty() ? nullptr : write.name.c_str();
        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) {
            close();
            return false;
        }
        RegistryData data;
        data.type = type;
        data.data.resize(bytes);
        const bool read = RegQueryValueExW(key, name, nullptr, &type, data.data.data(), &bytes) == ERROR_SUCCESS;
        close();
        data.data.resize(bytes);
        return read && sameRegistryData(write, data);
    }
    }
    close();
    return false;
}

Result<bool> OfflineRegistryReader::holds(const RegistryWrite& write) {
    if (isPostSetupOnlyKey(write.key)) {
        return false;
    }
    switch (write.kind) {
    case RegistryWrite::Kind::Set: {
        auto current = value(write.key, write.name);
        if (!current) {
            return std::unexpected(current.error());
        }
        return current->has_value() && sameRegistryData(write, **current);
    }
    case RegistryWrite::Kind::DeleteValue: {
        auto current = value(write.key, write.name);
        if (!current) {
            return std::unexpected(current.error());
        }
        return !current->has_value();
    }
    case RegistryWrite::Kind::DeleteKey: {
        auto exists = keyExists(write.key);
        if (!exists) {
            return std::unexpected(exists.error());
        }
        return !*exists;
    }
    case RegistryWrite::Kind::CreateKey:
        return keyExists(write.key);
    }
    return false;
}

} // namespace wl::core
