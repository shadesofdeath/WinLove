#include "core/image/OffReg.h"

#include "base/Log.h"

#include <format>
#include <mutex>

namespace wl::core {

const OffRegApi& offregApi() {
    static OffRegApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        const HMODULE module = LoadLibraryExW(L"offreg.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            log::error("core", std::format(L"offreg.dll could not be loaded [{}]", GetLastError()));
            return;
        }
        auto get = [module](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(module, name));
        };
        get(api.openHive, "OROpenHive");
        get(api.closeHive, "ORCloseHive");
        get(api.openKey, "OROpenKey");
        get(api.closeKey, "ORCloseKey");
        get(api.getValue, "ORGetValue");
        get(api.enumKey, "OREnumKey");
        get(api.enumValue, "OREnumValue");
    });
    return api;
}

OffRegKey::~OffRegKey() {
    if (m_key) {
        if (m_hive) {
            offregApi().closeHive(m_key);
        } else {
            offregApi().closeKey(m_key);
        }
    }
}

Result<OffRegKey> OffRegKey::openHive(const std::filesystem::path& file) {
    const auto& api = offregApi();
    if (!api.ready()) {
        return fail(ErrorCode::Unsupported, L"offreg.dll is not available");
    }
    ORHKEY hive = nullptr;
    if (const DWORD status = api.openHive(file.c_str(), &hive); status != ERROR_SUCCESS) {
        return std::unexpected(Error{status == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::IoError,
                                     L"could not open the hive file", file.wstring(),
                                     static_cast<std::int32_t>(HRESULT_FROM_WIN32(status))});
    }
    return OffRegKey(hive, true);
}

OffRegKey OffRegKey::open(const std::wstring& path) const {
    ORHKEY key = nullptr;
    if (!m_key || offregApi().openKey(m_key, path.c_str(), &key) != ERROR_SUCCESS) {
        return {};
    }
    return OffRegKey(key, false);
}

std::vector<std::wstring> OffRegKey::subkeys() const {
    std::vector<std::wstring> names;
    if (!m_key) {
        return names;
    }
    wchar_t buffer[512];
    for (DWORD i = 0;; ++i) {
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (offregApi().enumKey(m_key, i, buffer, &size, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        names.emplace_back(buffer, size);
    }
    return names;
}

std::vector<OffRegKey::Value> OffRegKey::values(bool withData) const {
    std::vector<Value> out;
    if (!m_key) {
        return out;
    }
    // offreg answers a call without a data buffer with ERROR_MORE_DATA and leaves the NAME buffer
    // untouched (2026-10-02: every value then carried the previous one's name). The name is taken
    // from a call that succeeded, with a data buffer grown until the value fits.
    std::wstring name(16384, L'\0');
    std::vector<std::uint8_t> data(4096);
    for (DWORD i = 0;; ++i) {
        DWORD status = ERROR_MORE_DATA;
        DWORD nameSize = 0;
        DWORD type = 0;
        DWORD dataSize = 0;
        while (status == ERROR_MORE_DATA) {
            nameSize = static_cast<DWORD>(name.size());
            dataSize = static_cast<DWORD>(data.size());
            status = offregApi().enumValue(m_key, i, name.data(), &nameSize, &type, data.data(), &dataSize);
            if (status == ERROR_MORE_DATA) {
                data.resize(std::max<std::size_t>(data.size() * 2, dataSize));
            }
        }
        if (status != ERROR_SUCCESS) {
            break;
        }
        Value value{std::wstring(name.data(), nameSize), type, {}};
        if (withData) {
            value.data.assign(data.begin(), data.begin() + dataSize);
        }
        out.push_back(std::move(value));
    }
    return out;
}

std::optional<DWORD> OffRegKey::dword(const wchar_t* name) const {
    DWORD value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (!m_key || offregApi().getValue(m_key, nullptr, name, &type, &value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
        return std::nullopt;
    }
    return value;
}

} // namespace wl::core
