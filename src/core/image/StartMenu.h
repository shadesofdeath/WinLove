#pragma once
// D-069: the Windows 11 Start menu of an image — which apps can be pinned, and the pin list.
//
// Candidates come from the mounted image itself:
//   - packaged apps: Program Files\WindowsApps\<full name>\AppxManifest.xml and
//     Windows\SystemApps\<family>\AppxManifest.xml — one entry per <Application> the Start menu
//     lists (AppListEntry ≠ none), id = AUMID "<family name>!<application id>"; frameworks and
//     resource packages are skipped, the highest version of a family wins;
//   - desktop apps: the shortcuts (.lnk) under ProgramData\Microsoft\Windows\Start Menu\Programs
//     (%ALLUSERSPROFILE%) and Users\Default\AppData\Roaming\Microsoft\Windows\Start Menu\Programs
//     (%APPDATA% of every new account);
//   - Microsoft Edge (desktopAppId "MSEdge").
// The pin list is Microsoft's ConfigureStartPins JSON: {"pinnedList": [{"packagedAppId": …},
// {"desktopAppLink": "%ALLUSERSPROFILE%\…\X.lnk"}, {"desktopAppId": "MSEdge"}]}.
#include "base/Result.h"
#include "core/ops/ChangeSet.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct StartApp {
    enum class Kind : std::uint8_t { Packaged, DesktopLink, DesktopId };
    Kind kind = Kind::Packaged;
    std::wstring id;    // AUMID, the link with its environment variable, or "MSEdge"
    std::wstring name;  // what the list shows
    std::wstring icon;  // image-relative file for a preview (a logo .png, the shortcut's .exe / .ico), may be empty
    int iconIndex = 0;  // PrivateExtractIcons index into `icon`
    [[nodiscard]] bool operator==(const StartApp& other) const { return kind == other.kind && id == other.id; }
};

[[nodiscard]] std::vector<StartApp> listStartApps(const std::filesystem::path& mountDir);

[[nodiscard]] std::string startPinsJson(const std::vector<StartApp>& pins);
// The pins of a ConfigureStartPins JSON (names are not in it: the ids are kept as names).
[[nodiscard]] Result<std::vector<StartApp>> startPinsFromJson(std::string_view json);

// ---- the plan: what the Başlat menüsü page queues -------------------------------------------
// Empty: no pins at all (Windows 10: no tiles either). Custom: exactly `pins`, in order.
// applyOnce: Windows applies the list once at the first sign-in and the user may change it
// afterwards (24H2 with the July 2025 update and later); off: it comes back at every sign-in.
struct StartPinsPlan {
    bool custom = false;
    std::vector<StartApp> pins; // custom only
    bool applyOnce = true;
};
inline constexpr wchar_t kStartPinsFile[] = LR"(ProgramData\WinLove\StartPins.json)";
[[nodiscard]] std::string startPinsJson(const StartPinsPlan& plan);
[[nodiscard]] std::vector<ops::Operation> startPinsOperations(const StartPinsPlan& plan);
// The plan a queue holds (from the StartPins.json WriteFile operation); nullopt: Windows' own.
[[nodiscard]] std::optional<StartPinsPlan> startPinsPlanFromOperations(const std::vector<ops::Operation>& ops);
// Every slot the plan's operations use (to take them out of the queue).
[[nodiscard]] std::vector<std::pair<ops::OpKind, std::wstring>> startPinsSlots();

// Builds whose Start applies a custom pin list (measured, D-069: 26200.8037 did not, 26200.9457
// did — on Pro and on Home). An empty list applies on every build (with the empty Start state).
inline constexpr int kStartPinsMinUbr = 9457;
[[nodiscard]] constexpr bool startAppliesCustomPins(int build, int ubr) noexcept {
    return build > 26200 || ((build == 26100 || build == 26200) && ubr >= kStartPinsMinUbr);
}

// "Microsoft.WindowsCalculator_8wekyb3d8bbwe" from "Microsoft.WindowsCalculator_11.2405.2.0_x64__8wekyb3d8bbwe";
// empty for a resource / bundle folder name it does not understand.
[[nodiscard]] std::wstring familyFromFullName(std::wstring_view fullName);
// "Microsoft.WindowsCalculator" → "Windows Calculator" (when the manifest's name is a resource).
[[nodiscard]] std::wstring readableAppName(std::wstring_view identity);

} // namespace wl::core
