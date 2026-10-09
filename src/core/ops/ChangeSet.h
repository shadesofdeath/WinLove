#pragma once
// The change queue (D-003, ARCHITECTURE §2.3): nothing touches the image until "Uygula".
// Operations are plain values (kind + target + optional value) so they serialize trivially
// (presets are ChangeSet files) and the Applier dispatches on `kind`.
//
// Rules:
// - one operation per (kind, target); adding again replaces the old one;
// - adding the inverse of a queued operation cancels both (e.g. Disable then Enable the same
//   feature = back to the image's original state, nothing to do);
// - every mutation is undoable (Ctrl+Z / Ctrl+Y in the UI).
#include "base/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wl::core::ops {

enum class OpKind : std::uint8_t {
    RemovePackage,
    RemoveCapability,
    RemoveAppx,
    DisableFeature,
    EnableFeature,
    AddDriver,
    AddPackage,
    SetRegistryValue,
    SetServiceStart,
    SetRegistryFirstLogon, // written offline AND re-applied after OOBE (SetupComplete / first logon)
    SetPostSetup,          // P14: target "postsetup", value = the whole plan as JSON (one slot)
    RemoveComponent,       // P07 system component: target = catalog id, value = its recipe as JSON
    CleanupImage,          // component store cleanup: target "component-store", value = options JSON
    SetEdition,            // edition upgrade: target "edition" (one slot), value = the edition id ("Professional")
    WriteFile,             // a text file in the image: target = its path from the image root, value = the text
    CopyFile,              // a file of this PC into the image: target = path in the image, value = the source
    // 2026-10-01 (D-048 … D-054):
    SetTaskState,          // scheduled task after setup: target = "\Microsoft\…\Name", value = "disabled" | "enabled"
    SetHosts,              // hosts entries: target "hosts" (one slot), value = the WinLove block (lines)
    SetDns,                // DNS servers / DoH after setup: target "dns" (one slot), value = JSON
    CopyTree,              // a file or folder of this PC into the image: target = folder in the image, value = source
    RemoveDriver,          // a third-party driver of the image: target = published name (oem3.inf), value = original name
    AddAppx,               // provision an .appx / .msix (bundle): target = package file, value = JSON (dependencies, license)
    SetDefaultApps,        // default app associations: target "associations" (one slot), value = the XML
    SetIntl,               // UI language, locales, keyboard, time zone: target "intl" (one slot), value = JSON
    // 2026-10-01 (D-056):
    SetPicture,            // default picture: target = "wallpaper" | "lockscreen" | "account" | "oemlogo", value = source file
    AddFont,               // font: target = file name in Windows\Fonts, value = source file
    // 2026-10-05 (D-068):
    PatchIcons,            // icons inside a Windows file: target = its path from the image root, value = JSON (IconPatch.h)
    // 2026-10-07 (D-079):
    ShrinkStore,           // WinSxS at its smallest, irreversible: target "component-store-shrink" (one slot), value = JSON (StoreShrink.h)
    // 2026-10-09 (D-102):
    CreateTask,            // custom recurring tasks created after setup: target "tasks-create" (one slot), value = JSON (ScheduledTasks.h)
};

enum class Risk : std::uint8_t { Low, Medium, High };

struct Operation {
    OpKind kind;
    std::wstring target;         // feature/package/capability name, driver/package path, registry path…
    std::wstring value;          // optional: registry data, service start type…
    Risk risk = Risk::Low;
    std::int64_t sizeDelta = 0;  // estimated bytes (negative = saved)

    [[nodiscard]] bool sameSlot(const Operation& other) const noexcept;
};

[[nodiscard]] const char* opKindKey(OpKind kind) noexcept;          // "disableFeature" (JSON)
[[nodiscard]] Result<OpKind> opKindFromKey(std::string_view key);

class ChangeSet {
public:
    void add(Operation op);
    bool remove(OpKind kind, std::wstring_view target);
    // Batches (a whole .reg import): one undo step, no per-item history copy.
    void addAll(std::vector<Operation> ops);
    std::size_t removeAll(const std::vector<std::pair<OpKind, std::wstring>>& slots);
    void clear();

    [[nodiscard]] const std::vector<Operation>& operations() const noexcept { return m_ops; }
    [[nodiscard]] std::size_t size() const noexcept { return m_ops.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_ops.empty(); }
    [[nodiscard]] const Operation* find(OpKind kind, std::wstring_view target) const;
    [[nodiscard]] std::size_t count(OpKind kind) const;
    [[nodiscard]] std::int64_t estimatedSizeDelta() const;
    // Bumped on every change: cheap "did anything change?" for UI badges.
    [[nodiscard]] std::uint64_t version() const noexcept { return m_version; }

    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    bool undo();
    bool redo();

    // Preset file format: {"format":"winlove.changeset","version":1,"operations":[...]}
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static Result<ChangeSet> fromJson(std::string_view json);

private:
    void snapshot();
    void addOne(Operation op);
    bool removeOne(OpKind kind, std::wstring_view target);
    std::vector<Operation> m_ops;
    std::vector<std::vector<Operation>> m_undo;
    std::vector<std::vector<Operation>> m_redo;
    std::uint64_t m_version = 0;
};

} // namespace wl::core::ops
