#include "core/system/HostExport.h"

#include "base/Log.h"
#include "core/system/Privileges.h"
#include "core/system/Process.h"

#include <format>

namespace wl::core {

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
    auto exit = runProcess(std::format(L"\"{}\" /export-driver * \"{}\"", *pnputil, folder.wstring()),
                           [&](std::string_view chunk) {
                               output.append(chunk);
                               if (output.size() > 65536) {
                                   output.erase(0, output.size() - 32768);
                               }
                           });
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
