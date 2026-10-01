#include "core/postsetup/Wifi.h"

#include "base/Encoding.h"
#include "base/Utf8.h"

#include <windows.h>

#include <wlanapi.h>

#include <algorithm>
#include <cwchar>
#include <format>

namespace wl::core {

namespace {

// Text of the first <tag>…</tag> (no nesting needed for the elements read here).
std::wstring firstElement(std::wstring_view xml, std::wstring_view tag) {
    const std::wstring open = L"<" + std::wstring(tag) + L">";
    const std::wstring close = L"</" + std::wstring(tag) + L">";
    const auto a = xml.find(open);
    if (a == std::wstring_view::npos) {
        return {};
    }
    const auto b = xml.find(close, a + open.size());
    return b == std::wstring_view::npos ? std::wstring() : std::wstring(xml.substr(a + open.size(), b - a - open.size()));
}

} // namespace

WifiProblem validateWifi(const WifiNetwork& network) {
    const std::size_t bytes = utf8::fromWide(network.ssid).size();
    if (bytes == 0 || bytes > 32) {
        return WifiProblem::Ssid;
    }
    if (network.security != WifiSecurity::Open) {
        const std::size_t n = network.password.size();
        const bool ascii = std::ranges::all_of(network.password, [](wchar_t c) { return c >= 32 && c < 127; });
        if (n < 8 || n > 63 || !ascii) {
            return WifiProblem::Password;
        }
    }
    return WifiProblem::None;
}

std::wstring wifiProfileXml(const WifiNetwork& n) {
    const std::wstring ssid = xmlEscape(n.ssid);
    std::wstring auth = L"WPA2PSK";
    std::wstring encryption = L"AES";
    if (n.security == WifiSecurity::Wpa3Personal) {
        auth = L"WPA3SAE";
    } else if (n.security == WifiSecurity::Open) {
        auth = L"open";
        encryption = L"none";
    }
    std::wstring xml = L"<?xml version=\"1.0\"?>\r\n"
                       L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n";
    xml += std::format(L"  <name>{}</name>\r\n", ssid);
    xml += std::format(L"  <SSIDConfig>\r\n    <SSID>\r\n      <name>{}</name>\r\n    </SSID>\r\n", ssid);
    if (n.hidden) {
        xml += L"    <nonBroadcast>true</nonBroadcast>\r\n";
    }
    xml += L"  </SSIDConfig>\r\n  <connectionType>ESS</connectionType>\r\n  <connectionMode>auto</connectionMode>\r\n";
    xml += L"  <MSM>\r\n    <security>\r\n      <authEncryption>\r\n";
    xml += std::format(L"        <authentication>{}</authentication>\r\n        <encryption>{}</encryption>\r\n", auth,
                       encryption);
    xml += L"        <useOneX>false</useOneX>\r\n      </authEncryption>\r\n";
    if (n.security != WifiSecurity::Open) {
        xml += std::format(L"      <sharedKey>\r\n        <keyType>passPhrase</keyType>\r\n        <protected>false</protected>\r\n"
                           L"        <keyMaterial>{}</keyMaterial>\r\n      </sharedKey>\r\n",
                           xmlEscape(n.password));
    }
    xml += L"    </security>\r\n  </MSM>\r\n</WLANProfile>\r\n";
    return xml;
}

std::wstring wifiProfileName(std::wstring_view xml) {
    return firstElement(xml, L"name");
}

bool wifiProfilePortable(std::wstring_view xml) {
    if (xml.find(L"<sharedKey>") == std::wstring_view::npos) {
        return true; // open network or 802.1X: no key in the profile
    }
    return firstElement(xml, L"protected") != L"true";
}

namespace {
std::wstring xmlUnescape(std::wstring text) {
    for (const auto& [from, to] : {std::pair{L"&lt;", L"<"}, std::pair{L"&gt;", L">"}, std::pair{L"&quot;", L"\""},
                                   std::pair{L"&apos;", L"'"}, std::pair{L"&amp;", L"&"}}) {
        for (std::size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + 1)) {
            text.replace(at, std::wcslen(from), to);
        }
    }
    return text;
}
} // namespace

WifiNetwork wifiNetworkFromXml(std::wstring_view xml) {
    WifiNetwork n;
    n.ssid = xmlUnescape(firstElement(xml, L"name"));
    n.password = xmlUnescape(firstElement(xml, L"keyMaterial"));
    const std::wstring auth = firstElement(xml, L"authentication");
    n.security = auth == L"open" ? WifiSecurity::Open : auth.starts_with(L"WPA3") ? WifiSecurity::Wpa3Personal
                                                                                  : WifiSecurity::Wpa2Personal;
    n.hidden = firstElement(xml, L"nonBroadcast") == L"true";
    return n;
}

Result<std::vector<HostWifiProfile>> hostWifiProfiles() {
    DWORD version = 0;
    HANDLE client = nullptr;
    DWORD rc = WlanOpenHandle(2, nullptr, &version, &client);
    if (rc != ERROR_SUCCESS) {
        // No WLAN service (desktop without Wi-Fi): nothing to offer, not an error.
        return std::vector<HostWifiProfile>{};
    }
    std::vector<HostWifiProfile> profiles;
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces) {
        for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
            const GUID& id = interfaces->InterfaceInfo[i].InterfaceGuid;
            PWLAN_PROFILE_INFO_LIST list = nullptr;
            if (WlanGetProfileList(client, &id, nullptr, &list) != ERROR_SUCCESS || !list) {
                continue;
            }
            for (DWORD p = 0; p < list->dwNumberOfItems; ++p) {
                const std::wstring name = list->ProfileInfo[p].strProfileName;
                if (std::ranges::any_of(profiles, [&](const HostWifiProfile& h) { return h.name == name; })) {
                    continue;
                }
                LPWSTR xml = nullptr;
                DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                DWORD access = 0;
                if (WlanGetProfile(client, &id, name.c_str(), nullptr, &xml, &flags, &access) == ERROR_SUCCESS && xml) {
                    HostWifiProfile h{name, xml, false};
                    h.portable = wifiProfilePortable(h.xml);
                    profiles.push_back(std::move(h));
                    WlanFreeMemory(xml);
                }
            }
            WlanFreeMemory(list);
        }
        WlanFreeMemory(interfaces);
    }
    WlanCloseHandle(client, nullptr);
    std::ranges::sort(profiles, {}, &HostWifiProfile::name);
    return profiles;
}

} // namespace wl::core
