#include "core/postsetup/PostSetup.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"
#include "core/image/RegistryEdit.h"
#include "core/programs/Winget.h"
#include "core/system/Files.h"

#include <json.hpp>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>
#include <functional>

namespace wl::core {

namespace {

using Json = nlohmann::json;
using Step = PostSetupStep;

constexpr const wchar_t* kTaskName = L"WinLove Post-Setup";
constexpr const wchar_t* kProgramsTaskName = L"WinLove Programs";

// The program window's texts when the plan does not give them (the CLI; English).
constexpr std::pair<const char*, const char*> kProgramTextDefaults[] = {
    {"heading", "Installing your programs"},
    {"finished", "Your programs are ready"},
    {"columnProgram", "Program"},
    {"columnState", "State"},
    {"pending", "Waiting"},
    {"installing", "Installing {name} ({n}/{total})"},
    {"installingRow", "Installing..."},
    {"installed", "Installed"},
    {"already", "Already installed"},
    {"restart", "Installed - finishes after a restart"},
    {"failed", "Not installed ({code})"},
    {"notInstalled", "Not installed"},
    {"waitingPostSetup", "Waiting for the setup steps to finish..."},
    {"waitingWinget", "Getting winget ready..."},
    {"noWinget", "winget is not on this Windows: App Installer was removed"},
    {"waitingNetwork", "Waiting for an internet connection..."},
    {"waitingNetworkHint", "No internet connection. Installing goes on by itself as soon as there is one."},
    {"hint", "Closing the window stops here; the rest is installed at the next sign-in."},
    {"updatingSources", "Updating the program list..."},
    {"done", "{ok} program(s) installed"},
    {"doneWithErrors", "{ok} installed, {failed} not installed"},
    {"close", "Close"},
};
constexpr const char* kSetupCompleteLine =
    "if exist \"%SystemRoot%\\Setup\\Scripts\\WinLove\\postsetup-machine.cmd\" "
    "call \"%SystemRoot%\\Setup\\Scripts\\WinLove\\postsetup-machine.cmd\"";

const char* typeKey(Step::Type type) {
    switch (type) {
    case Step::Type::Winget: return "winget";
    case Step::Type::Copy: return "copy";
    case Step::Type::Wifi: return "wifi";
    default: return "command";
    }
}

// Text a batch file echoes: the characters cmd would act on are taken out.
std::wstring echoSafe(std::wstring_view text) {
    std::wstring out;
    for (const wchar_t c : text) {
        out.push_back(std::wstring_view(L"&|<>^%\"()!\r\n").find(c) == std::wstring_view::npos ? c : L' ');
    }
    return out;
}

std::wstring label(const Step& step) {
    std::wstring name = echoSafe(step.name.empty() ? step.source : step.name);
    if (name.size() > 80) {
        name.resize(80);
    }
    return name;
}

// A command is one line of the batch file.
std::wstring oneLine(std::wstring text) {
    std::ranges::replace(text, L'\r', L' ');
    std::ranges::replace(text, L'\n', L' ');
    return text;
}

std::wstring trimmedDestination(const Step& step) {
    std::wstring destination = step.destination;
    // "C:\dir\" would end the quoted argument with an escaped quote.
    while (destination.size() > 3 && (destination.back() == L'\\' || destination.back() == L'/')) {
        destination.pop_back();
    }
    return destination;
}

class Script {
public:
    void line(std::wstring_view text) {
        m_text += text;
        m_text += L"\r\n";
    }
    [[nodiscard]] const std::wstring& text() const noexcept { return m_text; }

private:
    std::wstring m_text;
};

void header(Script& s, std::wstring_view part) {
    s.line(L"@echo off");
    s.line(L"chcp 65001 >nul"); // the file is UTF-8: names and paths outside ASCII
    s.line(L"setlocal EnableExtensions");
    s.line(L"set \"WL=%SystemRoot%\\Setup\\Scripts\\WinLove\"");
    s.line(L"if not exist \"%ProgramData%\\WinLove\" mkdir \"%ProgramData%\\WinLove\"");
    // One log per part: the machine part's file belongs to SYSTEM, a user could not append to it.
    s.line(std::format(L"set \"LOG=%ProgramData%\\WinLove\\postsetup-{}.log\"", part));
    s.line(L">>\"%LOG%\" echo [%date% %time%] WinLove post-setup started");
}

// `failLevel`: the lowest exit code that counts as a failure (robocopy: 8).
void afterStep(Script& s, const std::wstring& tag, int failLevel, bool continueOnError) {
    s.line(std::format(L"if errorlevel {} (", failLevel));
    s.line(std::format(L"  >>\"%LOG%\" echo {} failed with exit code %errorlevel%", tag));
    if (!continueOnError) {
        s.line(L"  goto :wl_end");
    }
    s.line(L")");
}

void emitStep(Script& s, const Step& step, std::size_t number, std::size_t total, bool continueOnError) {
    const std::wstring tag = std::format(L"[{}/{}]", number, total);
    s.line(std::format(L"echo {} {}", tag, label(step)));
    s.line(std::format(L">>\"%LOG%\" echo {} {}", tag, label(step)));
    switch (step.type) {
    case Step::Type::Winget:
        s.line(std::format(L"winget install --id {} -e --silent --accept-package-agreements --accept-source-agreements "
                           L"--disable-interactivity >>\"%LOG%\" 2>&1",
                           step.source));
        afterStep(s, tag, 1, continueOnError);
        break;
    case Step::Type::Copy:
        // robocopy: the target is always a folder; exit codes below 8 are success.
        s.line(std::format(L"robocopy \"%WL%\\files\\{}\" \"{}\" /e /r:1 /w:1 /np /nfl /ndl /njh /njs >>\"%LOG%\" 2>&1", number,
                           trimmedDestination(step)));
        afterStep(s, tag, 8, continueOnError);
        break;
    case Step::Type::Wifi:
        // The profile holds the key in clear text: gone from the disk as soon as WLAN has it.
        s.line(std::format(L"netsh wlan add profile filename=\"%WL%\\files\\{}\\wifi.xml\" user=all >>\"%LOG%\" 2>&1", number));
        afterStep(s, tag, 1, continueOnError);
        s.line(std::format(L"del /f /q \"%WL%\\files\\{}\\wifi.xml\" >nul 2>&1", number));
        break;
    case Step::Type::Command:
        if (step.wait) {
            // /s: only the outer quotes are stripped, the command keeps its own.
            s.line(std::format(L"cmd /d /s /c \"{}\" >>\"%LOG%\" 2>&1", oneLine(step.source)));
            afterStep(s, tag, 1, continueOnError);
        } else {
            s.line(std::format(L"start \"\" cmd /d /s /c \"{}\"", oneLine(step.source)));
        }
        break;
    }
}

// winget is registered per user a little after the first logon: wait for it (5 minutes at most).
void waitForWinget(Script& s) {
    s.line(L"set WLTRY=0");
    s.line(L":wl_winget");
    s.line(L"where winget >nul 2>&1 && goto :wl_winget_ready");
    s.line(L"set /a WLTRY+=1");
    s.line(L"if %WLTRY% geq 60 goto :wl_winget_ready");
    s.line(L"ping -n 6 127.0.0.1 >nul");
    s.line(L"goto :wl_winget");
    s.line(L":wl_winget_ready");
}

bool runsAtLogon(const PostSetupPlan& plan, const Step& step) {
    if (step.type == Step::Type::Wifi) {
        return false; // WLAN profiles for all users: SYSTEM, before the first logon
    }
    return plan.when == PostSetupPlan::When::FirstLogon || step.type == Step::Type::Winget;
}

Result<void> writeBytes(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return out ? Result<void>{} : fail(ErrorCode::IoError, L"could not write post-setup file", file.wstring());
}

// Copies a file, or a folder's contents, into `target` (created). Reports bytes through `copied`.
Result<void> stage(const std::filesystem::path& source, const std::filesystem::path& target, const TaskContext& task,
                   const std::function<void(std::uint64_t)>& copied) {
    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    // Each copy has its own error code: the walk's own must only say whether the walk failed.
    auto copyFile = [&](const std::filesystem::path& from, const std::filesystem::path& to) -> Result<void> {
        if (auto cancelled = task.cancel.check(L"post-setup files"); !cancelled) {
            return cancelled;
        }
        std::error_code copyEc;
        std::filesystem::create_directories(to.parent_path(), copyEc);
        if (!std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, copyEc) || copyEc) {
            return fail(ErrorCode::IoError, L"could not copy into the image", from.wstring(),
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(static_cast<unsigned long>(copyEc.value()))));
        }
        copied(treeBytes(to));
        return {};
    };
    if (std::filesystem::is_regular_file(source, ec)) {
        return copyFile(source, target / source.filename());
    }
    if (!std::filesystem::is_directory(source, ec)) {
        return fail(ErrorCode::NotFound, L"post-setup source not found", source.wstring());
    }
    for (auto it = std::filesystem::recursive_directory_iterator(source, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code entry;
        if (!it->is_regular_file(entry)) {
            continue;
        }
        if (auto r = copyFile(it->path(), target / std::filesystem::relative(it->path(), source, entry)); !r) {
            return r;
        }
    }
    // A walk that stopped half way must not report a partial payload as staged.
    if (ec) {
        return fail(ErrorCode::IoError, L"could not read the post-setup folder", source.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(static_cast<unsigned long>(ec.value()))));
    }
    return {};
}

// The task XML both logon tasks use (UTF-16 header: what schtasks /xml expects).
std::wstring logonTaskXml(std::wstring_view description, std::wstring_view command, std::wstring_view arguments,
                          std::wstring_view delay) {
    // S-1-5-32-545 = BUILTIN\Users: whoever logs on; HighestAvailable = elevated for administrators.
    std::wstring xml = L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
                       L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
                       L"  <RegistrationInfo>\r\n";
    xml += std::format(L"    <Description>{}</Description>\r\n", description);
    xml += L"  </RegistrationInfo>\r\n"
           L"  <Triggers>\r\n"
           L"    <LogonTrigger>\r\n"
           L"      <Enabled>true</Enabled>\r\n";
    if (!delay.empty()) {
        xml += std::format(L"      <Delay>{}</Delay>\r\n", delay);
    }
    xml += L"    </LogonTrigger>\r\n"
           L"  </Triggers>\r\n"
           L"  <Principals>\r\n"
           L"    <Principal id=\"Author\">\r\n"
           L"      <GroupId>S-1-5-32-545</GroupId>\r\n"
           L"      <RunLevel>HighestAvailable</RunLevel>\r\n"
           L"    </Principal>\r\n"
           L"  </Principals>\r\n"
           L"  <Settings>\r\n"
           L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
           L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
           L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
           L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
           L"    <Enabled>true</Enabled>\r\n"
           L"  </Settings>\r\n"
           L"  <Actions Context=\"Author\">\r\n"
           L"    <Exec>\r\n";
    xml += std::format(L"      <Command>{}</Command>\r\n      <Arguments>{}</Arguments>\r\n", command, arguments);
    xml += L"    </Exec>\r\n"
           L"  </Actions>\r\n"
           L"</Task>\r\n";
    return xml;
}

std::string utf16File(const std::wstring& text) {
    std::string bytes = "\xFF\xFE"; // UTF-16LE with its byte order mark
    bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    return bytes;
}

} // namespace

std::string postSetupToJson(const PostSetupPlan& plan) {
    Json steps = Json::array();
    for (const auto& step : plan.steps) {
        Json entry{{"type", typeKey(step.type)}, {"name", utf8::fromWide(step.name)}, {"source", utf8::fromWide(step.source)}};
        if (step.type == Step::Type::Copy) {
            entry["destination"] = utf8::fromWide(step.destination);
        }
        if (step.type == Step::Type::Command && !step.wait) {
            entry["wait"] = false;
        }
        steps.push_back(std::move(entry));
    }
    Json doc{{"when", plan.when == PostSetupPlan::When::SetupComplete ? "setupComplete" : "firstLogon"},
             {"continueOnError", plan.continueOnError},
             {"steps", std::move(steps)}};
    if (!plan.programs.empty()) {
        Json programs = Json::array();
        for (const auto& program : plan.programs) {
            programs.push_back({{"id", utf8::fromWide(program.id)}, {"name", utf8::fromWide(program.name)}});
        }
        doc["programs"] = std::move(programs);
        Json texts = Json::object();
        for (const auto& [key, value] : plan.programTexts) {
            texts[utf8::fromWide(key)] = utf8::fromWide(value);
        }
        doc["programTexts"] = std::move(texts);
    }
    return doc.dump();
}

Result<PostSetupPlan> postSetupFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a post-setup plan");
    }
    PostSetupPlan plan;
    try {
        plan.when = doc.value("when", std::string{"firstLogon"}) == "setupComplete" ? PostSetupPlan::When::SetupComplete
                                                                                      : PostSetupPlan::When::FirstLogon;
        plan.continueOnError = doc.value("continueOnError", true);
        for (const auto& entry : doc.value("steps", Json::array())) {
            Step step;
            const std::string type = entry.value("type", std::string{"command"});
            step.type = type == "winget" ? Step::Type::Winget
                        : type == "copy"   ? Step::Type::Copy
                        : type == "wifi"   ? Step::Type::Wifi
                                           : Step::Type::Command;
            step.name = utf8::toWide(entry.value("name", std::string{}));
            step.source = utf8::toWide(entry.value("source", std::string{}));
            step.destination = utf8::toWide(entry.value("destination", std::string{}));
            step.wait = entry.value("wait", true);
            plan.steps.push_back(std::move(step));
        }
        for (const auto& entry : doc.value("programs", Json::array())) {
            PostSetupProgram program{utf8::toWide(entry.value("id", std::string{})), utf8::toWide(entry.value("name", std::string{}))};
            if (!program.id.empty()) {
                plan.programs.push_back(std::move(program));
            }
        }
        if (const auto texts = doc.find("programTexts"); texts != doc.end() && texts->is_object()) {
            for (const auto& [key, value] : texts->items()) {
                if (value.is_string()) {
                    plan.programTexts.emplace_back(utf8::toWide(key), utf8::toWide(value.get<std::string>()));
                }
            }
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed post-setup plan", utf8::toWide(e.what()));
    }
    return plan;
}

std::vector<std::pair<std::size_t, PostSetupProblem>> validatePostSetup(const PostSetupPlan& plan) {
    std::vector<std::pair<std::size_t, PostSetupProblem>> problems;
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const Step& step = plan.steps[i];
        if (step.source.empty()) {
            problems.emplace_back(i, PostSetupProblem::EmptySource);
            continue;
        }
        if (step.type == Step::Type::Winget) {
            // The id goes into a command line as it is.
            const bool valid = std::ranges::all_of(step.source, [](wchar_t c) {
                return c < 128 && (std::iswalnum(c) != 0 || c == L'.' || c == L'-' || c == L'_' || c == L'+');
            });
            if (!valid) {
                problems.emplace_back(i, PostSetupProblem::BadWingetId);
            }
        } else if (step.type == Step::Type::Wifi) {
            if (step.source.find(L"<WLANProfile") == std::wstring::npos) {
                problems.emplace_back(i, PostSetupProblem::BadWifiProfile);
            }
        } else if (step.type == Step::Type::Copy) {
            if (step.destination.empty()) {
                problems.emplace_back(i, PostSetupProblem::EmptyDestination);
            } else if (step.destination.find(L'"') != std::wstring::npos) {
                problems.emplace_back(i, PostSetupProblem::BadDestination);
            }
        }
    }
    return problems;
}

std::vector<std::size_t> invalidPrograms(const PostSetupPlan& plan) {
    std::vector<std::size_t> bad;
    for (std::size_t i = 0; i < plan.programs.size(); ++i) {
        if (!validWingetId(plan.programs[i].id)) {
            bad.push_back(i);
        }
    }
    return bad;
}

PostSetupScripts buildPostSetupScripts(const PostSetupPlan& plan) {
    PostSetupScripts scripts;
    if (plan.empty()) {
        return scripts;
    }
    const std::size_t total = plan.steps.size();
    const bool anyAtLogon = std::ranges::any_of(plan.steps, [&](const Step& s) { return runsAtLogon(plan, s); });

    Script machine;
    header(machine, L"machine");
    for (std::size_t i = 0; i < total; ++i) {
        if (!runsAtLogon(plan, plan.steps[i])) {
            emitStep(machine, plan.steps[i], i + 1, total, plan.continueOnError);
        }
    }
    if (anyAtLogon) {
        // Before the end label: when a machine step stops the run, the rest does not start either.
        machine.line(std::format(L"schtasks /create /tn \"{}\" /xml \"%WL%\\postsetup-task.xml\" /f >>\"%LOG%\" 2>&1", kTaskName));
    }
    if (!plan.programs.empty()) {
        machine.line(std::format(L"schtasks /create /tn \"{}\" /xml \"%WL%\\programs-task.xml\" /f >>\"%LOG%\" 2>&1",
                                 kProgramsTaskName));
    }
    machine.line(L":wl_end");
    machine.line(L">>\"%LOG%\" echo [%date% %time%] finished");
    machine.line(L"endlocal");
    scripts.machine = machine.text();

    if (anyAtLogon) {
        Script user;
        header(user, L"user");
        bool waited = false;
        for (std::size_t i = 0; i < total; ++i) {
            const Step& step = plan.steps[i];
            if (!runsAtLogon(plan, step)) {
                continue;
            }
            if (step.type == Step::Type::Winget && !waited) {
                waitForWinget(user);
                waited = true;
            }
            emitStep(user, step, i + 1, total, plan.continueOnError);
        }
        user.line(L":wl_end");
        // Once: the task removes itself whatever happened (needs the elevation the task gives an admin).
        user.line(std::format(L"schtasks /delete /tn \"{}\" /f >nul 2>&1", kTaskName));
        user.line(L">>\"%LOG%\" echo [%date% %time%] finished");
        user.line(L"endlocal");
        scripts.user = user.text();
    }
    return scripts;
}

std::wstring postSetupTaskXml() {
    return logonTaskXml(L"WinLove: post-setup steps, once, at the first logon.", L"%SystemRoot%\\System32\\cmd.exe",
                        L"/d /c \"%SystemRoot%\\Setup\\Scripts\\WinLove\\postsetup-user.cmd\"", {});
}

std::wstring programsTaskXml() {
    // A short delay: the desktop is up before the window opens.
    return logonTaskXml(L"WinLove: the chosen programs, installed with winget at the first logon (until all are in place).",
                        L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
                        L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "
                        L"\"%SystemRoot%\\Setup\\Scripts\\WinLove\\programs.ps1\"",
                        L"PT15S");
}

std::string programsJson(const PostSetupPlan& plan) {
    Json texts = Json::object();
    for (const auto& [key, value] : kProgramTextDefaults) {
        texts[key] = value;
    }
    for (const auto& [key, value] : plan.programTexts) {
        texts[utf8::fromWide(key)] = utf8::fromWide(value);
    }
    Json programs = Json::array();
    for (const auto& program : plan.programs) {
        programs.push_back({{"id", utf8::fromWide(program.id)},
                            {"name", utf8::fromWide(program.name.empty() ? program.id : program.name)}});
    }
    return Json{{"title", "WinLove"}, {"texts", std::move(texts)}, {"programs", std::move(programs)}}.dump(2);
}

double estimatePostSetupSeconds(const PostSetupPlan& plan) {
    double seconds = 0;
    for (const auto& step : plan.steps) {
        switch (step.type) {
        case Step::Type::Winget: seconds += 45; break;
        case Step::Type::Copy: seconds += 5; break;
        case Step::Type::Command: seconds += step.wait ? 10 : 1; break;
        case Step::Type::Wifi: seconds += 2; break;
        }
    }
    return seconds + 60.0 * static_cast<double>(plan.programs.size()); // download + silent install each
}

std::uint64_t postSetupPayloadBytes(const PostSetupPlan& plan) {
    std::uint64_t total = 0;
    for (const auto& step : plan.steps) {
        if (step.type == Step::Type::Copy && !step.source.empty()) {
            total += treeBytes(step.source);
        }
    }
    return total;
}

Result<void> applyPostSetup(const std::filesystem::path& mountDir, const PostSetupPlan& plan, const TaskContext& task) {
    if (const auto problems = validatePostSetup(plan); !problems.empty()) {
        return fail(ErrorCode::InvalidArgument, L"post-setup step cannot be written",
                    std::format(L"step {}", problems.front().first + 1));
    }
    if (const auto bad = invalidPrograms(plan); !bad.empty()) {
        return fail(ErrorCode::InvalidArgument, L"not a winget package id", plan.programs[bad.front()].id);
    }
    const auto scripts = mountDir / L"Windows" / L"Setup" / L"Scripts";
    const auto folder = scripts / L"WinLove";
    std::error_code ec;
    // What an earlier run left: the plan is replaced as a whole.
    for (const wchar_t* name : {L"postsetup-machine.cmd", L"postsetup-user.cmd", L"postsetup-task.xml", L"programs.ps1",
                                L"programs.json", L"programs-task.xml"}) {
        std::filesystem::remove(folder / name, ec);
    }
    std::filesystem::remove_all(folder / L"files", ec);
    if (plan.empty()) {
        return {}; // the SetupComplete line checks for the script before calling it
    }
    std::filesystem::create_directories(folder, ec);

    const std::uint64_t totalBytes = std::max<std::uint64_t>(postSetupPayloadBytes(plan), 1);
    std::uint64_t done = 0;
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const Step& step = plan.steps[i];
        if (step.type == Step::Type::Wifi) {
            const auto target = folder / L"files" / std::to_wstring(i + 1);
            std::filesystem::create_directories(target, ec);
            if (auto r = writeBytes(target / L"wifi.xml", utf8::fromWide(step.source)); !r) {
                return r;
            }
            continue;
        }
        if (step.type != Step::Type::Copy) {
            continue;
        }
        auto staged = stage(step.source, folder / L"files" / std::to_wstring(i + 1), task, [&](std::uint64_t bytes) {
            done += bytes;
            task.report(static_cast<double>(done) / static_cast<double>(totalBytes), L"post-setup files");
        });
        if (!staged) {
            return staged;
        }
    }

    const PostSetupScripts text = buildPostSetupScripts(plan);
    if (auto r = writeBytes(folder / L"postsetup-machine.cmd", utf8::fromWide(text.machine)); !r) {
        return r;
    }
    if (!text.user.empty()) {
        if (auto r = writeBytes(folder / L"postsetup-user.cmd", utf8::fromWide(text.user)); !r) {
            return r;
        }
        if (auto r = writeBytes(folder / L"postsetup-task.xml", utf16File(postSetupTaskXml())); !r) {
            return r;
        }
    }
    if (!plan.programs.empty()) {
        // D-078: the window that installs them, what it reads, and its own logon task.
        if (auto r = writeBytes(folder / L"programs.ps1", scripts::kPrograms); !r) {
            return r;
        }
        if (auto r = writeBytes(folder / L"programs.json", programsJson(plan)); !r) {
            return r;
        }
        if (auto r = writeBytes(folder / L"programs-task.xml", utf16File(programsTaskXml())); !r) {
            return r;
        }
    }
    log::info("postsetup", std::format(L"{} step(s), {} program(s) written to {}", plan.steps.size(), plan.programs.size(),
                                       folder.wstring()));
    return ensureSetupCompleteLine(scripts / L"SetupComplete.cmd", kSetupCompleteLine, "WinLove: post-setup steps");
}

} // namespace wl::core
