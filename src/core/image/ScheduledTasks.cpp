#include "core/image/ScheduledTasks.h"

#include "base/Text.h"
#include "core/postsetup/SetupScripts.h"

#include <algorithm>
#include <format>

namespace wl::core {

namespace {

constexpr std::wstring_view kPrefix = L"schtasks /Change /TN \"";
constexpr std::wstring_view kSuffix = L"\" /Disable";

} // namespace

bool validTaskPath(std::wstring_view path) {
    if (path.size() < 2 || path.size() > 260 || path.front() != L'\\' || path.back() == L'\\') {
        return false;
    }
    for (const wchar_t c : path) {
        if (c < 0x20 || c == L'"' || c == L'%' || c == L'^' || c == L'&' || c == L'|' || c == L'<' || c == L'>' ||
            c == L'/' || c == L':' || c == L'*' || c == L'?') {
            return false;
        }
    }
    std::size_t start = 1;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(L'\\', start), path.size());
        const std::wstring_view part = path.substr(start, end - start);
        if (part.empty() || part == L"." || part == L".." || part.front() == L' ' || part.back() == L' ') {
            return false;
        }
        start = end + 1;
    }
    return true;
}

std::vector<std::wstring> disabledTasksIn(std::wstring_view script) {
    std::vector<std::wstring> tasks;
    std::size_t at = 0;
    while (at < script.size()) {
        std::size_t end = script.find(L'\n', at);
        if (end == std::wstring_view::npos) {
            end = script.size();
        }
        std::wstring_view line = script.substr(at, end - at);
        at = end + 1;
        if (!line.starts_with(kPrefix)) {
            continue;
        }
        line.remove_prefix(kPrefix.size());
        const auto close = line.find(kSuffix);
        if (close != std::wstring_view::npos && validTaskPath(line.substr(0, close))) {
            tasks.emplace_back(line.substr(0, close));
        }
    }
    return tasks;
}

std::wstring tasksScriptBody(const std::vector<std::wstring>& paths) {
    std::wstring body = L"if not exist \"%ProgramData%\\WinLove\" mkdir \"%ProgramData%\\WinLove\"\r\n";
    for (const auto& path : paths) {
        body += std::wstring(kPrefix) + path + std::wstring(kSuffix) + L" >>\"%ProgramData%\\WinLove\\tasks.log\" 2>&1\r\n";
    }
    return body;
}

std::vector<std::wstring> readDisabledTasks(const std::filesystem::path& mountDir) {
    return disabledTasksIn(readSetupScript(mountDir, kTasksScript));
}

Result<void> setTaskDisabled(const std::filesystem::path& mountDir, std::wstring_view path, bool disabled) {
    if (!validTaskPath(path)) {
        return fail(ErrorCode::InvalidArgument, L"not a scheduled task path", std::wstring(path));
    }
    auto tasks = readDisabledTasks(mountDir);
    const auto it = std::ranges::find_if(tasks, [&](const std::wstring& t) { return text::iequals(t, path); });
    if (disabled == (it != tasks.end())) {
        return {}; // already so
    }
    if (disabled) {
        tasks.emplace_back(path);
    } else {
        tasks.erase(it);
    }
    if (tasks.empty()) {
        return removeSetupScript(mountDir, kTasksScript);
    }
    return writeSetupScript(mountDir, kTasksScript, tasksScriptBody(tasks),
                            "WinLove: scheduled tasks switched off after setup");
}

} // namespace wl::core
