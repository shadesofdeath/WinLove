#pragma once
// A preset file (P15): a named ChangeSet plus, when it has any, the answer file options (P13).
// The format is the ChangeSet JSON with two more keys, so older readers and `wlcli apply` still
// take it:  {"format":"winlove.changeset","version":1,"name":…,"operations":[…],
//            "unattend":{"includeInIso":true,"xml":"<unattend …>"},
//            "bootDrivers":["D:\\drivers\\vmd\\iaStorVD.inf", …]}   (D-056, Sürücüler › Kurulum ortamı)
// The answer file travels as its own XML: one reader / writer for it (core/unattend).
#include "app/state/AppState.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wl::app {

struct Preset {
    std::wstring name;
    core::ops::ChangeSet changes;
    std::optional<AppState::Unattend> unattend; // only when the preset carries answer file options
    std::vector<std::filesystem::path> bootDrivers; // INFs for boot.wim (not part of the queue)
    std::filesystem::path file;                 // library entry it was read from (empty: not saved)
};

[[nodiscard]] std::string presetToJson(const Preset& preset);
// `fallbackName`: for files without a name (plain ChangeSet files): usually the file stem.
[[nodiscard]] Result<Preset> presetFromJson(std::string_view json, std::wstring fallbackName);

[[nodiscard]] Result<Preset> readPreset(const std::filesystem::path& file);
[[nodiscard]] Result<void> writePreset(const std::filesystem::path& file, const Preset& preset);

// The queue and answer file of `state` as a preset (the answer file only when it says anything).
[[nodiscard]] Preset presetFromState(const AppState& state, std::wstring name);

} // namespace wl::app
