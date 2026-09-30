#include "core/postsetup/SetupScripts.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/RegistryEdit.h"

#include <format>
#include <fstream>
#include <sstream>

namespace wl::core {

namespace {

bool isPowerShell(std::wstring_view name) {
    return name.size() > 4 && _wcsicmp(std::wstring(name.substr(name.size() - 4)).c_str(), L".ps1") == 0;
}

} // namespace

std::filesystem::path setupScriptPath(const std::filesystem::path& mountDir, std::wstring_view name) {
    return mountDir / L"Windows" / L"Setup" / L"Scripts" / L"WinLove" / std::wstring(name);
}

std::string setupScriptCallLine(std::wstring_view name) {
    const std::string file = "%SystemRoot%\\Setup\\Scripts\\WinLove\\" + utf8::fromWide(name);
    if (isPowerShell(name)) {
        return std::format("if exist \"{0}\" powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"{0}\"",
                           file);
    }
    return std::format("if exist \"{0}\" call \"{0}\"", file);
}

Result<void> writeSetupScript(const std::filesystem::path& mountDir, std::wstring_view name, std::wstring_view body,
                              std::string_view comment) {
    const auto file = setupScriptPath(mountDir, name);
    std::error_code ec;
    if (!std::filesystem::is_directory(mountDir / L"Windows", ec)) {
        return fail(ErrorCode::NotFound, L"not a Windows image", mountDir.wstring());
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::string text;
    if (isPowerShell(name)) {
        text = "\xEF\xBB\xBF# " + std::string(comment) + "\r\n" + utf8::fromWide(body);
    } else {
        text = "@echo off\r\nchcp 65001 >nul\r\nrem " + std::string(comment) + "\r\n" + utf8::fromWide(body);
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return fail(ErrorCode::IoError, L"could not write the script", file.wstring());
        }
    }
    log::info("postsetup", std::format(L"{} written ({} bytes)", file.wstring(), text.size()));
    return ensureSetupCompleteLine(mountDir / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd",
                                   setupScriptCallLine(name), comment);
}

Result<void> removeSetupScript(const std::filesystem::path& mountDir, std::wstring_view name) {
    std::error_code ec;
    std::filesystem::remove(setupScriptPath(mountDir, name), ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"could not delete the script", setupScriptPath(mountDir, name).wstring(),
                    ec.value());
    }
    return {};
}

std::wstring readSetupScript(const std::filesystem::path& mountDir, std::wstring_view name) {
    std::ifstream in(setupScriptPath(mountDir, name), std::ios::binary);
    if (!in) {
        return {};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.erase(0, 3);
    }
    return utf8::toWide(text);
}

} // namespace wl::core
