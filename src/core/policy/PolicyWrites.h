#pragma once
// D-154: a policy's state (Etkin / Devre dışı) and its options → registry writes, the way the Group
// Policy registry extension writes them:
//   Enabled  — valueName: enabledValue (none given: DWORD 1); enabledList; each element's value
//              (boolean: trueValue / falseValue, defaults DWORD 1 / 0, and their lists; decimal: DWORD,
//              or REG_SZ with storeAsText; longDecimal: QWORD; text: REG_SZ / REG_EXPAND_SZ;
//              multiText: REG_MULTI_SZ; enum: the chosen item's value and valueList; list: one value per
//              entry, named valuePrefix + 1, 2 … or by the entry itself, "name=value" with explicitValue);
//   Disabled — valueName: disabledValue (none given: DWORD 0); disabledList; the elements' values deleted.
// "Not configured" writes nothing (the queue holds no operation then).
//
// The queue keeps one SetPolicy operation per policy and scope: target "machine\<id>" / "user\<id>",
// value JSON {"name", "state", "values", "writes": [{"key", "name", "value" (.reg syntax)}]} — the
// writes are resolved when queued, so Uygula (and a preset on another build) needs no ADMX.
// Uygula writes them into the image's hives (HKLM → SOFTWARE, HKCU → the default profile) and into the
// local Group Policy object (Windows\System32\GroupPolicy\{Machine,User}\Registry.pol + gpt.ini), so
// gpedit.msc on the installed Windows shows the policy as set.
#include "base/Result.h"
#include "core/image/RegistryEdit.h"
#include "core/policy/Admx.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core::policy {

enum class PolicyState : std::uint8_t { NotConfigured, Enabled, Disabled };

// Element id → its value as text: boolean "1" / "0"; decimal digits; text as typed; enum the item's
// index ("0", "1" …); list / multiText one entry per line ("\n"; list with explicitValue "name=value").
using PolicyValues = std::map<std::wstring, std::wstring>;

struct PolicyChoice {
    PolicyState state = PolicyState::NotConfigured;
    PolicyValues values;
    [[nodiscard]] bool operator==(const PolicyChoice&) const = default;
};

// What gpedit starts with when the policy is enabled: the presentation's defaults (enum: defaultItem
// or the first item; boolean: defaultChecked; decimal: defaultValue or minValue; text: defaultValue).
[[nodiscard]] PolicyValues defaultValues(const Policy& policy);
// The elements the user must still fill in before Enabled can be written (required, empty text / list).
[[nodiscard]] std::vector<std::wstring> missingValues(const Policy& policy, const PolicyValues& values);

// Errors: a value out of range or not a number, a required value missing, a bad enum index.
[[nodiscard]] Result<std::vector<RegistryWrite>> policyWrites(const Policy& policy, bool user, const PolicyChoice& choice);

// "machine\<id>" / "user\<id>"; the other way (false when it is not a policy target).
[[nodiscard]] std::wstring policyTarget(std::wstring_view id, bool user);
[[nodiscard]] bool parsePolicyTarget(std::wstring_view target, std::wstring& id, bool& user);

// The operation's value: {"name","state","values","writes"}.
struct PolicyOperation {
    std::wstring name; // the policy's display name (logs, presets)
    PolicyChoice choice;
    std::vector<RegistryWrite> writes;
};
[[nodiscard]] std::wstring policyOperationJson(const PolicyOperation& op);
[[nodiscard]] Result<PolicyOperation> parsePolicyOperation(std::wstring_view json);

// ---- the local Group Policy object (Registry.pol) ---------------------------------------------
// PReg format: "PReg" + version 1, then entries [key;value;type;size;data] in UTF-16LE.
struct PolEntry {
    std::wstring key;       // "Software\Policies\…" (no root)
    std::wstring valueName; // "**del.X" deletes X
    std::uint32_t type = 1;
    std::vector<std::uint8_t> data;
    [[nodiscard]] bool operator==(const PolEntry&) const = default;
};
[[nodiscard]] Result<std::vector<PolEntry>> parsePol(std::string_view bytes);
[[nodiscard]] std::string formatPol(const std::vector<PolEntry>& entries);
// A write into a file's entries: replaces the entry of the same value (and its "**del." twin); a
// delete becomes "**del.<name>". Key creations / deletions are not recorded.
void mergeIntoPol(std::vector<PolEntry>& entries, const RegistryWrite& write);
// Records the writes (HKLM\… → Machine, HKCU\… → User) into <mount>\Windows\System32\GroupPolicy and
// raises gpt.ini's version of the side(s) written, creating what is missing.
[[nodiscard]] Result<void> recordInGroupPolicy(const std::filesystem::path& mountDir, std::span<const RegistryWrite> writes);

} // namespace wl::core::policy
