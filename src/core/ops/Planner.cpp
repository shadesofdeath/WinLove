#include "core/ops/Planner.h"

#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <format>

namespace wl::core::ops {

Phase phaseOf(OpKind kind) noexcept {
    switch (kind) {
    case OpKind::SetEdition: return Phase::Edition;
    case OpKind::RemovePackage:
    case OpKind::RemoveCapability:
    case OpKind::RemoveComponent:
    case OpKind::RemoveDriver:
    case OpKind::RemoveAppx: return Phase::Remove;
    case OpKind::AddAppx: return Phase::Apps;
    case OpKind::CleanupImage: return Phase::Cleanup;
    case OpKind::DisableFeature:
    case OpKind::EnableFeature: return Phase::Features;
    case OpKind::AddDriver: return Phase::Drivers;
    case OpKind::AddPackage: return Phase::Updates;
    case OpKind::SetRegistryValue:
    case OpKind::SetRegistryFirstLogon:
    case OpKind::SetPostSetup:
    case OpKind::WriteFile:
    case OpKind::CopyFile:
    case OpKind::SetTaskState:
    case OpKind::SetHosts:
    case OpKind::SetDns:
    case OpKind::CopyTree:
    case OpKind::SetDefaultApps:
    case OpKind::SetIntl:
    case OpKind::SetPicture:
    case OpKind::AddFont:
    case OpKind::SetServiceStart: return Phase::Settings;
    }
    return Phase::Settings;
}

Phase phaseOf(const Operation& op) {
    if (op.kind == OpKind::RemoveComponent) {
        // The recipe's JSON (SystemComponents.h: componentRecipeToJson) — read here without that
        // header, which brings <windows.h> and its CopyFile macro into OpKind::CopyFile.
        const auto doc = nlohmann::json::parse(utf8::fromWide(op.value), nullptr, /*allow_exceptions=*/false);
        if (doc.is_object() && doc.contains("driverClasses") && doc["driverClasses"].is_array() && !doc["driverClasses"].empty()) {
            return Phase::DeepRemove;
        }
    }
    return phaseOf(op.kind);
}

bool ApplyPlan::hasHighRisk() const {
    return std::ranges::any_of(steps, [](const PlanStep& s) { return s.operation.risk == Risk::High; });
}

double estimateSeconds(OpKind kind) noexcept {
    switch (kind) {
    case OpKind::RemovePackage: return 8.0;
    case OpKind::RemoveCapability: return 15.0;
    case OpKind::RemoveAppx: return 3.0;
    case OpKind::DisableFeature: return 8.0;
    case OpKind::EnableFeature: return 20.0;
    case OpKind::AddDriver: return 6.0;
    case OpKind::AddPackage: return 60.0;
    case OpKind::SetRegistryValue:
    case OpKind::WriteFile:
    case OpKind::CopyFile:
    case OpKind::SetRegistryFirstLogon: return 0.3;
    case OpKind::SetServiceStart: return 1.0;
    case OpKind::SetPostSetup: return 5.0; // scripts; copy payloads add their own time
    case OpKind::RemoveComponent: return 25.0; // hive edit + package removal + a few thousand files
    case OpKind::CleanupImage: return 30.0;    // StartComponentCleanup /ResetBase with nothing new to clean
    case OpKind::SetEdition: return 35.0;      // lab: Home → Pro 28 s
    case OpKind::SetTaskState:
    case OpKind::SetHosts:
    case OpKind::SetDns: return 0.3;
    case OpKind::CopyTree: return 5.0;         // plus the copy itself
    case OpKind::RemoveDriver: return 4.0;
    case OpKind::AddAppx: return 30.0;
    case OpKind::SetDefaultApps: return 10.0;  // dism.exe
    case OpKind::SetIntl: return 15.0;         // dism.exe
    case OpKind::SetPicture: return 3.0;       // a few encodes (WIC)
    case OpKind::AddFont: return 0.5;
    }
    return 5.0;
}

double estimateSeconds(const ApplyPlan& plan, std::size_t step) noexcept {
    const OpKind kind = plan.steps[step].operation.kind;
    if (kind == OpKind::CleanupImage &&
        std::ranges::any_of(plan.steps, [](const PlanStep& s) { return s.operation.kind == OpKind::AddPackage; })) {
        return 600.0; // the updates of this run left superseded versions behind
    }
    return estimateSeconds(kind);
}

std::vector<PlanGroup> groups(const ApplyPlan& plan) {
    std::vector<PlanGroup> result;
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& step = plan.steps[i];
        if (result.empty() || result.back().phase != step.phase) {
            result.push_back({step.phase, i, 0, 0});
        }
        ++result.back().count;
        result.back().estimateSeconds += estimateSeconds(plan, i);
    }
    return result;
}

ApplyPlan plan(const ChangeSet& changes) {
    ApplyPlan result;
    for (const auto& op : changes.operations()) {
        result.steps.push_back({phaseOf(op), op});
    }
    // stable: keeps the user's order inside each phase; updates go SSU → language packs → LCU → .NET → other
    // (op.value holds the kind from UpdatePackage.h).
    const auto updateRank = [](const Operation& op) {
        if (op.kind != OpKind::AddPackage) {
            return 0;
        }
        return op.value == L"ssu" ? 0 : op.value == L"language" ? 1 : op.value == L"lcu" ? 2 : op.value == L"dotnet" ? 3 : 4;
    };
    std::ranges::stable_sort(result.steps, [&](const PlanStep& a, const PlanStep& b) {
        if (a.phase != b.phase) {
            return static_cast<int>(a.phase) < static_cast<int>(b.phase);
        }
        return updateRank(a.operation) < updateRank(b.operation);
    });

    const auto highRisk = std::ranges::count_if(result.steps, [](const PlanStep& s) { return s.operation.risk == Risk::High; });
    if (highRisk > 0) {
        result.warnings.push_back(std::format(L"{} high-risk operation(s): cannot be undone in the image", highRisk));
    }
    return result;
}

} // namespace wl::core::ops
