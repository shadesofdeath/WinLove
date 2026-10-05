#include "app/controllers/RegistryController.h"

#include <algorithm>
#include <cwctype>
#include <set>

namespace wl::app {

using core::RegistryWrite;
using core::ops::OpKind;
using core::ops::Operation;

namespace {
constexpr auto kRisk = core::ops::Risk::Medium; // the user's own values: Uygula cannot judge them
} // namespace

RegistryController::RegistryController(AppState& state) : m_state(state) {}

Operation RegistryController::operationFor(const RegistryWrite& write, core::ops::Risk risk, OpKind kind) {
    Operation op{kind, core::registryTarget(write), core::formatRegValue(write)};
    op.risk = risk;
    return op;
}

bool RegistryController::checked(const std::vector<RegistryWrite>& writes, OpKind kind) const {
    if (writes.empty()) {
        return false;
    }
    return std::ranges::all_of(writes, [&](const RegistryWrite& w) {
        const auto* op = m_state.changes().find(kind, core::registryTarget(w));
        return op && op->value == core::formatRegValue(w);
    });
}

void RegistryController::setChecked(const std::vector<RegistryWrite>& writes, bool on, core::ops::Risk risk,
                                    OpKind kind) {
    // One batch = one notification and one undo step (a .reg file may hold thousands of values).
    if (on) {
        std::vector<Operation> ops;
        ops.reserve(writes.size());
        for (const auto& w : writes) {
            ops.push_back(operationFor(w, risk, kind));
        }
        m_state.queueMany(std::move(ops));
        return;
    }
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& w : writes) {
        if (const auto* op = m_state.changes().find(kind, core::registryTarget(w));
            op && op->value == core::formatRegValue(w)) {
            slots.emplace_back(kind, core::registryTarget(w));
        }
    }
    m_state.unqueueMany(slots);
}

bool RegistryController::checked(std::size_t entry) const {
    const auto& entries = m_state.regImports();
    return entry < entries.size() && checked(entries[entry].writes, kindOf(entries[entry]));
}

std::pair<int, int> RegistryController::selection() const {
    int on = 0;
    const auto& entries = m_state.regImports();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        on += checked(i) ? 1 : 0;
    }
    return {on, static_cast<int>(entries.size())};
}

int RegistryController::checkedCount() const {
    return selection().first;
}

bool RegistryController::importable(const RegistryWrite& write) {
    return core::mapOfflineKey(write.key).has_value() || core::isPostSetupOnlyKey(write.key);
}

void RegistryController::requeue(const std::vector<RegistryWrite>& writes, OpKind kind) {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& w : writes) {
        slots.emplace_back(kind, core::registryTarget(w));
    }
    m_state.unqueueMany(slots);
    setChecked(writes, true, kRisk, kind);
}

void RegistryController::addImport(const std::filesystem::path& file, std::vector<RegistryWrite> writes) {
    // Writes the image cannot take (HKLM\SAM, other users' SIDs…) are dropped from the queue here;
    // the page shows how many were skipped.
    // An import applies entries in order and the last one of a slot wins: keep only that one, at
    // its place ("x"=1, [-key], "x"=2 must end with x=2 after the delete).
    std::vector<RegistryWrite> usable;
    std::set<std::wstring> seen;
    std::size_t skipped = 0;
    for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
        if (!importable(*it)) {
            ++skipped;
            continue;
        }
        std::wstring slot = core::registryTarget(*it);
        std::ranges::transform(slot, slot.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (seen.insert(std::move(slot)).second) {
            usable.push_back(std::move(*it));
        }
    }
    std::ranges::reverse(usable);
    // Re-importing the same file: what the old copy queued goes first.
    const auto& entries = m_state.regImports();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].typed && entries[i].file == file) {
            setChecked(entries[i].writes, false, kRisk, kindOf(entries[i]));
        }
    }
    requeue(usable, kImportKind);
    m_state.addRegImport(AppState::RegImport{file, std::move(usable), skipped});
}

bool RegistryController::addValue(RegistryWrite write, bool afterSetup) {
    if (!importable(write)) {
        return false;
    }
    AppState::RegImport entry{{}, {std::move(write)}, 0, /*typed=*/true, afterSetup};
    requeue(entry.writes, kindOf(entry));
    m_state.addRegImport(std::move(entry));
    return true;
}

bool RegistryController::replaceValue(std::size_t index, RegistryWrite write, bool afterSetup) {
    const auto& entries = m_state.regImports();
    if (index >= entries.size() || !entries[index].typed || !importable(write)) {
        return false;
    }
    const bool on = checked(index);
    setChecked(entries[index].writes, false, kRisk, kindOf(entries[index]));
    AppState::RegImport entry{{}, {std::move(write)}, 0, /*typed=*/true, afterSetup};
    if (on) {
        requeue(entry.writes, kindOf(entry));
    }
    m_state.replaceRegImport(index, std::move(entry));
    return true;
}

void RegistryController::toggle(std::size_t index) {
    const auto& entries = m_state.regImports();
    if (index < entries.size()) {
        const bool on = checked(index);
        if (on) {
            setChecked(entries[index].writes, false, kRisk, kindOf(entries[index]));
        } else {
            requeue(entries[index].writes, kindOf(entries[index]));
        }
    }
}

void RegistryController::remove(std::size_t index) {
    const auto& entries = m_state.regImports();
    if (index < entries.size()) {
        setChecked(entries[index].writes, false, kRisk, kindOf(entries[index]));
        m_state.removeRegImport(index);
    }
}

} // namespace wl::app
