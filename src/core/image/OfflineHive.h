#pragma once
// A registry hive file of a mounted (offline) image, loaded under HKLM with RegLoadKey for the
// lifetime of the object (ENGINE.md "OfflineRegistry"). Needs an elevated process; SeBackup and
// SeRestore are enabled here. Every key opened below root() must be closed before the hive is
// destroyed (RegUnLoadKey fails while a handle is open) — use RegKey.
// Leftover hives (crash while loaded) are unloaded by MountHealth::unloadHivesUnder before unmount.
#include "base/Result.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wl::core {

// RAII HKEY with the few typed reads/writes the pages need.
class RegKey {
public:
    RegKey() = default;
    explicit RegKey(HKEY key) noexcept : m_key(key) {}
    RegKey(RegKey&& other) noexcept : m_key(std::exchange(other.m_key, nullptr)) {}
    RegKey& operator=(RegKey&& other) noexcept;
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    ~RegKey();

    [[nodiscard]] static Result<RegKey> open(HKEY parent, const std::wstring& path, bool write = false);
    [[nodiscard]] HKEY get() const noexcept { return m_key; }
    explicit operator bool() const noexcept { return m_key != nullptr; }

    [[nodiscard]] std::vector<std::wstring> subkeys() const;
    [[nodiscard]] std::optional<std::uint32_t> dword(const wchar_t* name) const;
    // REG_SZ / REG_EXPAND_SZ (not expanded).
    [[nodiscard]] std::optional<std::wstring> string(const wchar_t* name) const;
    [[nodiscard]] std::vector<std::wstring> multiString(const wchar_t* name) const;
    [[nodiscard]] Result<void> setDword(const wchar_t* name, std::uint32_t value);
    [[nodiscard]] Result<void> deleteValue(const wchar_t* name); // missing value = success

private:
    HKEY m_key = nullptr;
};

// A registry status as an Error (access denied / missing / other).
[[nodiscard]] Error registryError(LSTATUS status, std::wstring what, std::wstring detail);
// Names of the subkeys of an open key (not owned: nothing is closed).
[[nodiscard]] std::vector<std::wstring> subkeyNames(HKEY key);

// Deletes the (empty) key behind an open handle that has DELETE access. RegDeleteKey would open
// the key again by name and run into the ACL a backup / restore open went around.
[[nodiscard]] bool deleteKeyByHandle(HKEY key) noexcept;

// The servicing / driver keys of an image belong to TrustedInstaller. These open with the ACL
// first and, on access denied, with backup / restore semantics (SeRestore, enabled by
// OfflineHive::load), which ignores it.
// Opens `name` under `parent`; `create` makes it when it is missing.
[[nodiscard]] LSTATUS openKeyForWrite(HKEY parent, const wchar_t* name, REGSAM access, HKEY& out, bool create = false);
// Deletes `name` with everything under it. A missing key is ERROR_SUCCESS.
[[nodiscard]] LSTATUS deleteKeyTree(HKEY parent, const wchar_t* name);

class OfflineHive {
public:
    // `file`: e.g. <mount>\Windows\System32\config\SYSTEM.
    [[nodiscard]] static Result<OfflineHive> load(const std::filesystem::path& file);
    OfflineHive(OfflineHive&& other) noexcept;
    OfflineHive& operator=(OfflineHive&&) = delete;
    OfflineHive(const OfflineHive&) = delete;
    OfflineHive& operator=(const OfflineHive&) = delete;
    ~OfflineHive(); // flush + unload

    [[nodiscard]] HKEY root() const noexcept { return m_root.get(); }
    [[nodiscard]] const std::wstring& keyName() const noexcept { return m_name; } // under HKLM

private:
    OfflineHive(std::wstring name, RegKey root) : m_name(std::move(name)), m_root(std::move(root)) {}
    std::wstring m_name;
    RegKey m_root;
};

} // namespace wl::core
