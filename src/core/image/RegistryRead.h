#pragma once
// Reading the registry of a mounted (offline) image — what P11 / P12 show as "already in the
// image" (D-045). The hive files are opened with the Offline Registry Library (offreg.dll,
// System32 since Windows 8): it parses the file into memory, so unlike OfflineHive nothing is
// loaded into the live registry, no privilege is enabled and nothing has to be unloaded before
// an unmount. It needs read access to the file only (a mounted image: an elevated process).
// Keys are given in live-system terms and mapped like writes (RegistryEdit.h: mapOfflineKey);
// SYSTEM\CurrentControlSet resolves through Select\Current.
#include "base/Result.h"
#include "core/image/RegistryEdit.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace wl::core {

struct RegistryData {
    std::uint32_t type = 0;
    std::vector<std::uint8_t> data;
};

// Does `data` already say what `write` would write: same type and bytes. Strings compare without
// their terminating nulls (a .reg "text" carries one, a value written by some tool may not).
[[nodiscard]] bool sameRegistryData(const RegistryWrite& write, const RegistryData& data);

class OfflineRegistryReader {
public:
    explicit OfflineRegistryReader(std::filesystem::path mountDir);
    ~OfflineRegistryReader();
    OfflineRegistryReader(const OfflineRegistryReader&) = delete;
    OfflineRegistryReader& operator=(const OfflineRegistryReader&) = delete;

    // Missing hive file, key or value → nullopt; errors are unreadable / corrupt hives and keys
    // outside what the image has (Unsupported, as for writes).
    [[nodiscard]] Result<std::optional<RegistryData>> value(std::wstring_view key, std::wstring_view name);
    [[nodiscard]] Result<bool> keyExists(std::wstring_view key);

    // Is the write's result already there: Set → the value with the same data; DeleteValue → no
    // such value; DeleteKey → no such key; CreateKey → the key exists. Keys that only exist after
    // setup (isPostSetupOnlyKey) are never "there".
    [[nodiscard]] Result<bool> holds(const RegistryWrite& write);

private:
    struct Hive;
    [[nodiscard]] Result<Hive*> hive(OfflineHiveFile file); // nullptr: no such file in the image
    [[nodiscard]] Result<void*> openKey(std::wstring_view key); // nullptr: missing; close with closeKey
    static void closeKey(void* key) noexcept;

    std::filesystem::path m_mountDir;
    std::map<OfflineHiveFile, std::unique_ptr<Hive>> m_hives;
};

} // namespace wl::core
