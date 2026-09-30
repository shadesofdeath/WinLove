#include "app/controllers/ImageSettingsController.h"

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

ImageSettingsController::ImageSettingsController(AppState& state, ImageSettingsCatalog catalog)
    : m_state(state), m_catalog(std::move(catalog)) {}

std::vector<Operation> ImageSettingsController::operationsFor(const ImageSetting& setting, int option) {
    std::vector<Operation> ops;
    if (option < 0 || option >= static_cast<int>(setting.options.size())) {
        return ops;
    }
    const auto& chosen = setting.options[static_cast<std::size_t>(option)];
    const OpKind kind = setting.firstLogon ? OpKind::SetRegistryFirstLogon : OpKind::SetRegistryValue;
    for (const auto& write : chosen.writes) {
        Operation op{kind, core::registryTarget(write), core::formatRegValue(write)};
        op.risk = setting.risk;
        ops.push_back(std::move(op));
    }
    for (const auto& [name, start] : chosen.services) {
        Operation op{OpKind::SetServiceStart, name, core::startTypeKey(start)};
        op.risk = setting.risk;
        ops.push_back(std::move(op));
    }
    return ops;
}

namespace {
bool queued(const core::ops::ChangeSet& changes, const Operation& op) {
    const auto* found = changes.find(op.kind, op.target);
    return found && found->value == op.value;
}
} // namespace

int ImageSettingsController::optionIn(const core::ops::ChangeSet& changes, const ImageSetting& setting) {
    // Options of one setting may share slots with different values, never a whole set; when two
    // match anyway (a preset edited by hand) the one that says more wins.
    int best = setting.defaultOption;
    std::size_t bestSize = 0;
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        const auto ops = operationsFor(setting, i);
        if (ops.size() > bestSize && std::ranges::all_of(ops, [&](const Operation& op) { return queued(changes, op); })) {
            best = i;
            bestSize = ops.size();
        }
    }
    return best;
}

void ImageSettingsController::collectQueued(const ImageSetting& setting,
                                            std::vector<std::pair<OpKind, std::wstring>>& slots) const {
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        for (auto& op : operationsFor(setting, i)) {
            if (queued(m_state.changes(), op)) {
                slots.emplace_back(op.kind, std::move(op.target));
            }
        }
    }
}

void ImageSettingsController::select(const ImageSetting& setting, int option) {
    if (option == current(setting)) {
        return;
    }
    std::vector<std::pair<OpKind, std::wstring>> slots;
    collectQueued(setting, slots);
    m_state.unqueueMany(slots);
    m_state.queueMany(operationsFor(setting, option));
}

int ImageSettingsController::applyRecommended() {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    std::vector<Operation> ops;
    int changed = 0;
    for (const auto& setting : m_catalog.settings()) {
        if (setting.recommended < 0 || setting.recommended == current(setting)) {
            continue;
        }
        ++changed;
        collectQueued(setting, slots);
        auto add = operationsFor(setting, setting.recommended);
        ops.insert(ops.end(), std::make_move_iterator(add.begin()), std::make_move_iterator(add.end()));
    }
    m_state.unqueueMany(slots);
    m_state.queueMany(std::move(ops));
    return changed;
}

int ImageSettingsController::changedCount() const {
    return static_cast<int>(std::ranges::count_if(
        m_catalog.settings(), [&](const ImageSetting& s) { return current(s) != s.defaultOption; }));
}

} // namespace wl::app
