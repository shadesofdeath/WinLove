#pragma once
// Wi-Fi networks the installed system knows from the start: a WLAN profile (XML) per network,
// added after setup with `netsh wlan add profile ... user=all` (PostSetupStep::Type::Wifi, SYSTEM)
// and deleted from the disk right after — it carries the key in clear text.
// Profiles are written by hand (SSID + key + security) or taken from this PC (WlanGetProfile;
// the key is only readable in clear text from an elevated process).
#include "base/Result.h"

#include <string>
#include <vector>

namespace wl::core {

enum class WifiSecurity : std::uint8_t { Wpa2Personal, Wpa3Personal, Open };

struct WifiNetwork {
    std::wstring ssid;
    std::wstring password; // 8–63 characters (WPA2 / WPA3); empty for Open
    WifiSecurity security = WifiSecurity::Wpa2Personal;
    bool hidden = false;   // does not broadcast its SSID
};

enum class WifiProblem : std::uint8_t { None, Ssid, Password };
[[nodiscard]] WifiProblem validateWifi(const WifiNetwork& network);

[[nodiscard]] std::wstring wifiProfileXml(const WifiNetwork& network);
// The <name> of a profile XML ("" when there is none) and whether its key is readable on another
// machine (protected = false).
[[nodiscard]] std::wstring wifiProfileName(std::wstring_view xml);
[[nodiscard]] bool wifiProfilePortable(std::wstring_view xml);
// SSID, key, security and "hidden" read back from a profile (ours or this PC's), for editing.
[[nodiscard]] WifiNetwork wifiNetworkFromXml(std::wstring_view xml);

struct HostWifiProfile {
    std::wstring name;
    std::wstring xml;
    bool portable = false; // key in clear text (elevated) or an open network
};
// This PC's profiles (all wireless interfaces, duplicates by name once). Empty without Wi-Fi.
[[nodiscard]] Result<std::vector<HostWifiProfile>> hostWifiProfiles();

} // namespace wl::core
