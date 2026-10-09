#pragma once
// D-084 / D-085: WinLove's own setup screens in place of Windows' OOBE. The answer file runs
// ProgramData\WinLove\Oobe\oobe.ps1 (resources/scripts/oobe.ps1) as the last command of Setup's
// specialize pass: after Windows' logo, as SYSTEM, full screen, before any account exists. Whoever
// installs picks a wireless network, the account, the computer's name and time zone, the look, a few
// habits and how much Windows may collect; the script creates the account, puts its one automatic
// sign-in into Setup's own answer file (oobeSystem) and Setup goes on: OOBE's pages are hidden, the
// account signs in, a task at that sign-in clears the password Windows kept (ENGINE.md, VM).
#include "core/image/Source.h"
#include "core/ops/ChangeSet.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::core {

struct WelcomePlan {
    bool networkPage = true; // shown only where the PC has a wireless adapter
    bool computerPage = true;
    bool lookPage = true;
    bool wallpaperPage = true; // D-099: a desktop background from Windows' own set
    bool bundlesPage = true;   // D-099: program bundles the user picks, installed at the first sign-in
    bool prefsPage = true;
    bool privacyPage = true;
    bool allowEmptyPassword = true;
    std::wstring theme = L"dark";     // the pre-selected choice: "dark" | "light"
    std::wstring accent = L"#0078D4"; // "#RRGGBB", one of kWelcomeAccents (Windows' own blue)
    std::wstring privacy = L"strict"; // "strict" | "windows"
    std::wstring computerName;        // suggested; empty: made from the account's name
    std::wstring timeZone;            // pre-selected (Windows' id, "Turkey Standard Time"); empty: the PC's
    // The window's texts in the language of whoever installs (the app's strings, by key); a key
    // missing here shows empty.
    std::vector<std::pair<std::string, std::wstring>> texts;

    [[nodiscard]] bool operator==(const WelcomePlan&) const = default;
};

inline constexpr wchar_t kWelcomeFolder[] = LR"(ProgramData\WinLove\Oobe)";
// The answer file's computer name while the welcome is on and none was given: Setup applies a file's
// name at the start of specialize (the welcome renames after it), and with no name at all Windows'
// OOBE gives the PC a random one of its own after the welcome (VM spec3, D-085).
inline constexpr wchar_t kWelcomeComputerName[] = L"WINLOVE-PC";
// The colours the look page offers, from Windows' own accent list (names are texts: accentBlue …),
// and WinLove's copper last.
inline constexpr std::pair<const char*, const wchar_t*> kWelcomeAccents[] = {
    {"accentBlue", L"#0078D4"},   {"accentNavy", L"#0063B1"},   {"accentTeal", L"#038387"},
    {"accentGreen", L"#107C10"},  {"accentPurple", L"#8764B8"}, {"accentPink", L"#E3008C"},
    {"accentRed", L"#E81123"},    {"accentOrange", L"#CA5010"}, {"accentCopper", L"#D4905A"}};

// oobe.json: the pages, the choices, the texts.
[[nodiscard]] std::string welcomeJson(const WelcomePlan& plan);
// The script and its oobe.json into the image (WriteFile, ProgramData\WinLove\Oobe).
[[nodiscard]] std::vector<ops::Operation> welcomeOperations(const WelcomePlan& plan);
[[nodiscard]] std::optional<WelcomePlan> welcomePlanFromOperations(const std::vector<ops::Operation>& ops);
[[nodiscard]] std::vector<std::pair<ops::OpKind, std::wstring>> welcomeSlots();
// A preset keeps the script as it was when it was saved: the app's own one goes in when the preset is
// applied (an older copy wrote an empty password as spaces, D-087). Its oobe.json (the choices) stays.
[[nodiscard]] ops::ChangeSet withCurrentWelcomeScript(ops::ChangeSet changes);
// The specialize pass's command: Setup waits for the script (the pages, then the account).
[[nodiscard]] std::wstring welcomeSetupCommand();
// The editions of `source` whose file list has no welcome script (Apply puts it in): Setup would
// end without an account. `onlyIndex` > 0: that edition alone.
// Read without a mount, like editionsWithoutWinre. Unsupported for ESD.
[[nodiscard]] Result<std::vector<int>> editionsWithoutWelcome(const SourceInfo& source, int onlyIndex = 0);

} // namespace wl::core
