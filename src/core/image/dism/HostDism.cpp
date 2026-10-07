#include "core/image/dism/HostDism.h"

#include "base/Log.h"
#include "core/system/Files.h"
#include "core/system/Signature.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <format>
#include <mutex>

namespace wl::core {

namespace {

std::filesystem::path systemFolder() {
    wchar_t buffer[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::filesystem::path(buffer) : std::filesystem::path(L"C:\\Windows\\System32");
}

// What dismapi.dll needs next to it (System32\Dism) to open, mount and read an image at all.
constexpr std::array kDismCore{L"DismCore.dll", L"DismProv.dll", L"WimProvider.dll", L"FolderProvider.dll",
                               L"ImagingProvider.dll", L"LogProvider.dll"};
// The rest of what WinLove asks DISM for: packages, apps, languages, answer files, drivers.
constexpr std::array kDismMore{L"CbsProvider.dll", L"OSProvider.dll", L"AppxProvider.dll", L"IntlProvider.dll",
                               L"SmiProvider.dll", L"UnattendProvider.dll"};

std::atomic<bool> g_forceAdk{false};

std::optional<std::wstring> kitsRoot() {
    for (const DWORD view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
        wchar_t buffer[MAX_PATH] = {};
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
                         RRF_RT_REG_SZ | view, nullptr, buffer, &size) == ERROR_SUCCESS &&
            buffer[0] != 0) {
            return std::wstring(buffer);
        }
    }
    return std::nullopt;
}

bool microsoftName(const std::wstring& signer) {
    return signer == L"Microsoft Windows" || signer == L"Microsoft Corporation" || signer.starts_with(L"Microsoft Windows ");
}

} // namespace

std::optional<std::filesystem::path> adkDismFolder() {
    static const std::optional<std::filesystem::path> folder = []() -> std::optional<std::filesystem::path> {
        const auto root = kitsRoot();
        if (!root) {
            return std::nullopt;
        }
#if defined(_M_ARM64)
        constexpr const wchar_t* kArch = L"arm64";
#else
        constexpr const wchar_t* kArch = L"amd64";
#endif
        const std::filesystem::path dism =
            std::filesystem::path(*root) / L"Assessment and Deployment Kit" / L"Deployment Tools" / kArch / L"DISM";
        for (const wchar_t* file : {L"dismapi.dll", L"wimgapi.dll", L"dism.exe"}) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(dism / file, ec)) {
                return std::nullopt;
            }
            if (!signedByMicrosoft(dism / file)) {
                return std::nullopt;
            }
        }
        log::info("dism", std::format(L"Windows ADK DISM {} at {}", fileVersion(dism / L"dismapi.dll"), dism.wstring()));
        return dism;
    }();
    return folder;
}

void forceAdkDism() {
    g_forceAdk = true;
}

const DismLocation& dismLocation() {
    static const DismLocation location = [] {
        const std::filesystem::path system = systemFolder();
        DismLocation l{system / L"dismapi.dll", system / L"dism.exe", false, {}};
        std::wstring missing;
        std::error_code ec;
        for (const auto& file : {system / L"dismapi.dll", system / L"dism.exe"}) {
            if (missing.empty() && !std::filesystem::is_regular_file(file, ec)) {
                missing = file.wstring();
            }
        }
        for (const wchar_t* name : kDismCore) {
            if (missing.empty() && !std::filesystem::is_regular_file(system / L"Dism" / name, ec)) {
                missing = (system / L"Dism" / name).wstring();
            }
        }
        if (!g_forceAdk && missing.empty()) {
            return l;
        }
        const auto adk = adkDismFolder();
        if (!adk) {
            if (!missing.empty()) {
                log::warn("dism", std::format(L"this PC's DISM is missing {} and the Windows ADK is not installed", missing));
            }
            return l;
        }
        l.dismapi = *adk / L"dismapi.dll";
        l.dismExe = *adk / L"dism.exe";
        l.adk = true;
        l.reason = missing.empty() ? std::wstring(L"asked for (--dism=adk)") : std::format(L"{} is missing", missing);
        log::warn("dism", std::format(L"using the Windows ADK's DISM ({}): {}", l.reason, adk->wstring()));
        return l;
    }();
    return location;
}

HostDismReport checkHostDism() {
    HostDismReport report;
    const std::filesystem::path system = systemFolder();
    report.dismVersion = fileVersion(system / L"dismapi.dll");
    report.wimgapiVersion = fileVersion(system / L"wimgapi.dll");
    report.adk = adkDismFolder();
    report.usingAdk = dismLocation().adk;
    // A Windows whose own kernel32.dll does not verify cannot check catalog signatures at all
    // (catalog database removed or broken): then only what is missing is reported.
    report.signaturesChecked = microsoftName(trustedSigner(system / L"kernel32.dll"));

    std::vector<std::filesystem::path> files{system / L"dismapi.dll", system / L"wimgapi.dll", system / L"dism.exe"};
    for (const wchar_t* name : kDismCore) {
        files.push_back(system / L"Dism" / name);
    }
    for (const wchar_t* name : kDismMore) {
        files.push_back(system / L"Dism" / name);
    }
    files.push_back(system / L"drivers" / L"wimmount.sys");
    for (const auto& file : files) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            report.problems.push_back({file.wstring(), L"missing"});
            continue;
        }
        if (report.signaturesChecked) {
            if (const std::wstring signer = trustedSigner(file); !microsoftName(signer)) {
                report.problems.push_back(
                    {file.wstring(), signer.empty() ? std::wstring(L"not signed") : std::format(L"signed by {}", signer)});
            }
        }
    }
    // The filter driver every mount goes through.
    DWORD start = 0;
    DWORD size = sizeof(start);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\WIMMount", L"Start",
                                        RRF_RT_REG_DWORD, nullptr, &start, &size);
    if (status != ERROR_SUCCESS) {
        report.problems.push_back({L"WIMMount", L"service missing"});
    } else if (start == SERVICE_DISABLED) {
        report.problems.push_back({L"WIMMount", L"service disabled"});
    }
    for (const auto& problem : report.problems) {
        log::warn("dism", std::format(L"host DISM: {} — {}", problem.file, problem.what));
    }
    log::info("dism", std::format(L"host DISM {} / wimgapi {}: {} problem(s){}{}", report.dismVersion, report.wimgapiVersion,
                                  report.problems.size(), report.signaturesChecked ? L"" : L", signatures not checkable",
                                  report.adk ? L", ADK installed" : L""));
    return report;
}

} // namespace wl::core
