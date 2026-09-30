#include "app/controllers/RegistryController.h"

#include <algorithm>
#include <cwctype>
#include <set>

namespace wl::app {

using core::RegistryWrite;
using core::ops::OpKind;
using core::ops::Operation;

RegistryController::RegistryController(AppState& state, TweakCatalog catalog)
    : m_state(state), m_catalog(std::move(catalog)) {}

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

void RegistryController::toggle(const Tweak& tweak) {
    setChecked(tweak.writes, !checked(tweak), tweak.risk, kindOf(tweak));
}

std::pair<int, int> RegistryController::selection(std::string_view category) const {
    int on = 0;
    int total = 0;
    if (category == "custom") {
        for (const auto& import : m_state.regImports()) {
            ++total;
            on += checked(import.writes, kImportKind) ? 1 : 0;
        }
        return {on, total};
    }
    for (const auto& tweak : m_catalog.tweaks()) {
        if (tweak.category == category) {
            ++total;
            on += checked(tweak) ? 1 : 0;
        }
    }
    return {on, total};
}

int RegistryController::checkedCount() const {
    int n = 0;
    for (const auto& tweak : m_catalog.tweaks()) {
        n += checked(tweak) ? 1 : 0;
    }
    return n + selection("custom").first;
}

bool RegistryController::importable(const RegistryWrite& write) {
    return core::mapOfflineKey(write.key).has_value() || core::isPostSetupOnlyKey(write.key);
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
    // Slots an earlier import or preset already queued move to this file's position in the queue.
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& w : usable) {
        slots.emplace_back(kImportKind, core::registryTarget(w));
    }
    m_state.unqueueMany(slots);
    setChecked(usable, true, core::ops::Risk::Medium, kImportKind);
    m_state.addRegImport(AppState::RegImport{file, std::move(usable), skipped});
}

void RegistryController::toggleImport(std::size_t index) {
    const auto& imports = m_state.regImports();
    if (index < imports.size()) {
        setChecked(imports[index].writes, !checked(imports[index].writes, kImportKind), core::ops::Risk::Medium,
                   kImportKind);
    }
}

void RegistryController::removeImport(std::size_t index) {
    const auto& imports = m_state.regImports();
    if (index < imports.size()) {
        setChecked(imports[index].writes, false, core::ops::Risk::Medium, kImportKind);
        m_state.removeRegImport(index);
    }
}

} // namespace wl::app
