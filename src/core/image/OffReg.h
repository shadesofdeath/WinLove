#pragma once
// The Offline Registry Library (offreg.dll, System32 since Windows 8): reads a hive FILE into
// memory — nothing is loaded into the live registry, no privilege is enabled, nothing has to be
// unloaded before an unmount. It is not in the SDK import libraries (it ships with the WDK): the
// entry points are resolved at run time from System32 only.
#include "base/Result.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::core {

using ORHKEY = void*;

struct OffRegApi {
    DWORD(WINAPI* openHive)(PCWSTR, ORHKEY*) = nullptr;
    DWORD(WINAPI* closeHive)(ORHKEY) = nullptr;
    DWORD(WINAPI* openKey)(ORHKEY, PCWSTR, ORHKEY*) = nullptr;
    DWORD(WINAPI* closeKey)(ORHKEY) = nullptr;
    DWORD(WINAPI* getValue)(ORHKEY, PCWSTR, PCWSTR, PDWORD, PVOID, PDWORD) = nullptr;
    DWORD(WINAPI* enumKey)(ORHKEY, DWORD, PWSTR, PDWORD, PWSTR, PDWORD, PFILETIME) = nullptr;
    DWORD(WINAPI* enumValue)(ORHKEY, DWORD, PWSTR, PDWORD, PDWORD, PBYTE, PDWORD) = nullptr;
    [[nodiscard]] bool ready() const noexcept {
        return openHive && closeHive && openKey && closeKey && getValue && enumKey && enumValue;
    }
};
[[nodiscard]] const OffRegApi& offregApi();

// One opened key (or the hive itself); closed when destroyed. Children keep no reference to the
// parent: the hive object must outlive every key opened below it.
class OffRegKey {
public:
    OffRegKey() = default;
    OffRegKey(OffRegKey&& other) noexcept : m_key(std::exchange(other.m_key, nullptr)), m_hive(other.m_hive) {}
    OffRegKey& operator=(OffRegKey&&) = delete;
    OffRegKey(const OffRegKey&) = delete;
    OffRegKey& operator=(const OffRegKey&) = delete;
    ~OffRegKey();

    // The hive file, read only.
    [[nodiscard]] static Result<OffRegKey> openHive(const std::filesystem::path& file);
    // nullopt-like: an empty key when it does not exist.
    [[nodiscard]] OffRegKey open(const std::wstring& path) const;
    [[nodiscard]] explicit operator bool() const noexcept { return m_key != nullptr; }

    [[nodiscard]] std::vector<std::wstring> subkeys() const;
    struct Value {
        std::wstring name;
        DWORD type = 0;
        std::vector<std::uint8_t> data;
    };
    // Every value; `withData` false leaves data empty (names only: much faster on big keys).
    [[nodiscard]] std::vector<Value> values(bool withData = true) const;
    [[nodiscard]] std::optional<DWORD> dword(const wchar_t* name) const;

private:
    OffRegKey(ORHKEY key, bool hive) : m_key(key), m_hive(hive) {}
    ORHKEY m_key = nullptr;
    bool m_hive = false;
};

} // namespace wl::core
