#include "core/image/dism/OptionalFeatures.h"

#include "base/Log.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace wl::core {

bool OptionalFeature::isOn() const noexcept {
    return state == ServicingState::Installed || state == ServicingState::InstallPending ||
           state == ServicingState::PartiallyInstalled;
}

Result<std::vector<OptionalFeature>> readOptionalFeatures(Dism& dism, const std::filesystem::path& mountDir,
                                                          const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    auto session = dism.openSession(mountDir);
    if (!session) {
        return std::unexpected(session.error());
    }
    auto features = (*session)->features();
    if (!features) {
        return std::unexpected(features.error());
    }
    auto capabilities = (*session)->capabilities();
    if (!capabilities) {
        return std::unexpected(capabilities.error());
    }
    std::erase_if(*capabilities, [](const CapabilityEntry& c) {
        return c.state == ServicingState::NotPresent || c.state == ServicingState::Removed;
    });

    const double total = static_cast<double>(features->size() + capabilities->size());
    double done = 0;
    std::vector<OptionalFeature> result;
    result.reserve(features->size() + capabilities->size());
    for (const auto& f : *features) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"reading features cancelled", mountDir.wstring());
        }
        OptionalFeature item{OptionalFeature::Kind::Feature, f.name, f.name, {}, f.state, 0, false};
        if (auto info = (*session)->featureInfo(f.name)) {
            if (!info->displayName.empty()) {
                item.displayName = info->displayName;
            }
            item.description = info->description;
            item.restartRequired = info->restartRequired;
        }
        result.push_back(std::move(item));
        task.report(++done / total, L"features");
    }
    for (const auto& c : *capabilities) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"reading features cancelled", mountDir.wstring());
        }
        OptionalFeature item{OptionalFeature::Kind::Capability, c.name, c.name, {}, c.state, 0, false};
        if (auto info = (*session)->capabilityInfo(c.name)) {
            if (!info->displayName.empty()) {
                item.displayName = info->displayName;
            }
            item.description = info->description;
            item.size = info->installSize;
        }
        result.push_back(std::move(item));
        task.report(++done / total, L"capabilities");
    }
    std::ranges::sort(result, [](const OptionalFeature& a, const OptionalFeature& b) {
        return _wcsicmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
    });
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    log::info("dism", std::format(L"{} features + {} capabilities read in {} ms", features->size(),
                                  capabilities->size(), ms));
    return result;
}

} // namespace wl::core
