#pragma once
// D-114: a device setup class as Windows itself shows it (the reference tool's driver tree does the same): the
// class description in this PC's language (SetupDiGetClassDescription — "Ağ bağdaştırıcıları",
// "Depolama denetleyicileri") and the class icon from the class key (Control\Class\{GUID}: IconPath
// "%SystemRoot%\System32\setupapi.dll,-5", or Icon "-5" → setupapi.dll). Read once per class.
#include <string>
#include <string_view>

namespace wl::core {

struct DeviceClassInfo {
    std::wstring guid;        // "{4D36E972-E325-11CE-BFC1-08002BE10318}" (upper case), empty when unknown
    std::wstring description; // localized; empty when Windows does not know the class
    std::wstring iconFile;    // a file with the icon resource, empty for none
    int iconIndex = 0;        // ExtractIcon index (negative: a resource id)
};

// By GUID ("{…}") or, without one, by the INF class name ("Net", "SCSIAdapter").
[[nodiscard]] const DeviceClassInfo& deviceClassInfo(std::wstring_view classGuid, std::wstring_view className);

} // namespace wl::core
