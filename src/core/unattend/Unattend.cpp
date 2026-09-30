#include "core/unattend/Unattend.h"

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
constexpr std::wstring_view kPasswordSuffix = L"Password";

const wchar_t* architectureAttribute(Architecture architecture) {
    switch (architecture) {
    case Architecture::X86: return L"x86";
    case Architecture::Arm: return L"arm";
    case Architecture::Arm64: return L"arm64";
    default: return L"amd64";
    }
}

std::wstring escape(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
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

void password(Xml& xml, std::wstring_view value) {
    xml.open(L"Password");
    if (value.empty()) {
        xml.leaf(L"Value", L"");
        xml.flag(L"PlainText", true);
    } else {
        xml.leaf(L"Value", encodeUnattendPassword(value));
        xml.flag(L"PlainText", false);
    }
    xml.close(L"Password");
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
};

void diskConfiguration(Xml& xml, UnattendDisk disk) {
    static constexpr Partition kGpt[] = {{L"EFI", 300, L"FAT32", L"System", false, false},
                                         {L"MSR", 16, nullptr, nullptr, false, false},
                                         {L"Primary", 0, L"NTFS", L"Windows", false, true}};
    static constexpr Partition kMbr[] = {{L"Primary", 500, L"NTFS", L"System", true, false},
                                         {L"Primary", 0, L"NTFS", L"Windows", false, true}};
    const auto layout = disk == UnattendDisk::WipeGpt ? std::span<const Partition>(kGpt) : std::span<const Partition>(kMbr);
    xml.open(L"DiskConfiguration");
    xml.leaf(L"WillShowUI", L"OnError");
    xml.open(L"Disk", L"wcm:action=\"add\"");
    xml.number(L"DiskID", 0);
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
        xml.close(L"ModifyPartition");
    }
    xml.close(L"ModifyPartitions");
    xml.close(L"Disk");
    xml.close(L"DiskConfiguration");
}

int windowsPartition(UnattendDisk disk) {
    return disk == UnattendDisk::WipeGpt ? 3 : 2;
}

bool truthy(const char* text) {
    return _stricmp(text, "true") == 0 || std::strcmp(text, "1") == 0;
}

std::wstring childText(const pugi::xml_node& node, const char* child) {
    return utf8::toWide(node.child(child).text().get());
}

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::wstring encodeUnattendPassword(std::wstring_view value) {
    std::wstring text(value);
    text += kPasswordSuffix;
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

std::wstring decodeUnattendPassword(std::wstring_view encoded) {
    std::vector<unsigned char> bytes;
    unsigned buffer = 0;
    int bits = 0;
    for (const wchar_t c : encoded) {
        const auto at = c < 128 ? kBase64.find(static_cast<char>(c)) : std::string_view::npos;
        if (at == std::string_view::npos) {
            continue; // padding, line breaks
        }
        buffer = (buffer << 6) | static_cast<unsigned>(at);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    std::wstring text(bytes.size() / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), bytes.data(), text.size() * sizeof(wchar_t));
    if (text.ends_with(kPasswordSuffix)) {
        text.resize(text.size() - kPasswordSuffix.size());
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
                                             std::pair{o.bypassRam, L"BypassRAMCheck"}}) {
            if (enabled) {
                commands.push_back(labConfig(value));
            }
        }
        runSynchronous(setup, commands);
        if (o.disk != UnattendDisk::Ask) {
            diskConfiguration(setup, o.disk);
        }
        if (o.disk != UnattendDisk::Ask || o.imageIndex > 0) {
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
                setup.number(L"DiskID", 0);
                setup.number(L"PartitionID", windowsPartition(o.disk));
                setup.close(L"InstallTo");
            }
            setup.close(L"OSImage");
            setup.close(L"ImageInstall");
        }
        if (!o.productKey.empty() || o.acceptEula) {
            setup.open(L"UserData");
            if (!o.productKey.empty()) {
                setup.open(L"ProductKey");
                setup.leaf(L"Key", o.productKey);
                setup.leaf(L"WillShowUI", L"OnError");
                setup.close(L"ProductKey");
            }
            if (o.acceptEula) {
                setup.flag(L"AcceptEula", true);
            }
            setup.close(L"UserData");
        }
    }
    pass(L"windowsPE", {{L"Microsoft-Windows-International-Core-WinPE", &peLanguage}, {L"Microsoft-Windows-Setup", &setup}});

    // ---- specialize ----
    Xml specializeShell(kBodyDepth);
    if (!o.computerName.empty()) {
        specializeShell.leaf(L"ComputerName", o.computerName);
    }
    if (!o.timeZone.empty()) {
        specializeShell.leaf(L"TimeZone", o.timeZone);
    }
    Xml deployment(kBodyDepth);
    if (o.bypassNro) {
        runSynchronous(deployment, {std::wstring(kBypassNro)});
    }
    pass(L"specialize",
         {{L"Microsoft-Windows-Shell-Setup", &specializeShell}, {L"Microsoft-Windows-Deployment", &deployment}});

    // ---- oobeSystem ----
    Xml oobeLanguage(kBodyDepth);
    if (anyLanguage) {
        international(oobeLanguage, o, /*setupLanguage=*/false);
    }
    Xml shell(kBodyDepth);
    if (o.acceptEula || o.skipOnlineAccount || o.skipPrivacy) {
        shell.open(L"OOBE");
        if (o.acceptEula) {
            shell.flag(L"HideEULAPage", true);
        }
        if (o.skipOnlineAccount) {
            shell.flag(L"HideOnlineAccountScreens", true);
            shell.flag(L"HideWirelessSetupInOOBE", true);
        }
        if (o.skipPrivacy) {
            shell.number(L"ProtectYourPC", 3);
        }
        shell.close(L"OOBE");
    }
    if (!o.accountName.empty()) {
        shell.open(L"UserAccounts");
        shell.open(L"LocalAccounts");
        shell.open(L"LocalAccount", L"wcm:action=\"add\"");
        shell.leaf(L"Name", o.accountName);
        shell.leaf(L"DisplayName", o.accountName);
        shell.leaf(L"Group", L"Administrators");
        password(shell, o.password);
        shell.close(L"LocalAccount");
        shell.close(L"LocalAccounts");
        shell.close(L"UserAccounts");
        if (o.autoLogon) {
            shell.open(L"AutoLogon");
            shell.flag(L"Enabled", true);
            shell.leaf(L"Username", o.accountName);
            password(shell, o.password);
            shell.number(L"LogonCount", 1);
            shell.close(L"AutoLogon");
        }
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
                const auto disk = component.child("DiskConfiguration").child("Disk");
                if (disk && truthy(disk.child("WillWipeDisk").text().as_string("false"))) {
                    o.disk = UnattendDisk::WipeMbr;
                    for (const auto& partition : disk.child("CreatePartitions").children("CreatePartition")) {
                        if (_stricmp(partition.child("Type").text().get(), "EFI") == 0) {
                            o.disk = UnattendDisk::WipeGpt;
                        }
                    }
                }
                for (const auto& meta : component.child("ImageInstall").child("OSImage").child("InstallFrom").children("MetaData")) {
                    if (_stricmp(meta.child("Key").text().get(), "/IMAGE/INDEX") == 0) {
                        o.imageIndex = meta.child("Value").text().as_int(0);
                    }
                }
                const auto user = component.child("UserData");
                o.productKey = childText(user.child("ProductKey"), "Key");
                o.acceptEula = o.acceptEula || truthy(user.child("AcceptEula").text().as_string("false"));
            } else if (name == "Microsoft-Windows-Deployment") {
                o.bypassNro = o.bypassNro || commands(component, "BypassNRO");
            } else if (name == "Microsoft-Windows-Shell-Setup") {
                if (const auto computer = childText(component, "ComputerName"); !computer.empty()) {
                    o.computerName = computer;
                }
                if (const auto zone = childText(component, "TimeZone"); !zone.empty()) {
                    o.timeZone = zone;
                }
                const auto oobe = component.child("OOBE");
                o.acceptEula = o.acceptEula || truthy(oobe.child("HideEULAPage").text().as_string("false"));
                o.skipOnlineAccount =
                    o.skipOnlineAccount || truthy(oobe.child("HideOnlineAccountScreens").text().as_string("false"));
                o.skipPrivacy = o.skipPrivacy || oobe.child("ProtectYourPC").text().as_int(0) == 3;
                if (const auto account = component.child("UserAccounts").child("LocalAccounts").child("LocalAccount")) {
                    o.accountName = childText(account, "Name");
                    o.password = readPassword(account.child("Password"));
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
    if (!o.computerName.empty()) {
        const bool digitsOnly = std::ranges::all_of(o.computerName, [](wchar_t c) { return std::iswdigit(c) != 0; });
        // Setup's own list for ComputerName (spaces included).
        if (o.computerName.size() > 15 || digitsOnly || hasAny(o.computerName, L" {|}~[\\]^':;<=>?@!\"#$%`()+/.,*&")) {
            problems.push_back(UnattendProblem::ComputerName);
        }
    }
    if (!o.accountName.empty()) {
        static constexpr std::wstring_view kReserved[] = {L"administrator", L"guest", L"defaultaccount", L"system",
                                                          L"wdagutilityaccount"};
        std::wstring lower = o.accountName;
        std::ranges::transform(lower, lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (o.accountName.size() > 20 || hasAny(o.accountName, L"\"/\\[]:;|=,+*?<>@") || o.accountName.ends_with(L'.') ||
            std::ranges::find(kReserved, lower) != std::end(kReserved)) {
            problems.push_back(UnattendProblem::AccountName);
        }
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
