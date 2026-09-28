#include "app/controllers/RegistryController.h"

#include <algorithm>

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
    for (const auto& w : writes) {
        if (on) {
            m_state.queue(operationFor(w, risk, kind));
        } else if (const auto* op = m_state.changes().find(kind, core::registryTarget(w));
                   op && op->value == core::formatRegValue(w)) {
            m_state.unqueue(kind, core::registryTarget(w));
        }
    }
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
            on += checked(import.writes) ? 1 : 0;
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

void RegistryController::addImport(const std::filesystem::path& file, std::vector<RegistryWrite> writes) {
    // Writes the image cannot take (HKLM\SAM, other users' SIDs…) are dropped from the queue here;
    // the page shows how many were skipped.
    std::vector<RegistryWrite> usable;
    std::size_t skipped = 0;
    for (auto& w : writes) {
        if (core::mapOfflineKey(w.key)) {
            usable.push_back(std::move(w));
        } else {
            ++skipped;
        }
    }
    setChecked(usable, true, core::ops::Risk::Medium);
    m_state.addRegImport(AppState::RegImport{file, std::move(usable), skipped});
}

void RegistryController::toggleImport(std::size_t index) {
    const auto& imports = m_state.regImports();
    if (index < imports.size()) {
        setChecked(imports[index].writes, !checked(imports[index].writes), core::ops::Risk::Medium);
    }
}

void RegistryController::removeImport(std::size_t index) {
    const auto& imports = m_state.regImports();
    if (index < imports.size()) {
        setChecked(imports[index].writes, false, core::ops::Risk::Medium);
        m_state.removeRegImport(index);
    }
}

} // namespace wl::app
