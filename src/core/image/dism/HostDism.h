#pragma once
// The DISM and WIM libraries WinLove works with, and whether this PC's are whole. WinLove loads
// dismapi.dll / wimgapi.dll from System32 (D-017); a "modded" Windows may have removed or replaced
// parts of them (a user's ESD → WIM failed with 0x8007000B, 2026-10-07). Here: the files are
// checked (present, signed by Microsoft), and when this PC's DISM is missing a part, the Windows
// ADK's copy is used if it is installed — Microsoft's own way to service images from a host whose
// DISM will not do. wimgapi picks its copy per file (WimGapi.h). D-081.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wl::core {

// The ADK's DISM folder (…\Assessment and Deployment Kit\Deployment Tools\amd64\DISM) when it has
// dismapi.dll, wimgapi.dll and dism.exe signed by Microsoft. Looked up once.
[[nodiscard]] std::optional<std::filesystem::path> adkDismFolder();

// Where dismapi.dll and dism.exe come from: System32 unless a part of this PC's DISM is missing and
// the ADK's is installed (or forceAdkDism was called). Decided once, on the first use.
struct DismLocation {
    std::filesystem::path dismapi;
    std::filesystem::path dismExe;
    bool adk = false;
    std::wstring reason; // why the ADK's: the first missing file
};
[[nodiscard]] const DismLocation& dismLocation();
// wlcli --dism=adk: the ADK's DISM even on a sound PC (to prove it works). Before the first DISM call.
void forceAdkDism();

struct HostDismReport {
    struct Problem {
        std::wstring file;   // a path, or a service name
        std::wstring what;   // "missing", "not signed by Microsoft (…)", "disabled"
    };
    std::vector<Problem> problems;
    std::wstring dismVersion;          // System32\dismapi.dll
    std::wstring wimgapiVersion;       // System32\wimgapi.dll
    bool signaturesChecked = false;    // false when this PC cannot check even its own kernel32.dll
    std::optional<std::filesystem::path> adk;
    bool usingAdk = false;             // dismLocation() chose the ADK's

    [[nodiscard]] bool healthy() const noexcept { return problems.empty(); }
};
// Reads the signatures of ~15 files (a second or so): off the UI thread.
[[nodiscard]] HostDismReport checkHostDism();

} // namespace wl::core
