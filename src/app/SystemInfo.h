#pragma once
// Facts about this build and this machine that the Settings and About pages show.
#include <string>

namespace wl::app {

// The DISM library the engine loads (D-017): "C:\Windows\System32\dismapi.dll", or the Windows
// ADK's when this PC's DISM is missing a part (D-081).
[[nodiscard]] std::wstring dismLibraryPath();
// Its file version, "10.0.26100.1"; empty when it cannot be read.
[[nodiscard]] std::wstring dismLibraryVersion();
[[nodiscard]] bool dismFromAdk();
// The day this executable was compiled, "2026.09.30".
[[nodiscard]] std::wstring buildDate();
// The architecture this executable was built for ("x64").
[[nodiscard]] std::wstring buildArchitecture();

} // namespace wl::app
