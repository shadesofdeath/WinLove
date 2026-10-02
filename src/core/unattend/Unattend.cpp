#include "core/unattend/Unattend.h"

#include "base/Encoding.h"
#include "base/Text.h"
#include "base/Utf8.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
#include <initializer_list>
#include <span>

namespace wl::core {

namespace {

// Windows 11 Setup skips a hardware check when its value is 1 here (read in Windows PE).
std::wstring labConfig(std::wstring_view value) {
    return std::format(L"reg add HKLM\\SYSTEM\\Setup\\LabConfig /v {} /t REG_DWORD /d 1 /f", value);
}
constexpr std::wstring_view kBypassNro =
    L"reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE /v BypassNRO /t REG_DWORD /d 1 /f";
// specialize, as SYSTEM: what the options write besides elements. Read back by these texts.
constexpr std::wstring_view kEnableAdministrator = L"net.exe user Administrator /active:yes";
constexpr std::wstring_view kNeverExpire = L"net.exe accounts /maxpwage:UNLIMITED";
constexpr std::wstring_view kNoLockout = L"net.exe accounts /lockoutthreshold:0";
constexpr std::wstring_view kPreventEncryption =
    L"reg add HKLM\\SYSTEM\\CurrentControlSet\\Control\\BitLocker /v PreventDeviceEncryption /t REG_DWORD /d 1 /f";
constexpr std::wstring_view kRecoveryGpt = L"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC"; // Windows RE partition type
constexpr std::wstring_view kRecoveryMbr = L"0x27";
constexpr int kRecoveryMb = 1000;

struct GenericKey {
    std::wstring_view editionId;
    std::wstring_view key;
};
constexpr GenericKey kGenericKeys[] = {
    {L"Core", L"YTMG3-N6DKC-DKB77-7M9GH-8HVX7"},
    {L"CoreN", L"4CPRK-NM3K3-X6XXQ-RXX86-WXCHW"},
    {L"CoreSingleLanguage", L"BT79Q-G7N6G-PGBYW-4YWX6-6F4BT"},
    {L"CoreCountrySpecific", L"N2434-X9D7W-8PF6X-8DV9T-8TYMD"},
    {L"Professional", L"VK7JG-NPHTM-C97JM-9MPGT-3V66T"},
    {L"ProfessionalN", L"2B87N-8KFHP-DKV6R-Y2C8J-PKCKT"},
    {L"ProfessionalSingleLanguage", L"G3KNM-CHG6T-R36X3-9QDG6-8M8K9"},
    {L"ProfessionalCountrySpecific", L"HNGCC-Y38KG-QVK8D-WMWRK-X86VK"},
    {L"ProfessionalWorkstation", L"DXG7C-N36C4-C4HTG-X4T3X-2YV77"},
    {L"ProfessionalWorkstationN", L"WYPNQ-8C467-V2W6J-TX4WX-WT2RQ"},
    {L"ProfessionalEducation", L"8PTT6-RNW4C-6V7J2-C2D3X-MHBPB"},
    {L"ProfessionalEducationN", L"GJTYN-HDMQY-FRR76-HVGC7-QPF8P"},
    {L"Education", L"YNMGQ-8RYV3-4PGQ3-C8XTP-7CFBY"},
    {L"EducationN", L"84NGF-MHBT6-FXBX8-QWJK7-DRR8H"},
    {L"Enterprise", L"XGVPP-NMH47-7TTHJ-W3FW7-8HV2C"},
    {L"EnterpriseN", L"WGGHN-J84D6-QYCPR-T7PJ7-X766F"},
};

bool isGenericProductKey(std::wstring_view key) {
    return std::ranges::any_of(kGenericKeys, [&](const GenericKey& k) { return _wcsnicmp(k.key.data(), key.data(), k.key.size()) == 0 && key.size() == k.key.size(); });
}

const wchar_t* architectureAttribute(Architecture architecture) {
    switch (architecture) {
    case Architecture::X86: return L"x86";
    case Architecture::Arm: return L"arm";
    case Architecture::Arm64: return L"arm64";
    default: return L"amd64";
    }
}

std::wstring escape(std::wstring_view text) {
    return xmlEscape(text, /*apostrophe=*/false);
}

// Indented element writer; `depth` starts inside <unattend><settings><component>.
class Xml {
public:
    explicit Xml(int depth) : m_depth(depth) {}

    void open(std::wstring_view tag, std::wstring_view attributes = {}) {
        line(std::format(L"<{}{}{}>", tag, attributes.empty() ? L"" : L" ", attributes));
        ++m_depth;
    }
    void close(std::wstring_view tag) {
        --m_depth;
        line(std::format(L"</{}>", tag));
    }
    void leaf(std::wstring_view tag, std::wstring_view text) {
        line(std::format(L"<{0}>{1}</{0}>", tag, escape(text)));
    }
    // Separate names, not overloads: a wide literal would convert to bool before wstring_view.
    void flag(std::wstring_view tag, bool value) { leaf(tag, std::wstring_view(value ? L"true" : L"false")); }
    void number(std::wstring_view tag, int value) { leaf(tag, std::wstring_view(std::to_wstring(value))); }

    [[nodiscard]] bool empty() const noexcept { return m_text.empty(); }
    [[nodiscard]] const std::wstring& text() const noexcept { return m_text; }

private:
    void line(const std::wstring& content) {
        m_text.append(static_cast<std::size_t>(m_depth) * 2, L' ');
        m_text += content;
        m_text.push_back(L'\n');
    }
    std::wstring m_text;
    int m_depth;
};

constexpr int kBodyDepth = 3;

void password(Xml& xml, std::wstring_view value, std::wstring_view element = L"Password") {
    xml.open(element);
    if (value.empty()) {
        xml.leaf(L"Value", L"");
        xml.flag(L"PlainText", true);
    } else {
        xml.leaf(L"Value", encodeUnattendPassword(value, element));
        xml.flag(L"PlainText", false);
    }
    xml.close(element);
}

void localAccount(Xml& xml, std::wstring_view name, std::wstring_view secret, bool administrator) {
    xml.open(L"LocalAccount", L"wcm:action=\"add\"");
    password(xml, secret);
    xml.leaf(L"DisplayName", name);
    xml.leaf(L"Group", administrator ? L"Administrators" : L"Users");
    xml.leaf(L"Name", name);
    xml.close(L"LocalAccount");
}

void runSynchronous(Xml& xml, const std::vector<std::wstring>& commands) {
    if (commands.empty()) {
        return;
    }
    xml.open(L"RunSynchronous");
    int order = 0;
    for (const auto& command : commands) {
        xml.open(L"RunSynchronousCommand", L"wcm:action=\"add\"");
        xml.number(L"Order", ++order);
        xml.leaf(L"Path", command);
        xml.close(L"RunSynchronousCommand");
    }
    xml.close(L"RunSynchronous");
}

void international(Xml& xml, const UnattendOptions& o, bool setupLanguage) {
    if (setupLanguage && !o.uiLanguage.empty()) {
        xml.open(L"SetupUILanguage");
        xml.leaf(L"UILanguage", o.uiLanguage);
        xml.close(L"SetupUILanguage");
    }
    if (!o.keyboard.empty()) {
        xml.leaf(L"InputLocale", o.keyboard);
    }
    if (!o.locale.empty()) {
        xml.leaf(L"SystemLocale", o.locale);
    }
    if (!o.uiLanguage.empty()) {
        xml.leaf(L"UILanguage", o.uiLanguage);
    }
    if (!o.locale.empty()) {
        xml.leaf(L"UserLocale", o.locale);
    }
}

struct Partition {
    const wchar_t* type;
    int sizeMb; // 0 = the rest of the disk
    const wchar_t* format; // nullptr = not formatted (MSR)
    const wchar_t* label;
    bool active;
    bool windows;
    const wchar_t* typeId = nullptr; // ModifyPartition TypeID (the recovery partition)
};

std::vector<Partition> partitionLayout(UnattendDisk disk, bool recovery) {
    std::vector<Partition> layout;
    if (recovery) {
        // First on the disk, as in Microsoft's sample layouts: Windows can then grow to the end.
        layout.push_back({L"Primary", kRecoveryMb, L"NTFS", L"Recovery", false, false,
                          disk == UnattendDisk::WipeGpt ? kRecoveryGpt.data() : kRecoveryMbr.data()});
    }
    if (disk == UnattendDisk::WipeGpt) {
        layout.push_back({L"EFI", 300, L"FAT32", L"System", false, false});
        layout.push_back({L"MSR", 16, nullptr, nullptr, false, false});
    } else {
        layout.push_back({L"Primary", 500, L"NTFS", L"System", true, false});
    }
    layout.push_back({L"Primary", 0, L"NTFS", L"Windows", false, true});
    return layout;
}

void diskConfiguration(Xml& xml, UnattendDisk disk, int diskId, bool recovery) {
    const auto layout = partitionLayout(disk, recovery);
    xml.open(L"DiskConfiguration");
    xml.open(L"Disk", L"wcm:action=\"add\"");
    xml.number(L"DiskID", diskId);
    xml.flag(L"WillWipeDisk", true);
    xml.open(L"CreatePartitions");
    int order = 0;
    for (const auto& p : layout) {
        xml.open(L"CreatePartition", L"wcm:action=\"add\"");
        xml.number(L"Order", ++order);
        xml.leaf(L"Type", p.type);
        if (p.sizeMb > 0) {
            xml.number(L"Size", p.sizeMb);
        } else {
            xml.flag(L"Extend", true);
        }
        xml.close(L"CreatePartition");
    }
    xml.close(L"CreatePartitions");
    xml.open(L"ModifyPartitions");
    order = 0;
    for (const auto& p : layout) {
        xml.open(L"ModifyPartition", L"wcm:action=\"add\"");
        xml.number(L"Order", ++order);
        xml.number(L"PartitionID", order);
        if (p.format) {
            xml.leaf(L"Format", p.format);
            xml.leaf(L"Label", p.label);
        }
        if (p.active) {
            xml.flag(L"Active", true);
        }
        if (p.windows) {
            xml.leaf(L"Letter", L"C");
        }
        if (p.typeId) {
            xml.leaf(L"TypeID", p.typeId);
        }
        xml.close(L"ModifyPartition");
    }
    xml.close(L"ModifyPartitions");
    xml.close(L"Disk");
    xml.leaf(L"WillShowUI", L"OnError");
    xml.close(L"DiskConfiguration");
}

int windowsPartition(UnattendDisk disk, bool recovery) {
    return static_cast<int>(partitionLayout(disk, recovery).size());
}

bool truthy(const char* text) {
    return _stricmp(text, "true") == 0 || std::strcmp(text, "1") == 0;
}

std::wstring childText(const pugi::xml_node& node, const char* child) {
    return utf8::toWide(node.child(child).text().get());
}

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::wstring_view genericProductKey(std::wstring_view editionId) noexcept {
    for (const auto& known : kGenericKeys) {
        if (known.editionId == editionId) {
            return known.key;
        }
    }
    return {};
}

std::wstring encodeUnattendPassword(std::wstring_view value, std::wstring_view element) {
    std::wstring text(value);
    text += element;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size() * sizeof(wchar_t);
    std::wstring out;
    for (std::size_t i = 0; i < size; i += 3) {
        const unsigned b0 = bytes[i];
        const unsigned b1 = i + 1 < size ? bytes[i + 1] : 0;
        const unsigned b2 = i + 2 < size ? bytes[i + 2] : 0;
        out.push_back(static_cast<wchar_t>(kBase64[b0 >> 2]));
        out.push_back(static_cast<wchar_t>(kBase64[((b0 & 3) << 4) | (b1 >> 4)]));
        out.push_back(i + 1 < size ? static_cast<wchar_t>(kBase64[((b1 & 15) << 2) | (b2 >> 6)]) : L'=');
        out.push_back(i + 2 < size ? static_cast<wchar_t>(kBase64[b2 & 63]) : L'=');
    }
    return out;
}

std::wstring decodeUnattendPassword(std::wstring_view encoded, std::wstring_view element) {
    std::string digits; // the alphabet only: padding and line breaks are skipped
    for (const wchar_t c : encoded) {
        if (c < 128 && kBase64.find(static_cast<char>(c)) != std::string_view::npos) {
            digits.push_back(static_cast<char>(c));
        }
    }
    const auto bytes = base64Decode(digits);
    std::wstring text(bytes.size() / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), bytes.data(), text.size() * sizeof(wchar_t));
    if (text.ends_with(element)) {
        text.resize(text.size() - element.size());
    }
    return text;
}

std::wstring buildUnattendXml(const UnattendOptions& o) {
    const std::wstring componentAttributes = std::format(
        L"processorArchitecture=\"{}\" publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" versionScope=\"nonSxS\"",
        architectureAttribute(o.architecture));
    std::wstring out = L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       L"<unattend xmlns=\"urn:schemas-microsoft-com:unattend\" "
                       L"xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\">\n";
    auto pass = [&](std::wstring_view name, std::initializer_list<std::pair<std::wstring_view, const Xml*>> components) {
        if (std::ranges::all_of(components, [](const auto& c) { return c.second->empty(); })) {
            return;
        }
        out += std::format(L"  <settings pass=\"{}\">\n", name);
        for (const auto& [component, body] : components) {
            if (body->empty()) {
                continue;
            }
            out += std::format(L"    <component name=\"{}\" {}>\n", component, componentAttributes);
            out += body->text();
            out += L"    </component>\n";
        }
        out += L"  </settings>\n";
    };
    const bool anyLanguage = !o.uiLanguage.empty() || !o.locale.empty() || !o.keyboard.empty();

    // ---- windowsPE ----
    Xml peLanguage(kBodyDepth);
    if (anyLanguage) {
        international(peLanguage, o, /*setupLanguage=*/true);
    }
    Xml setup(kBodyDepth);
    {
        std::vector<std::wstring> commands;
        for (const auto& [enabled, value] : {std::pair{o.bypassTpm, L"BypassTPMCheck"},
                                             std::pair{o.bypassSecureBoot, L"BypassSecureBootCheck"},
                                             std::pair{o.bypassRam, L"BypassRAMCheck"},
                                             std::pair{o.bypassCpu, L"BypassCPUCheck"},
                                             std::pair{o.bypassStorage, L"BypassStorageCheck"}}) {
            if (enabled) {
                commands.push_back(labConfig(value));
            }
        }
        runSynchronous(setup, commands);
        if (o.disk != UnattendDisk::Ask) {
            diskConfiguration(setup, o.disk, o.diskId, o.recoveryPartition);
        }
        if (o.disk != UnattendDisk::Ask || o.imageIndex > 0 || o.compactOs) {
            setup.open(L"ImageInstall");
            setup.open(L"OSImage");
            if (o.imageIndex > 0) {
                setup.open(L"InstallFrom");
                setup.open(L"MetaData", L"wcm:action=\"add\"");
                setup.leaf(L"Key", L"/IMAGE/INDEX");
                setup.number(L"Value", o.imageIndex);
                setup.close(L"MetaData");
                setup.close(L"InstallFrom");
            }
            if (o.disk != UnattendDisk::Ask) {
                setup.open(L"InstallTo");
                setup.number(L"DiskID", o.diskId);
                setup.number(L"PartitionID", windowsPartition(o.disk, o.recoveryPartition));
                setup.close(L"InstallTo");
            }
            if (o.compactOs) {
                setup.flag(L"Compact", true);
            }
            setup.close(L"OSImage");
            setup.close(L"ImageInstall");
        }
        if (!o.productKey.empty() || o.acceptEula) {
            // Always with a key: the user's, else the generic one of the edition that will be
            // installed, else the placeholder that makes Setup show its key page.
            const std::wstring_view generic = genericProductKey(o.editionId);
            const bool ask = o.productKey.empty() && generic.empty();
            setup.open(L"UserData");
            setup.open(L"ProductKey");
            setup.leaf(L"Key", !o.productKey.empty() ? std::wstring_view(o.productKey) : ask ? kNoProductKey : generic);
            setup.leaf(L"WillShowUI", ask ? L"Always" : L"OnError");
            setup.close(L"ProductKey");
            if (o.acceptEula) {
                setup.flag(L"AcceptEula", true);
            }
            setup.close(L"UserData");
        }
    }
    pass(L"windowsPE", {{L"Microsoft-Windows-International-Core-WinPE", &peLanguage}, {L"Microsoft-Windows-Setup", &setup}});

    // ---- specialize ----
    Xml specializeShell(kBodyDepth);
    if (o.randomComputerName) {
        specializeShell.leaf(L"ComputerName", L"*");
    } else if (!o.computerName.empty()) {
        specializeShell.leaf(L"ComputerName", o.computerName);
    }
    if (!o.registeredOrganization.empty()) {
        specializeShell.leaf(L"RegisteredOrganization", o.registeredOrganization);
    }
    if (!o.registeredOwner.empty()) {
        specializeShell.leaf(L"RegisteredOwner", o.registeredOwner);
    }
    if (!o.timeZone.empty()) {
        specializeShell.leaf(L"TimeZone", o.timeZone);
    }
    Xml deployment(kBodyDepth);
    {
        std::vector<std::wstring> commands;
        for (const auto& [enabled, command] : {std::pair{o.bypassNro, kBypassNro}, std::pair{o.enableAdministrator, kEnableAdministrator},
                                               std::pair{o.passwordsNeverExpire, kNeverExpire}, std::pair{o.disableLockout, kNoLockout},
                                               std::pair{o.preventDeviceEncryption, kPreventEncryption}}) {
            if (enabled) {
                commands.emplace_back(command);
            }
        }
        for (const auto& command : o.specializeCommands) {
            if (!text::trim(command).empty()) {
                commands.emplace_back(text::trim(command));
            }
        }
        runSynchronous(deployment, commands);
    }
    pass(L"specialize",
         {{L"Microsoft-Windows-Shell-Setup", &specializeShell}, {L"Microsoft-Windows-Deployment", &deployment}});

    // ---- oobeSystem ----
    Xml oobeLanguage(kBodyDepth);
    if (anyLanguage) {
        international(oobeLanguage, o, /*setupLanguage=*/false);
    }
    Xml shell(kBodyDepth);
    if (o.autoLogon && !o.accountName.empty()) {
        shell.open(L"AutoLogon");
        password(shell, o.password);
        shell.flag(L"Enabled", true);
        shell.number(L"LogonCount", 1);
        shell.leaf(L"Username", o.accountName);
        shell.close(L"AutoLogon");
    }
    {
        std::vector<std::wstring> commands;
        for (const auto& command : o.firstLogonCommands) {
            if (!text::trim(command).empty()) {
                commands.emplace_back(text::trim(command));
            }
        }
        if (!commands.empty()) {
            shell.open(L"FirstLogonCommands");
            int order = 0;
            for (const auto& command : commands) {
                shell.open(L"SynchronousCommand", L"wcm:action=\"add\"");
                shell.leaf(L"CommandLine", command);
                shell.number(L"Order", ++order);
                shell.flag(L"RequiresUserInput", false);
                shell.close(L"SynchronousCommand");
            }
            shell.close(L"FirstLogonCommands");
        }
    }
    const bool hideWifi = o.skipOnlineAccount || o.hideWifiSetup;
    if (o.acceptEula || o.skipOnlineAccount || o.skipPrivacy || hideWifi || o.hideOemRegistration) {
        shell.open(L"OOBE");
        if (o.acceptEula) {
            shell.flag(L"HideEULAPage", true);
        }
        if (o.hideOemRegistration) {
            shell.flag(L"HideOEMRegistrationScreen", true);
        }
        if (o.skipOnlineAccount) {
            shell.flag(L"HideOnlineAccountScreens", true);
        }
        if (hideWifi) {
            shell.flag(L"HideWirelessSetupInOOBE", true);
        }
        if (o.skipPrivacy) {
            shell.number(L"ProtectYourPC", 3);
        }
        shell.close(L"OOBE");
    }
    const bool anyAccount = !o.accountName.empty() || std::ranges::any_of(o.extraAccounts, [](const auto& a) { return !a.name.empty(); });
    if (o.enableAdministrator || anyAccount) {
        shell.open(L"UserAccounts");
        if (o.enableAdministrator) {
            password(shell, o.administratorPassword, L"AdministratorPassword");
        }
        if (anyAccount) {
            shell.open(L"LocalAccounts");
            if (!o.accountName.empty()) {
                localAccount(shell, o.accountName, o.password, /*administrator=*/true);
            }
            for (const auto& account : o.extraAccounts) {
                if (!account.name.empty()) {
                    localAccount(shell, account.name, account.password, account.administrator);
                }
            }
            shell.close(L"LocalAccounts");
        }
        shell.close(L"UserAccounts");
    }
    pass(L"oobeSystem", {{L"Microsoft-Windows-International-Core", &oobeLanguage}, {L"Microsoft-Windows-Shell-Setup", &shell}});

    out += L"</unattend>\n";
    return out;
}

Result<UnattendOptions> parseUnattendXml(std::string_view utf8) {
    pugi::xml_document doc;
    const auto parsed = doc.load_buffer(utf8.data(), utf8.size(), pugi::parse_default, pugi::encoding_auto);
    const pugi::xml_node root = doc.child("unattend");
    if (!parsed || !root) {
        return fail(ErrorCode::ParseError, L"not an answer file (no <unattend> element)",
                    parsed ? std::wstring() : utf8::toWide(parsed.description()));
    }
    UnattendOptions o;
    auto readPassword = [](const pugi::xml_node& node) {
        const std::wstring value = childText(node, "Value");
        return truthy(node.child("PlainText").text().as_string("true")) ? value : decodeUnattendPassword(value);
    };
    auto commands = [](const pugi::xml_node& component, std::string_view needle) {
        for (const auto& command : component.child("RunSynchronous").children("RunSynchronousCommand")) {
            if (std::string_view(command.child("Path").text().get()).find(needle) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    };
    // The specialize commands WinLove writes for an option, read back as that option.
    auto ownCommand = [&](const std::wstring& path) {
        for (const auto& [flag, command] : {std::pair{&o.bypassNro, kBypassNro}, std::pair{&o.enableAdministrator, kEnableAdministrator},
                                            std::pair{&o.passwordsNeverExpire, kNeverExpire}, std::pair{&o.disableLockout, kNoLockout},
                                            std::pair{&o.preventDeviceEncryption, kPreventEncryption}}) {
            if (text::iequals(path, command)) {
                *flag = true;
                return true;
            }
        }
        return false;
    };
    auto language = [&](const pugi::xml_node& component) {
        auto take = [](std::wstring& target, std::wstring value) {
            if (target.empty()) {
                target = std::move(value);
            }
        };
        take(o.uiLanguage, childText(component.child("SetupUILanguage"), "UILanguage"));
        take(o.uiLanguage, childText(component, "UILanguage"));
        take(o.keyboard, childText(component, "InputLocale"));
        take(o.locale, childText(component, "UserLocale"));
        take(o.locale, childText(component, "SystemLocale"));
    };
    for (const auto& settings : root.children("settings")) {
        for (const auto& component : settings.children("component")) {
            const std::string_view name = component.attribute("name").as_string();
            const std::string_view arch = component.attribute("processorArchitecture").as_string();
            o.architecture = arch == "x86" ? Architecture::X86 : arch == "arm64" ? Architecture::Arm64
                             : arch == "arm" ? Architecture::Arm : Architecture::X64;
            if (name == "Microsoft-Windows-International-Core-WinPE" || name == "Microsoft-Windows-International-Core") {
                language(component);
            } else if (name == "Microsoft-Windows-Setup") {
                o.bypassTpm = o.bypassTpm || commands(component, "BypassTPMCheck");
                o.bypassSecureBoot = o.bypassSecureBoot || commands(component, "BypassSecureBootCheck");
                o.bypassRam = o.bypassRam || commands(component, "BypassRAMCheck");
                o.bypassCpu = o.bypassCpu || commands(component, "BypassCPUCheck");
                o.bypassStorage = o.bypassStorage || commands(component, "BypassStorageCheck");
                const auto disk = component.child("DiskConfiguration").child("Disk");
                if (disk && truthy(disk.child("WillWipeDisk").text().as_string("false"))) {
                    o.disk = UnattendDisk::WipeMbr;
                    o.diskId = disk.child("DiskID").text().as_int(0);
                    for (const auto& partition : disk.child("CreatePartitions").children("CreatePartition")) {
                        if (_stricmp(partition.child("Type").text().get(), "EFI") == 0) {
                            o.disk = UnattendDisk::WipeGpt;
                        }
                    }
                    for (const auto& partition : disk.child("ModifyPartitions").children("ModifyPartition")) {
                        const std::wstring type = childText(partition, "TypeID");
                        o.recoveryPartition = o.recoveryPartition || text::iequals(type, kRecoveryGpt) || text::iequals(type, kRecoveryMbr);
                    }
                }
                o.compactOs = o.compactOs ||
                              truthy(component.child("ImageInstall").child("OSImage").child("Compact").text().as_string("false"));
                for (const auto& meta : component.child("ImageInstall").child("OSImage").child("InstallFrom").children("MetaData")) {
                    if (_stricmp(meta.child("Key").text().get(), "/IMAGE/INDEX") == 0) {
                        o.imageIndex = meta.child("Value").text().as_int(0);
                    }
                }
                const auto user = component.child("UserData");
                o.productKey = childText(user.child("ProductKey"), "Key");
                // A generic key or the placeholder is not an answer of the user's: it is written
                // again for whatever edition the file is built for next.
                if (o.productKey == kNoProductKey || isGenericProductKey(o.productKey)) {
                    o.productKey.clear();
                }
                o.acceptEula = o.acceptEula || truthy(user.child("AcceptEula").text().as_string("false"));
            } else if (name == "Microsoft-Windows-Deployment") {
                for (const auto& command : component.child("RunSynchronous").children("RunSynchronousCommand")) {
                    const std::wstring path = childText(command, "Path");
                    if (!ownCommand(path) && path.find(L"BypassNRO") == std::wstring::npos && !path.empty()) {
                        o.specializeCommands.push_back(path);
                    }
                    o.bypassNro = o.bypassNro || path.find(L"BypassNRO") != std::wstring::npos;
                }
            } else if (name == "Microsoft-Windows-Shell-Setup") {
                if (const auto computer = childText(component, "ComputerName"); computer == L"*") {
                    o.randomComputerName = true;
                } else if (!computer.empty()) {
                    o.computerName = computer;
                }
                if (const auto owner = childText(component, "RegisteredOwner"); !owner.empty()) {
                    o.registeredOwner = owner;
                }
                if (const auto organization = childText(component, "RegisteredOrganization"); !organization.empty()) {
                    o.registeredOrganization = organization;
                }
                for (const auto& command : component.child("FirstLogonCommands").children("SynchronousCommand")) {
                    if (auto line = childText(command, "CommandLine"); !line.empty()) {
                        o.firstLogonCommands.push_back(std::move(line));
                    }
                }
                if (const auto zone = childText(component, "TimeZone"); !zone.empty()) {
                    o.timeZone = zone;
                }
                const auto oobe = component.child("OOBE");
                o.acceptEula = o.acceptEula || truthy(oobe.child("HideEULAPage").text().as_string("false"));
                o.skipOnlineAccount =
                    o.skipOnlineAccount || truthy(oobe.child("HideOnlineAccountScreens").text().as_string("false"));
                // Part of "skip the online account screens" when that is on; an option of its own otherwise.
                o.hideWifiSetup = (o.hideWifiSetup || truthy(oobe.child("HideWirelessSetupInOOBE").text().as_string("false"))) &&
                                  !o.skipOnlineAccount;
                o.hideOemRegistration =
                    o.hideOemRegistration || truthy(oobe.child("HideOEMRegistrationScreen").text().as_string("false"));
                o.skipPrivacy = o.skipPrivacy || oobe.child("ProtectYourPC").text().as_int(0) == 3;
                const auto accounts = component.child("UserAccounts");
                if (const auto admin = accounts.child("AdministratorPassword")) {
                    const std::wstring value = childText(admin, "Value");
                    o.administratorPassword = truthy(admin.child("PlainText").text().as_string("true"))
                                                  ? value
                                                  : decodeUnattendPassword(value, L"AdministratorPassword");
                }
                bool first = true;
                for (const auto& account : accounts.child("LocalAccounts").children("LocalAccount")) {
                    if (first) {
                        o.accountName = childText(account, "Name");
                        o.password = readPassword(account.child("Password"));
                        first = false;
                    } else {
                        o.extraAccounts.push_back({childText(account, "Name"), readPassword(account.child("Password")),
                                                   text::iequals(childText(account, "Group"), L"Administrators")});
                    }
                }
                o.autoLogon = o.autoLogon || truthy(component.child("AutoLogon").child("Enabled").text().as_string("false"));
            }
        }
    }
    return o;
}

std::vector<UnattendProblem> validateUnattend(const UnattendOptions& o) {
    std::vector<UnattendProblem> problems;
    auto hasAny = [](std::wstring_view text, std::wstring_view chars) {
        return text.find_first_of(chars) != std::wstring_view::npos;
    };
    if (!o.randomComputerName && !o.computerName.empty()) {
        const bool digitsOnly = std::ranges::all_of(o.computerName, [](wchar_t c) { return std::iswdigit(c) != 0; });
        // Setup's own list for ComputerName (spaces included).
        if (o.computerName.size() > 15 || digitsOnly || hasAny(o.computerName, L" {|}~[\\]^':;<=>?@!\"#$%`()+/.,*&")) {
            problems.push_back(UnattendProblem::ComputerName);
        }
    }
    auto badAccountName = [&](const std::wstring& name) {
        static constexpr std::wstring_view kReserved[] = {L"administrator", L"guest", L"defaultaccount", L"system",
                                                          L"wdagutilityaccount"};
        const std::wstring lower = text::lower(name);
        return name.size() > 20 || hasAny(name, L"\"/\\[]:;|=,+*?<>@") || name.ends_with(L'.') ||
               std::ranges::find(kReserved, lower) != std::end(kReserved);
    };
    if (!o.accountName.empty() && badAccountName(o.accountName)) {
        problems.push_back(UnattendProblem::AccountName);
    }
    {
        std::vector<std::wstring> seen{text::lower(o.accountName)};
        for (const auto& account : o.extraAccounts) {
            const std::wstring lower = text::lower(account.name);
            if (account.name.empty() || badAccountName(account.name) || std::ranges::find(seen, lower) != seen.end()) {
                problems.push_back(UnattendProblem::ExtraAccountName);
                break;
            }
            seen.push_back(lower);
        }
    }
    if (o.diskId < 0 || o.diskId > 63) {
        problems.push_back(UnattendProblem::DiskId);
    }
    if (!o.productKey.empty()) {
        bool valid = o.productKey.size() == 29;
        for (std::size_t i = 0; valid && i < o.productKey.size(); ++i) {
            valid = i % 6 == 5 ? o.productKey[i] == L'-' : std::iswalnum(o.productKey[i]) != 0 && o.productKey[i] < 128;
        }
        if (!valid) {
            problems.push_back(UnattendProblem::ProductKey);
        }
    }
    if (o.autoLogon && o.accountName.empty()) {
        problems.push_back(UnattendProblem::AutoLogonNeedsAccount);
    }
    return problems;
}

} // namespace wl::core
