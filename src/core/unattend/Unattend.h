#pragma once
// P13: the answer file (autounattend.xml) Windows Setup reads from the root of its media, built
// from a small set of options. Only what an option asks for is written: an empty UnattendOptions
// gives a file with no settings, and every page Setup would show stays as it is.
//
// Passes used:
//   windowsPE   language of Setup, LabConfig requirement bypasses, disk layout, image index,
//               product key, EULA
//   specialize  computer name, time zone, BypassNRO
//   oobeSystem  language, OOBE pages, local account, one automatic logon
#include "base/Result.h"
#include "core/image/ImageInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

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

    UnattendDisk disk = UnattendDisk::Ask;

    // OOBE
    bool skipPrivacy = false;       // ProtectYourPC = 3
    bool skipOnlineAccount = false; // HideOnlineAccountScreens + HideWirelessSetupInOOBE
    bool bypassNro = false;         // no Microsoft account requirement (BypassNRO)
    bool acceptEula = false;        // AcceptEula + HideEULAPage

    // Ürün anahtarı
    std::wstring productKey; // XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
    int imageIndex = 0;      // edition to install; 0 = Setup asks (or the key decides)

    // Gereksinimler (Windows 11 checks, skipped through HKLM\SYSTEM\Setup\LabConfig)
    bool bypassTpm = false;
    bool bypassSecureBoot = false;
    bool bypassRam = false;

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
    AutoLogonNeedsAccount // automatic logon without a local account
};
[[nodiscard]] std::vector<UnattendProblem> validateUnattend(const UnattendOptions& options);

// Setup's own encoding of a password element (<PlainText>false</PlainText>): Base64 of the
// UTF-16LE text followed by the element name ("Password"). Obfuscation, not encryption.
[[nodiscard]] std::wstring encodeUnattendPassword(std::wstring_view password);
[[nodiscard]] std::wstring decodeUnattendPassword(std::wstring_view encoded);

} // namespace wl::core
