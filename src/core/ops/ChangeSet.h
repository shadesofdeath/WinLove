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
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
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
