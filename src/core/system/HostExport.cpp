#include "core/system/HostExport.h"

#include <windows.h>

#include <setupapi.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <vector>

#include "base/Log.h"
#include "core/system/Privileges.h"
#include "core/system/Process.h"

#include <format>

namespace wl::core {

namespace {
// D-114: the third-party packages the PC's present devices run on ("oem12.inf"): their driver key's
// InfPath. Stale packages (an older graphics driver …) stay behind: they are what made the export slow.
std::vector<std::wstring> presentDeviceOemInfs() {
    std::set<std::wstring> infs;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return {};
    }
    SP_DEVINFO_DATA device{sizeof(SP_DEVINFO_DATA)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &device); ++i) {
        HKEY key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) {
            continue;
        }
        wchar_t inf[MAX_PATH] = {};
        DWORD size = sizeof(inf);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"InfPath", nullptr, &type, reinterpret_cast<BYTE*>(inf), &size) == ERROR_SUCCESS && type == REG_SZ) {
            std::wstring name(inf);
            for (auto& c : name) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            if (name.starts_with(L"oem") && name.ends_with(L".inf")) {
                infs.insert(std::move(name));
            }
        }
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(set);
    return {infs.begin(), infs.end()};
}
} // namespace

Result<int> exportHostDrivers(const std::filesystem::path& folder, const TaskContext& task) {
    if (!isElevated()) {
        return fail(ErrorCode::AccessDenied, L"exporting this PC's drivers needs an elevated (administrator) process");
    }
    auto pnputil = systemTool(L"pnputil.exe");
    if (!pnputil) {
        return std::unexpected(pnputil.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot create folder", folder.wstring(), ec.value());
    }
    task.report(-1.0, L"export");
    std::string output;
    auto keep = [&](std::string_view chunk) {
        output.append(chunk);
        if (output.size() > 65536) {
            output.erase(0, output.size() - 32768);
        }
    };
    Result<std::uint32_t> exit = 0u;
    const auto inUse = presentDeviceOemInfs();
    if (!inUse.empty()) {
        // Only what the devices of this PC use, each package into its own folder.
        std::uint32_t worst = 0;
        for (std::size_t i = 0; i < inUse.size(); ++i) {
            if (auto go = task.cancel.check(); !go) {
                return std::unexpected(go.error());
            }
            const std::filesystem::path target = folder / std::filesystem::path(inUse[i]).stem();
            std::filesystem::create_directories(target, ec);
            auto one = runProcess(std::format(L"\"{}\" /export-driver {} \"{}\"", *pnputil, inUse[i], target.wstring()), keep);
            if (!one) {
                return std::unexpected(one.error());
            }
            worst = std::max(worst, *one);
            task.report(static_cast<double>(i + 1) / static_cast<double>(inUse.size()), inUse[i]);
        }
        exit = worst;
        log::info("drivers", std::format(L"{} driver package(s) in use by this PC's devices", inUse.size()));
    } else {
        exit = runProcess(std::format(L"\"{}\" /export-driver * \"{}\"", *pnputil, folder.wstring()), keep);
    }
    if (!exit) {
        return std::unexpected(exit.error());
    }
    int packages = 0;
    for (auto it = std::filesystem::directory_iterator(folder, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        for (auto inner = std::filesystem::directory_iterator(it->path(), ec);
             !ec && inner != std::filesystem::directory_iterator(); inner.increment(ec)) {
            if (_wcsicmp(inner->path().extension().c_str(), L".inf") == 0) {
                ++packages;
                break;
            }
        }
    }
    // pnputil fails the whole run for one package it cannot export and still exports the rest.
    if (*exit != 0 && packages == 0) {
        return fail(ErrorCode::IoError, L"pnputil could not export the drivers of this PC", folder.wstring(),
                    static_cast<std::int32_t>(*exit));
    }
    log::info("drivers", std::format(L"{} driver package(s) of this PC exported to {} (pnputil exit {})", packages,
                                     folder.wstring(), *exit));
    task.report(1.0, L"export");
    return packages;
}

} // namespace wl::core
