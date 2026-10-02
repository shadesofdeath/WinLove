#pragma once
// P13: the answer file (autounattend.xml) Windows Setup reads from the root of its media, built
// from a small set of options. Only what an option asks for is written: an empty UnattendOptions
// gives a file with no settings, and every page Setup would show stays as it is.
//
// Passes used (elements in the order Windows SIM writes them):
//   windowsPE   language of Setup, LabConfig requirement bypasses, disk layout (target disk,
//               optional recovery partition), image index, product key, EULA. A <UserData>
//               block never goes out without a <ProductKey>: Setup then stops with "cannot read
//               the <ProductKey> setting from the unattend answer file" (Windows 10 22H2, 2026-09-30).
//   specialize  Shell-Setup: computer name ("*" = random), registered owner / organisation, time
//               zone. Deployment RunSynchronous (as SYSTEM, before OOBE): BypassNRO, the built-in
//               Administrator switched on, password expiry / account lockout, device encryption
//               prevented, the user's own commands.
//   oobeSystem  language; Shell-Setup: AutoLogon, FirstLogonCommands, OOBE pages, UserAccounts
//               (AdministratorPassword, LocalAccounts).
#include "base/Result.h"
#include "core/image/ImageInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// A further local account (the first one is accountName / password).
struct UnattendAccount {
    std::wstring name;
    std::wstring password;
    bool administrator = false; // Administrators, else Users
    [[nodiscard]] bool operator==(const UnattendAccount&) const = default;
};

enum class UnattendDisk : std::uint8_t {
    Ask,     // Setup shows the disk page
    WipeGpt, // disk 0 erased: EFI 300 MB + MSR 16 MB + Windows (UEFI)
    WipeMbr, // disk 0 erased: System 500 MB (active) + Windows (legacy BIOS)
};

struct UnattendOptions {
    Architecture architecture = Architecture::X64;

    // Dil ve bölge — empty = Setup asks.
    std::wstring uiLanguage; // "tr-TR"; must be a language the image contains
    std::wstring locale;     // formats and system locale
    std::wstring keyboard;   // InputLocale: "041f:0000041f" or a locale name
    std::wstring timeZone;   // "Turkey Standard Time"; empty = Windows decides

    // Hesap — empty name = OOBE asks.
    std::wstring accountName; // local administrator
    std::wstring password;
    bool autoLogon = false;   // once, at the first start
    std::wstring computerName;
    bool randomComputerName = false; // ComputerName "*": Setup makes one up (computerName ignored)
    std::vector<UnattendAccount> extraAccounts;
    bool enableAdministrator = false; // the built-in Administrator, with administratorPassword
    std::wstring administratorPassword;
    bool passwordsNeverExpire = false; // net accounts /maxpwage:UNLIMITED
    bool disableLockout = false;       // net accounts /lockoutthreshold:0
    std::wstring registeredOwner;
    std::wstring registeredOrganization;

    UnattendDisk disk = UnattendDisk::Ask;
    int diskId = 0;                 // the disk that is erased (0 = the first one Setup sees)
    // Windows RE on a partition of its own, first on the disk (Microsoft's sample layout:
    // Recovery, then EFI / MSR or System, then Windows), 1000 MB.
    bool recoveryPartition = false;
    // Compact OS: Windows' own files stay compressed on the disk (WOF, XPRESS); Setup writes
    // <ImageInstall><OSImage><Compact>true</Compact> (windowsPE, Microsoft-Windows-Setup).
    bool compactOs = false;

    // OOBE
    bool skipPrivacy = false;       // ProtectYourPC = 3
    bool skipOnlineAccount = false; // HideOnlineAccountScreens + HideWirelessSetupInOOBE
    bool bypassNro = false;         // no Microsoft account requirement (BypassNRO)
    bool acceptEula = false;        // AcceptEula + HideEULAPage
    bool hideWifiSetup = false;     // HideWirelessSetupInOOBE (also part of skipOnlineAccount)
    bool hideOemRegistration = false; // HideOEMRegistrationScreen

    // Sistem
    bool preventDeviceEncryption = false; // BitLocker\PreventDeviceEncryption = 1 (specialize)
    std::vector<std::wstring> specializeCommands; // run as SYSTEM in specialize, before OOBE
    std::vector<std::wstring> firstLogonCommands; // run at the first logon of the first account

    // Ürün anahtarı
    std::wstring productKey; // XXXXX-XXXXX-XXXXX-XXXXX-XXXXX; empty: see editionId
    int imageIndex = 0;      // edition to install; 0 = Setup asks (or the key decides)
    // The edition Setup will install, when that is known (the image has one edition, or
    // imageIndex names it): "CoreSingleLanguage". Without a key of the user's, its generic key is
    // written — Setup installs that edition without asking, activation is for later. Unknown
    // (empty): the all-zero key with WillShowUI "Always", and Setup shows its key page.
    // Not read back from a file: it describes the image, not the answers.
    std::wstring editionId;

    // Gereksinimler (Windows 11 checks, skipped through HKLM\SYSTEM\Setup\LabConfig)
    bool bypassTpm = false;
    bool bypassSecureBoot = false;
    bool bypassRam = false;
    bool bypassCpu = false;     // supported processor list / 2 cores
    bool bypassStorage = false; // 64 GB system disk

    [[nodiscard]] bool operator==(const UnattendOptions&) const = default;
};

// The file text ("\n" line ends, 2-space indent, XML declaration first). Saved as UTF-8.
[[nodiscard]] std::wstring buildUnattendXml(const UnattendOptions& options);

// Reads the options back from an answer file — ours or someone else's: what is not one of the
// options above is ignored.
[[nodiscard]] Result<UnattendOptions> parseUnattendXml(std::string_view utf8);

enum class UnattendProblem : std::uint8_t {
    ComputerName,         // 1–15 characters, no spaces or \/:*?"<>|, not only digits
    AccountName,          // 1–20 characters, none of "/\[]:;|=,+*?<>, not a built-in account
    ProductKey,           // five groups of five letters / digits
    AutoLogonNeedsAccount, // automatic logon without a local account
    ExtraAccountName,      // one of the further accounts: same rules as AccountName, no duplicates
    DiskId,                // 0 … 63
};
[[nodiscard]] std::vector<UnattendProblem> validateUnattend(const UnattendOptions& options);

// Microsoft's generic (default) key of a client edition: it selects the edition and installs
// without activating. Empty for editions that have none here. Each was checked against the
// pkeyconfig of a Windows 10 22H2 image and of a Windows 11 25H2 installation (PidGenX names the
// edition a key belongs to).
[[nodiscard]] std::wstring_view genericProductKey(std::wstring_view editionId) noexcept;
// "00000-00000-00000-00000-00000": stands in the answer file where Setup is to ask.
inline constexpr std::wstring_view kNoProductKey = L"00000-00000-00000-00000-00000";

// Setup's own encoding of a password element (<PlainText>false</PlainText>): Base64 of the
// UTF-16LE text followed by the element name ("Password", "AdministratorPassword").
// Obfuscation, not encryption.
[[nodiscard]] std::wstring encodeUnattendPassword(std::wstring_view password, std::wstring_view element = L"Password");
[[nodiscard]] std::wstring decodeUnattendPassword(std::wstring_view encoded, std::wstring_view element = L"Password");

} // namespace wl::core
