#include "core/image/ScheduledTasks.h"

#include "base/Text.h"
#include "base/Utf8.h"
#include "core/postsetup/SetupScripts.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>

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

// ---- D-102: custom recurring tasks ------------------------------------------------------------

namespace {

using Json = nlohmann::json;

// schtasks /SC token for a trigger (Daily/Weekly take a time, Weekly a day too).
constexpr std::wstring_view scheduleToken(CreatedTask::Trigger t) {
    switch (t) {
    case CreatedTask::Trigger::AtLogon: return L"ONLOGON";
    case CreatedTask::Trigger::AtStartup: return L"ONSTART";
    case CreatedTask::Trigger::Daily: return L"DAILY";
    case CreatedTask::Trigger::Weekly: return L"WEEKLY";
    case CreatedTask::Trigger::Hourly: return L"HOURLY";
    }
    return L"DAILY";
}

constexpr std::wstring_view weekdayToken(int weekday) {
    constexpr std::array<std::wstring_view, 7> days{L"MON", L"TUE", L"WED", L"THU", L"FRI", L"SAT", L"SUN"};
    return days[static_cast<std::size_t>(std::clamp(weekday, 0, 6))];
}

// "%" is the one metacharacter that batch expands even inside quotes: store it literally (%%).
std::wstring batchLiteral(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        out.push_back(c);
        if (c == L'%') {
            out.push_back(L'%');
        }
    }
    return out;
}

bool validTime(std::wstring_view time) {
    if (time.size() != 5 || time[2] != L':') {
        return false;
    }
    for (const std::size_t i : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{4}}) {
        if (time[i] < L'0' || time[i] > L'9') {
            return false;
        }
    }
    const int hh = (time[0] - L'0') * 10 + (time[1] - L'0');
    const int mm = (time[3] - L'0') * 10 + (time[4] - L'0');
    return hh < 24 && mm < 60;
}

bool hasForbidden(std::wstring_view text, bool allowSpace) {
    for (const wchar_t c : text) {
        if (c < 0x20 || c == L'"') {
            return true;
        }
        if (!allowSpace && c == L' ') {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<CreatedTaskProblem> validCreatedTask(const CreatedTask& task) {
    if (task.name.empty()) {
        return CreatedTaskProblem::EmptyName;
    }
    if (task.name.size() > 200 || hasForbidden(task.name, /*allowSpace=*/true)) {
        return CreatedTaskProblem::BadName;
    }
    for (const wchar_t c : task.name) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'<' || c == L'>' || c == L'|' ||
            c == L'%' || c == L'^' || c == L'&') {
            return CreatedTaskProblem::BadName;
        }
    }
    if (task.command.empty()) {
        return CreatedTaskProblem::EmptyCommand;
    }
    if (task.command.size() > 500 || hasForbidden(task.command, /*allowSpace=*/true)) {
        return CreatedTaskProblem::BadCommand;
    }
    const bool timed = task.trigger == CreatedTask::Trigger::Daily || task.trigger == CreatedTask::Trigger::Weekly;
    if (timed && !task.time.empty() && !validTime(task.time)) {
        return CreatedTaskProblem::BadTime;
    }
    return std::nullopt;
}

std::wstring createTaskCommand(const CreatedTask& task) {
    std::wstring line = L"schtasks /Create /TN \"\\WinLove\\" + batchLiteral(task.name) + L"\" /TR \"" +
                        batchLiteral(task.command) + L"\" /SC " + std::wstring(scheduleToken(task.trigger));
    const bool timed = task.trigger == CreatedTask::Trigger::Daily || task.trigger == CreatedTask::Trigger::Weekly;
    if (timed && !task.time.empty()) {
        line += L" /ST " + task.time;
    }
    if (task.trigger == CreatedTask::Trigger::Weekly) {
        line += L" /D " + std::wstring(weekdayToken(task.weekday));
    }
    line += L" /RU SYSTEM /RL HIGHEST /F";
    return line;
}

std::wstring createdTasksScriptBody(const std::vector<CreatedTask>& tasks) {
    std::wstring body = L"if not exist \"%ProgramData%\\WinLove\" mkdir \"%ProgramData%\\WinLove\"\r\n";
    for (const auto& task : tasks) {
        if (validCreatedTask(task)) {
            continue; // a bad task is skipped, not written
        }
        body += createTaskCommand(task) + L" >>\"%ProgramData%\\WinLove\\tasks.log\" 2>&1\r\n";
    }
    return body;
}

std::string createdTasksToJson(const std::vector<CreatedTask>& tasks) {
    Json array = Json::array();
    for (const auto& task : tasks) {
        array.push_back(Json{{"name", utf8::fromWide(task.name)},
                             {"trigger", static_cast<int>(task.trigger)},
                             {"time", utf8::fromWide(task.time)},
                             {"weekday", task.weekday},
                             {"command", utf8::fromWide(task.command)}});
    }
    return array.dump();
}

Result<std::vector<CreatedTask>> createdTasksFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_array()) {
        return fail(ErrorCode::ParseError, L"not a created-tasks list");
    }
    std::vector<CreatedTask> tasks;
    try {
        for (const auto& item : doc) {
            CreatedTask task;
            task.name = utf8::toWide(item.value("name", std::string{}));
            const int t = item.value("trigger", 0);
            task.trigger = static_cast<CreatedTask::Trigger>(std::clamp(t, 0, 4));
            task.time = utf8::toWide(item.value("time", std::string{}));
            task.weekday = std::clamp(item.value("weekday", 0), 0, 6);
            task.command = utf8::toWide(item.value("command", std::string{}));
            tasks.push_back(std::move(task));
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed created-tasks list", utf8::toWide(e.what()));
    }
    return tasks;
}

std::vector<CreatedTask> readCreatedTasks(const std::filesystem::path& mountDir) {
    const auto data = setupScriptPath(mountDir, kCreateTasksData);
    std::ifstream in(data, std::ios::binary);
    if (!in) {
        return {};
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = createdTasksFromJson(bytes);
    return parsed ? std::move(*parsed) : std::vector<CreatedTask>{};
}

Result<void> setCreatedTasks(const std::filesystem::path& mountDir, const std::vector<CreatedTask>& tasks) {
    const auto data = setupScriptPath(mountDir, kCreateTasksData);
    std::error_code ec;
    const bool anyValid = std::ranges::any_of(tasks, [](const CreatedTask& t) { return !validCreatedTask(t); });
    if (!anyValid) {
        std::filesystem::remove(data, ec);
        return removeSetupScript(mountDir, kCreateTasksScript);
    }
    if (auto wrote = writeSetupScript(mountDir, kCreateTasksScript, createdTasksScriptBody(tasks),
                                      "WinLove: custom scheduled tasks created after setup");
        !wrote) {
        return wrote;
    }
    // The data file sits next to the script so the page can read these tasks back next time.
    const std::string json = createdTasksToJson(tasks);
    std::ofstream out(data, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail(ErrorCode::IoError, L"could not write taskcreate.json", data.wstring());
    }
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!out) {
        return fail(ErrorCode::IoError, L"could not write taskcreate.json", data.wstring());
    }
    return {};
}

} // namespace wl::core
