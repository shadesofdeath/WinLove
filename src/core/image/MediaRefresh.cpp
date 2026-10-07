#include "core/image/MediaRefresh.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/system/Process.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <map>

namespace wl::core {

Result<std::vector<std::filesystem::path>> expandSetupDynamicUpdate(const std::filesystem::path& cab,
                                                                    const std::filesystem::path& folder,
                                                                    const TaskContext& task) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(cab, ec)) {
        return fail(ErrorCode::NotFound, L"no Setup dynamic update file", cab.wstring());
    }
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot create folder", folder.wstring(), ec.value());
    }
    auto expand = systemTool(L"expand.exe");
    if (!expand) {
        return std::unexpected(expand.error());
    }
    task.report(0.0, L"Setup");
    std::string output;
    auto run = runProcess(L"\"" + *expand + L"\" \"" + cab.wstring() + L"\" -F:* \"" + folder.wstring() + L"\"",
                          [&](std::string_view text) { output.append(text.substr(0, 4096 - std::min<std::size_t>(output.size(), 4096))); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (*run != 0) {
        return fail(ErrorCode::IoError, std::format(L"expand.exe could not unpack the Setup dynamic update (exit {})", *run),
                    cab.wstring());
    }
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file()) {
            files.push_back(it->path());
        }
    }
    if (files.empty()) {
        return fail(ErrorCode::ParseError, L"the Setup dynamic update has no files", cab.wstring());
    }
    log::info("media", std::format(L"Setup dynamic update: {} file(s) from {}", files.size(), cab.filename().wstring()));
    task.report(1.0, L"Setup");
    return files;
}

std::vector<MediaFile> mediaRefreshFiles(const std::filesystem::path& setupFolder, const std::filesystem::path& setupDu,
                                         const std::filesystem::path& saved) {
    std::map<std::wstring, MediaFile> byPath; // lower-case path → entry: a later one wins
    std::error_code ec;
    auto put = [&](std::wstring path, const std::filesystem::path& file) {
        const bool exists = std::filesystem::exists(setupFolder / path, ec);
        const std::wstring key = text::lower(path); // before the move: an assignment evaluates its right side first
        byPath[key] = MediaFile{std::move(path), file, !exists};
    };
    if (!setupDu.empty() && std::filesystem::is_directory(setupDu, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(setupDu, ec); !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            const auto relative = std::filesystem::relative(it->path(), setupDu, ec);
            // The update carries every language's resources (ar-sa\, de-de\ …): only those of the
            // languages the media has are of use — and a folder the media lacks cannot be added to.
            if (relative.has_parent_path() && !std::filesystem::is_directory(setupFolder / L"sources" / relative.parent_path(), ec)) {
                continue;
            }
            put(L"sources\\" + relative.wstring(), it->path());
        }
    }
    if (!saved.empty() && std::filesystem::is_directory(saved, ec)) {
        // Setup's whole sources\ (setup.exe, setuphost.exe and the Setup Platform they load).
        const auto sources = saved / L"sources";
        if (std::filesystem::is_directory(sources, ec)) {
            for (auto it = std::filesystem::recursive_directory_iterator(sources, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file()) {
                    put(L"sources\\" + std::filesystem::relative(it->path(), sources, ec).wstring(), it->path());
                }
            }
        }
        // The boot manager wherever the media has it (efi\boot\bootx64.efi, bootmgr.efi at the root…).
        for (auto it = std::filesystem::recursive_directory_iterator(setupFolder, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            const std::wstring name = text::lower(it->path().filename().wstring());
            const std::wstring relative = std::filesystem::relative(it->path(), setupFolder, ec).wstring();
            if ((name == L"bootmgfw.efi" || name == L"bootx64.efi" || name == L"bootia32.efi" || name == L"bootaa64.efi") &&
                std::filesystem::exists(saved / L"bootmgfw.efi", ec)) {
                put(relative, saved / L"bootmgfw.efi");
            } else if (name == L"bootmgr.efi" && std::filesystem::exists(saved / L"bootmgr.efi", ec)) {
                put(relative, saved / L"bootmgr.efi");
            }
        }
        if (std::filesystem::exists(saved / L"boot.stl", ec)) {
            put(L"efi\\microsoft\\boot\\boot.stl", saved / L"boot.stl");
        }
    }
    std::vector<MediaFile> out;
    for (auto& [key, file] : byPath) {
        out.push_back(std::move(file));
    }
    return out;
}

} // namespace wl::core
