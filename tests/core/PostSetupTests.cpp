// P14: the post-setup plan — JSON, validation, the generated batch files, writing into an image.
#include "core/postsetup/PostSetup.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::core;
using Step = PostSetupStep;

namespace {

bool has(const std::wstring& text, std::wstring_view fragment) {
    return text.find(fragment) != std::wstring::npos;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& file, const std::string& content) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
}

PostSetupPlan sample() {
    PostSetupPlan plan;
    plan.steps = {
        {Step::Type::Winget, L"7-Zip", L"7zip.7zip", {}, true},
        {Step::Type::Command, L"G\u00fc\u00e7 plan\u0131 & co", L"powercfg /setactive \"8c5e7fda\"", {}, true},
        {Step::Type::Copy, L"Duvar ka\u011f\u0131tlar\u0131", L"D:\\assets\\wallpapers", L"C:\\Users\\Public\\Pictures\\", true},
        {Step::Type::Command, L"Yeniden ba\u015flat", L"shutdown /r /t 30", {}, false},
    };
    return plan;
}

} // namespace

TEST_CASE("post-setup: the plan survives the JSON round trip") {
    PostSetupPlan plan = sample();
    plan.when = PostSetupPlan::When::SetupComplete;
    plan.continueOnError = false;
    const auto back = postSetupFromJson(postSetupToJson(plan));
    REQUIRE(back);
    CHECK(*back == plan);
    CHECK(*postSetupFromJson("{}") == PostSetupPlan{}); // defaults: first logon, continue on error
    CHECK_FALSE(postSetupFromJson("[1,2]"));
    CHECK_FALSE(postSetupFromJson("{\"steps\":[{\"name\":5}]}"));
}

TEST_CASE("post-setup: a power plan is imported and made active (SYSTEM), its GUID checked (D-096)") {
    PostSetupPlan plan;
    plan.when = PostSetupPlan::When::FirstLogon; // a power plan is a machine step even so
    plan.steps = {{Step::Type::PowerPlan, L"Ultimate", L"C:\\plans\\ultimate.pow", L"{11111111-2222-3333-4444-555555555555}", true}};
    CHECK(validatePostSetup(plan).empty());
    const auto scripts = buildPostSetupScripts(plan);
    CHECK(scripts.machine.find(L"powercfg /import \"%WL%\\files\\1\\plan.pow\" {11111111-2222-3333-4444-555555555555}") !=
          std::wstring::npos);
    CHECK(scripts.machine.find(L"powercfg /setactive {11111111-2222-3333-4444-555555555555}") != std::wstring::npos);
    CHECK(scripts.user.find(L"powercfg") == std::wstring::npos); // SYSTEM, not the user script
    // A destination that is not a GUID is refused.
    plan.steps[0].destination = L"not a guid";
    REQUIRE(validatePostSetup(plan).size() == 1);
    CHECK(validatePostSetup(plan)[0].second == PostSetupProblem::BadDestination);
    // Survives the JSON round trip.
    plan.steps[0].destination = L"{11111111-2222-3333-4444-555555555555}";
    const auto back = postSetupFromJson(postSetupToJson(plan));
    REQUIRE(back);
    CHECK(back->steps[0].type == Step::Type::PowerPlan);
}

TEST_CASE("post-setup: validation points at the step") {
    CHECK(validatePostSetup(sample()).empty());
    PostSetupPlan plan;
    plan.steps = {
        {Step::Type::Winget, L"", L"7zip.7zip & calc", {}, true},
        {Step::Type::Command, L"", L"", {}, true},
        {Step::Type::Copy, L"", L"C:\\a", L"", true},
        {Step::Type::Copy, L"", L"C:\\a", L"C:\\b\"c", true},
        {Step::Type::Winget, L"", L"Notepad++.Notepad++", {}, true},
    };
    const auto problems = validatePostSetup(plan);
    REQUIRE(problems.size() == 4);
    CHECK(problems[0] == std::pair<std::size_t, PostSetupProblem>{0, PostSetupProblem::BadWingetId});
    CHECK(problems[1] == std::pair<std::size_t, PostSetupProblem>{1, PostSetupProblem::EmptySource});
    CHECK(problems[2] == std::pair<std::size_t, PostSetupProblem>{2, PostSetupProblem::EmptyDestination});
    CHECK(problems[3] == std::pair<std::size_t, PostSetupProblem>{3, PostSetupProblem::BadDestination});
}

TEST_CASE("post-setup scripts: first logon — every step in the user script, the machine script only registers the task") {
    const auto scripts = buildPostSetupScripts(sample());
    const auto& machine = scripts.machine;
    const auto& user = scripts.user;

    CHECK(machine.starts_with(L"@echo off\r\nchcp 65001 >nul\r\n"));
    CHECK(has(machine, L"schtasks /create /tn \"WinLove Post-Setup\" /xml \"%WL%\\postsetup-task.xml\" /f"));
    CHECK_FALSE(has(machine, L"powercfg"));
    CHECK_FALSE(has(machine, L"winget install"));

    // In order, numbered over the whole plan.
    const auto winget = user.find(L"[1/4] 7-Zip");
    const auto command = user.find(L"[2/4]");
    const auto copy = user.find(L"[3/4]");
    const auto restart = user.find(L"[4/4]");
    REQUIRE(winget != std::wstring::npos);
    CHECK(winget < command);
    CHECK(command < copy);
    CHECK(copy < restart);
    // winget: waited for, then installed without prompts.
    CHECK(user.find(L":wl_winget_ready") < user.find(L"winget install"));
    CHECK(has(user, L"winget install --id 7zip.7zip -e --silent --accept-package-agreements --accept-source-agreements "
                    L"--disable-interactivity >>\"%LOG%\" 2>&1"));
    // The command keeps its own quotes inside cmd /s /c "…"; what is echoed has no cmd metacharacters.
    CHECK(has(user, L"cmd /d /s /c \"powercfg /setactive \"8c5e7fda\"\" >>\"%LOG%\" 2>&1"));
    CHECK(has(user, L"echo [2/4] G\u00fc\u00e7 plan\u0131   co"));
    // Copy: staged under the step's number; the trailing backslash would escape the closing quote.
    CHECK(has(user, L"robocopy \"%WL%\\files\\3\" \"C:\\Users\\Public\\Pictures\" /e"));
    CHECK(has(user, L"if errorlevel 8 ("));
    // No wait: started, not followed by an error check.
    CHECK(has(user, L"start \"\" cmd /d /s /c \"shutdown /r /t 30\"\r\n:wl_end"));
    CHECK(has(user, L"schtasks /delete /tn \"WinLove Post-Setup\" /f"));
    CHECK_FALSE(has(user, L"goto :wl_end")); // continue on error
    CHECK(has(user, L"postsetup-user.log"));
    CHECK(has(machine, L"postsetup-machine.log"));
}

TEST_CASE("post-setup scripts: SetupComplete mode — commands and copies as SYSTEM, winget still at logon; stop on error") {
    PostSetupPlan plan = sample();
    plan.when = PostSetupPlan::When::SetupComplete;
    plan.continueOnError = false;
    auto scripts = buildPostSetupScripts(plan);
    CHECK(has(scripts.machine, L"cmd /d /s /c \"powercfg /setactive \"8c5e7fda\"\""));
    CHECK(has(scripts.machine, L"robocopy \"%WL%\\files\\3\""));
    CHECK(has(scripts.machine, L"goto :wl_end"));
    CHECK_FALSE(has(scripts.machine, L"winget install"));
    // The task is registered before the end label: a stopped run does not start the logon part.
    CHECK(scripts.machine.find(L"schtasks /create") < scripts.machine.find(L"\r\n:wl_end\r\n"));
    CHECK(has(scripts.user, L"winget install --id 7zip.7zip"));
    CHECK_FALSE(has(scripts.user, L"powercfg"));

    // Nothing for the logon: no user script, no task.
    plan.steps.erase(plan.steps.begin());
    scripts = buildPostSetupScripts(plan);
    CHECK(scripts.user.empty());
    CHECK_FALSE(has(scripts.machine, L"schtasks"));
    CHECK(buildPostSetupScripts({}).machine.empty());
}

TEST_CASE("post-setup: written into the image folder — payloads, scripts, task, SetupComplete line; replaced as a whole") {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"postsetup";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto mount = root / L"mount";
    const auto assets = root / L"assets";
    writeFile(assets / L"a.jpg", "AAAA");
    writeFile(assets / L"sub" / L"b.jpg", "BB");
    writeFile(root / L"single.txt", "one file");

    PostSetupPlan plan;
    plan.steps = {
        {Step::Type::Copy, L"Folder", assets.wstring(), L"C:\\Pictures", true},
        {Step::Type::Winget, L"7-Zip", L"7zip.7zip", {}, true},
        {Step::Type::Copy, L"File", (root / L"single.txt").wstring(), L"%PUBLIC%\\Desktop", true},
    };
    CHECK(postSetupPayloadBytes(plan) == 4 + 2 + 8);
    REQUIRE(applyPostSetup(mount, plan, TaskContext{}));

    const auto folder = mount / L"Windows" / L"Setup" / L"Scripts" / L"WinLove";
    CHECK(readFile(folder / L"files" / L"1" / L"a.jpg") == "AAAA");
    CHECK(readFile(folder / L"files" / L"1" / L"sub" / L"b.jpg") == "BB");
    CHECK(readFile(folder / L"files" / L"3" / L"single.txt") == "one file");
    CHECK(readFile(folder / L"postsetup-machine.cmd").find("schtasks /create") != std::string::npos);
    CHECK(readFile(folder / L"postsetup-user.cmd").find("robocopy \"%WL%\\files\\3\" \"%PUBLIC%\\Desktop\"") != std::string::npos);
    const std::string taskXml = readFile(folder / L"postsetup-task.xml"); // UTF-16LE with BOM
    REQUIRE(taskXml.size() > 4);
    CHECK(static_cast<unsigned char>(taskXml[0]) == 0xFF);
    CHECK(static_cast<unsigned char>(taskXml[1]) == 0xFE);
    CHECK(taskXml[2] == '<');
    CHECK(taskXml[3] == '\0');
    const std::string setupComplete = readFile(mount / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd");
    CHECK(setupComplete.find("call \"%SystemRoot%\\Setup\\Scripts\\WinLove\\postsetup-machine.cmd\"") != std::string::npos);

    // A smaller plan replaces everything: no stale payload, no user script, the hook line once.
    plan.when = PostSetupPlan::When::SetupComplete;
    plan.steps = {{Step::Type::Command, L"x", L"echo hi", {}, true}};
    REQUIRE(applyPostSetup(mount, plan, TaskContext{}));
    CHECK_FALSE(std::filesystem::exists(folder / L"files"));
    CHECK_FALSE(std::filesystem::exists(folder / L"postsetup-user.cmd"));
    CHECK_FALSE(std::filesystem::exists(folder / L"postsetup-task.xml"));
    const std::string again = readFile(mount / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd");
    CHECK(again.find("postsetup-machine.cmd") != std::string::npos);
    CHECK(again == setupComplete);

    // An empty plan removes the scripts; the hook checks "if exist" and does nothing.
    REQUIRE(applyPostSetup(mount, {}, TaskContext{}));
    CHECK_FALSE(std::filesystem::exists(folder / L"postsetup-machine.cmd"));

    // A missing source or an invalid step is an error, not a silent skip.
    plan.steps = {{Step::Type::Copy, L"gone", (root / L"nope").wstring(), L"C:\\x", true}};
    const auto missing = applyPostSetup(mount, plan, TaskContext{});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::NotFound);
    plan.steps = {{Step::Type::Winget, L"bad", L"a b", {}, true}};
    CHECK_FALSE(applyPostSetup(mount, plan, TaskContext{}));
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("post-setup: the task runs for whoever logs on, elevated when the account allows") {
    const std::wstring xml = postSetupTaskXml();
    CHECK(has(xml, L"<LogonTrigger>"));
    CHECK(has(xml, L"<GroupId>S-1-5-32-545</GroupId>"));
    CHECK(has(xml, L"<RunLevel>HighestAvailable</RunLevel>"));
    CHECK(has(xml, L"postsetup-user.cmd"));
    CHECK(estimatePostSetupSeconds(sample()) > 45);
}
