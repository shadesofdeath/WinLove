#pragma once
// D-125 "Güncellemelere karşı koru" (the user's choice: what the reference tool lacks). An image's choices do
// not all survive Windows Update: a cumulative update can turn a service back on, reset a policy,
// enable a task again or provision an app again, and it puts back the files of a component whose
// store copy was kept (D-113). When the setting is on, Apply ends by writing into the image what
// its steps that ran asked for (guard.json) and resources/scripts/updateguard.ps1; SetupComplete
// registers the task "\WinLove\Update guard" (SYSTEM: at startup, after an update, daily) that
// repairs what an update changed and leaves what the user changed (the rule is in the script).
//
// Kept: services' start types, machine-wide registry values (HKLM; per-user ones are written once
// into the default profile and the user's own after that), removed apps (provisioned again →
// removed, also when the user did not ask), scheduled tasks switched off. Removed components are
// only reported when their files come back. Nothing a step did not do is guarded.
#include "base/Result.h"
#include "core/ops/ChangeSet.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::ops {

struct UpdateGuardService {
    std::wstring name;
    std::uint32_t start = 4; // 2 auto, 3 manual, 4 disabled
    bool delayed = false;    // auto: DelayedAutostart
    [[nodiscard]] bool operator==(const UpdateGuardService&) const = default;
};

struct UpdateGuardValue {
    std::wstring key;  // "HKLM\SOFTWARE\Policies\…"
    std::wstring name; // empty: the default value
    // dword / qword (data in decimal), sz / expandsz (the text), multisz (lines joined by \n),
    // binary (hex), delete (the value must not be there; data empty).
    std::wstring type;
    std::wstring data;
    [[nodiscard]] bool operator==(const UpdateGuardValue&) const = default;
};

struct UpdateGuardApp {
    std::wstring name;   // identity name: "Microsoft.BingNews"
    std::wstring family; // "Microsoft.BingNews_8wekyb3d8bbwe" when the package's full name said it
    [[nodiscard]] bool operator==(const UpdateGuardApp&) const = default;
};

struct UpdateGuardComponent {
    std::wstring title;
    std::vector<std::wstring> paths; // relative to the system drive, as in the recipe
    [[nodiscard]] bool operator==(const UpdateGuardComponent&) const = default;
};

struct UpdateGuardPlan {
    std::vector<UpdateGuardService> services;
    std::vector<UpdateGuardValue> values;
    std::vector<UpdateGuardApp> apps;
    std::vector<std::wstring> tasks; // to stay switched off: "\Microsoft\Windows\…\Name"
    std::vector<UpdateGuardComponent> components;
    bool reportOnly = false;

    [[nodiscard]] bool empty() const noexcept {
        return services.empty() && values.empty() && apps.empty() && tasks.empty() && components.empty();
    }
};

// What `ran` (the operations whose steps succeeded, in order: a later write wins) asks to keep.
[[nodiscard]] UpdateGuardPlan updateGuardPlan(const std::vector<Operation>& ran);
[[nodiscard]] std::string updateGuardToJson(const UpdateGuardPlan& plan);
[[nodiscard]] Result<UpdateGuardPlan> updateGuardFromJson(std::string_view json);

inline constexpr std::wstring_view kUpdateGuardFolder = L"ProgramData\\WinLove\\Guard";
inline constexpr std::wstring_view kUpdateGuardSetupScript = L"updateguard.cmd";

// <image>\ProgramData\WinLove\Guard\{updateguard.ps1, guard.json} and the SetupComplete line that
// registers the task. An empty plan writes nothing.
[[nodiscard]] Result<void> writeUpdateGuard(const std::filesystem::path& mountDir, const UpdateGuardPlan& plan);

} // namespace wl::core::ops
