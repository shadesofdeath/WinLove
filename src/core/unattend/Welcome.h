#pragma once
// D-084: WinLove's own welcome in place of Windows' account pages. The answer file creates a setup
// account (kWelcomeAccount, a random password) that signs in once by itself; its first sign-in
// starts ProgramData\WinLove\Oobe\oobe.ps1 (resources/scripts/oobe.ps1) full screen once Windows'
// own first sign-in screen is gone. Whoever installs picks the account, the computer's name, the
// look and how much Windows may collect; the script creates the account, removes the setup account
// at the next start, lets the new account sign in once and clears that password afterwards
// (ENGINE.md, VM-proven 2026-10-08).
#include "core/image/Source.h"
#include "core/ops/ChangeSet.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::core {

struct WelcomePlan {
    bool computerPage = true;
    bool lookPage = true;
    bool privacyPage = true;
    bool allowEmptyPassword = true;
    std::wstring theme = L"dark";     // the pre-selected choice: "dark" | "light"
    std::wstring accent = L"#D4905A"; // "#RRGGBB", one of kWelcomeAccents
    std::wstring privacy = L"strict"; // "strict" | "windows"
    std::wstring computerName;        // suggested; empty: made from the account's name
    // The window's texts in the language of whoever installs (the app's strings, by key); a key
    // missing here shows empty.
    std::vector<std::pair<std::string, std::wstring>> texts;

    [[nodiscard]] bool operator==(const WelcomePlan&) const = default;
};

inline constexpr wchar_t kWelcomeFolder[] = LR"(ProgramData\WinLove\Oobe)";
inline constexpr wchar_t kWelcomeAccount[] = L"WinLoveSetup";
// The colours the look page offers (names are texts: accentCopper, accentBlue …).
inline constexpr std::pair<const char*, const wchar_t*> kWelcomeAccents[] = {
    {"accentCopper", L"#D4905A"}, {"accentBlue", L"#0078D4"}, {"accentGreen", L"#107C10"},
    {"accentPurple", L"#8764B8"}, {"accentRed", L"#E81123"},  {"accentOrange", L"#CA5010"}};

// oobe.json: the choices, the texts, the setup account's name.
[[nodiscard]] std::string welcomeJson(const WelcomePlan& plan);
// The script and its oobe.json into the image (WriteFile, ProgramData\WinLove\Oobe).
[[nodiscard]] std::vector<ops::Operation> welcomeOperations(const WelcomePlan& plan);
[[nodiscard]] std::optional<WelcomePlan> welcomePlanFromOperations(const std::vector<ops::Operation>& ops);
[[nodiscard]] std::vector<std::pair<ops::OpKind, std::wstring>> welcomeSlots();
// The setup account's first-sign-in command: the script on its own (the command list goes on).
[[nodiscard]] std::wstring welcomeFirstLogonCommand();
// The editions of `source` whose file list has no welcome script (Apply puts it in): Setup would
// leave whoever installs signed in to the setup account. `onlyIndex` > 0: that edition alone.
// Read without a mount, like editionsWithoutWinre. Unsupported for ESD.
[[nodiscard]] Result<std::vector<int>> editionsWithoutWelcome(const SourceInfo& source, int onlyIndex = 0);
// 20 characters from BCryptGenRandom: letters and digits (Windows' complexity rules: three classes).
[[nodiscard]] std::wstring randomWelcomePassword();

} // namespace wl::core
