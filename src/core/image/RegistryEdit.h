#pragma once
// P11: registry writes against a mounted (offline) image.
//
// A RegistryWrite is expressed in live-system terms ("HKLM\SOFTWARE\…", "HKCU\…") and mapped to the
// image's hive files by mapOfflineKey:
//   HKLM\SOFTWARE\x            → Windows\System32\config\SOFTWARE : x
//   HKLM\SYSTEM\x              → …\config\SYSTEM : x   (CurrentControlSet → ControlSetNNN from Select\Current)
//   HKCR\x                     → SOFTWARE : Classes\x
//   HKCU\Software\Classes\x    → Users\Default\AppData\Local\Microsoft\Windows\UsrClass.dat : x
//   HKCU\x                     → Users\Default\NTUSER.DAT : x   (the profile new users are created from)
//   HKU\.DEFAULT\x, HKU\S-1-5-18\x → …\config\DEFAULT : x
// HKCC and the service accounts (HKU\S-1-5-19 / -20) have no hive in the image: they can only be
// written after setup (isPostSetupOnlyKey). SAM / SECURITY / HARDWARE / other users' SIDs are
// rejected (Unsupported).
//
// In the ChangeSet a write is one SetRegistryValue operation: target = "<key>::<name>" (empty name =
// default value), value = the data in .reg syntax ("dword:00000001", "\"text\"", "hex(2):…",
// "-" deletes the value). Key operations have targets of their own, so "delete the key, then set
// its default value" keeps both: "[-]" deletes the key (target "<key>\\::"; "<key>::" from older
// presets is still read), "[+]" creates a key a .reg file lists without values (target "<key>\::").
// Presets therefore stay human-readable.
#include "base/Result.h"
#include "core/image/OfflineHive.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct RegistryWrite {
    // CreateKey: "[key]" with no values in a .reg file (shell extensions are often just a key name).
    enum class Kind : std::uint8_t { Set, DeleteValue, DeleteKey, CreateKey };
    Kind kind = Kind::Set;
    std::wstring key;  // normalized root: HKLM, HKCU, HKU, HKCR, HKCC
    std::wstring name; // empty = default value "@"
    std::uint32_t type = 1; // REG_SZ
    std::vector<std::uint8_t> data;

    [[nodiscard]] bool operator==(const RegistryWrite&) const = default;
};

enum class OfflineHiveFile : std::uint8_t { Software, System, DefaultUser, DefaultUserClasses, DotDefault };

struct OfflineKey {
    OfflineHiveFile hive;
    std::wstring path; // inside the hive ("CurrentControlSet" still unresolved)
};

// "HKEY_LOCAL_MACHINE\Software\X" → "HKLM\Software\X" (trims slashes). Empty when the root is unknown.
[[nodiscard]] std::wstring normalizeRegistryKey(std::wstring_view key);
[[nodiscard]] Result<OfflineKey> mapOfflineKey(std::wstring_view key);
[[nodiscard]] std::filesystem::path hiveFilePath(const std::filesystem::path& mountDir, OfflineHiveFile hive);
// "ControlSet001": what CurrentControlSet means in a loaded SYSTEM hive (Select\Current; 1 when unreadable).
[[nodiscard]] std::wstring currentControlSet(HKEY systemRoot);
// "Sistem" (HKLM/HKCR/HKU) vs "Kullanıcı" (HKCU).
[[nodiscard]] bool isUserKey(std::wstring_view key);
// No hive in the image, but writable on the installed system by SetupComplete.cmd (SYSTEM):
// HKCC\…, HKU\S-1-5-19\…, HKU\S-1-5-20\…. Such a write is only recorded for after setup.
[[nodiscard]] bool isPostSetupOnlyKey(std::wstring_view key);

// .reg data syntax ↔ RegistryWrite.
[[nodiscard]] Result<RegistryWrite> parseRegValue(std::wstring key, std::wstring name, std::wstring_view value);
[[nodiscard]] std::wstring formatRegValue(const RegistryWrite& write);
[[nodiscard]] std::wstring registryTarget(const RegistryWrite& write); // "<key>::<name>"
[[nodiscard]] Result<RegistryWrite> registryWriteFrom(std::wstring_view target, std::wstring_view value);

// A whole "Windows Registry Editor Version 5.00" / "REGEDIT4" file. Errors carry the line number.
[[nodiscard]] Result<std::vector<RegistryWrite>> parseRegText(std::wstring_view text);
[[nodiscard]] Result<std::vector<RegistryWrite>> readRegFile(const std::filesystem::path& file);

// "HKEY_CURRENT_USER\…" style .reg text (header, [key] groups in order) — what reg.exe import reads.
[[nodiscard]] std::wstring formatRegText(const std::vector<RegistryWrite>& writes);

// ---- Values Windows resets during OOBE / first logon -------------------------------------------
// Some settings written offline do not survive setup: the OOBE privacy page rewrites location /
// advertising / tailored-experience values, the first logon applies the default theme and taskbar
// layout, ContentDeliveryManager reseeds itself. Such writes (catalog tweaks marked firstLogon, and
// every value of an imported .reg file — nobody knows which of those Windows resets) are also
// recorded in .reg files inside the image and re-imported after setup:
//   HKLM/HKCR → Windows\Setup\Scripts\WinLove\setupcomplete.reg, imported by SetupComplete.cmd
//               (runs as SYSTEM after OOBE, before the first logon);
//   HKCU      → Windows\Setup\Scripts\WinLove\firstlogon-user.reg, imported at each new user's first
//               logon by a RunOnce value in the default profile (reg.exe, as that user).
// Caveat: Windows skips SetupComplete.cmd when the edition is activated with an OEM product key.
enum class DeferredScope : std::uint8_t { Machine, User };
[[nodiscard]] DeferredScope deferredScope(const RegistryWrite& write);
[[nodiscard]] std::filesystem::path deferredRegFile(const std::filesystem::path& mountDir, DeferredScope scope);
// Makes sure SetupComplete.cmd (created if missing) contains `line`, once, right after "@echo off":
// an existing script may end with exit / shutdown / del %0. `comment` goes into a rem line above it.
[[nodiscard]] Result<void> ensureSetupCompleteLine(const std::filesystem::path& setupComplete, std::string_view line,
                                                   std::string_view comment);
// The line that imports setupcomplete.reg.
[[nodiscard]] Result<void> ensureSetupCompleteImport(const std::filesystem::path& setupComplete);
extern const wchar_t* const kSetupCompleteImportLine;

// The deferred .reg files of one apply run. An import applies entries in order, so a write is
// appended right away (the last entry of a slot wins — the same result as replacing it): a .reg
// file with thousands of values costs one small append each. close() rewrites the files without
// the superseded entries. The first machine-scope write also puts the import line into
// SetupComplete.cmd.
class DeferredRegistry {
public:
    explicit DeferredRegistry(std::filesystem::path mountDir);
    ~DeferredRegistry();
    DeferredRegistry(const DeferredRegistry&) = delete;
    DeferredRegistry& operator=(const DeferredRegistry&) = delete;

    [[nodiscard]] Result<void> record(const RegistryWrite& write);
    // True once, after the first user-scope write: the caller adds the default-profile RunOnce value.
    [[nodiscard]] bool takeUserHook() noexcept;
    void close();

private:
    struct File {
        std::filesystem::path path;
        std::vector<RegistryWrite> writes;
        bool loaded = false;
        bool superseded = false; // an appended entry replaced an earlier one: compact on close
    };
    std::filesystem::path m_mountDir;
    File m_machine;
    File m_user;
    bool m_machineHooked = false;
    bool m_userHookPending = false;
    bool m_userHooked = false;
};

class OfflineRegistry;
// Offline write (when the key has a hive in the image) + deferred record + hook (SetupComplete
// line or default-user RunOnce value).
[[nodiscard]] Result<void> deferRegistryWrite(OfflineRegistry& registry, DeferredRegistry& deferred,
                                              const RegistryWrite& write);

// Applies writes to the image's hives; each hive is loaded on first use and unloaded when the
// object is destroyed (so a batch of tweaks costs one load per hive). Needs an elevated process.
class OfflineRegistry {
public:
    explicit OfflineRegistry(std::filesystem::path mountDir);
    ~OfflineRegistry();
    OfflineRegistry(const OfflineRegistry&) = delete;
    OfflineRegistry& operator=(const OfflineRegistry&) = delete;

    [[nodiscard]] Result<void> apply(const RegistryWrite& write);
    // Unloads every loaded hive now (also done by the destructor).
    void close();

private:
    [[nodiscard]] Result<std::wstring> resolve(const OfflineKey& key); // "WinLove_…\path" under HKLM

    std::filesystem::path m_mountDir;
    std::map<OfflineHiveFile, std::unique_ptr<OfflineHive>> m_hives;
};

} // namespace wl::core
