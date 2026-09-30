#include "core/image/dism/OptionalFeatures.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/SystemComponents.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>

namespace wl::core {

bool OptionalFeature::isOn() const noexcept {
    return state == ServicingState::Installed || state == ServicingState::InstallPending ||
           state == ServicingState::PartiallyInstalled;
}

std::vector<std::wstring> permanentCapabilitiesIn(std::string_view mum) {
    std::vector<std::wstring> names;
    if (mum.find("permanence=\"permanent\"") == std::string_view::npos) {
        return names;
    }
    constexpr std::string_view tag = "<capabilityIdentity";
    constexpr std::string_view attribute = "name=\"";
    for (std::size_t at = mum.find(tag); at != std::string_view::npos; at = mum.find(tag, at + tag.size())) {
        const std::size_t end = mum.find('>', at);
        const std::size_t name = mum.find(attribute, at);
        if (end == std::string_view::npos || name == std::string_view::npos || name > end) {
            continue;
        }
        const std::size_t from = name + attribute.size();
        const std::size_t to = mum.find('"', from);
        if (to != std::string_view::npos && to < end) {
            names.push_back(utf8::toWide(mum.substr(from, to - from)));
        }
    }
    return names;
}

std::vector<std::wstring> readPermanentCapabilities(const std::filesystem::path& mountDir) {
    std::vector<std::wstring> names;
    const auto packages = readCbsPackages(mountDir);
    if (!packages) {
        log::warn("dism", L"permanent capabilities not read: " + describe(packages.error()));
        return names;
    }
    const auto folder = mountDir / L"Windows" / L"servicing" / L"Packages";
    for (const auto& package : *packages) {
        // A capability is a visible package; the hidden thousands are its parts.
        if (package.visibility != 1 || package.state < kCbsInstalled) {
            continue;
        }
        std::ifstream in(folder / (package.identity + L".mum"), std::ios::binary);
        if (!in) {
            continue;
        }
        const std::string mum((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (auto& name : permanentCapabilitiesIn(mum)) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

Result<std::vector<OptionalFeature>> readOptionalFeatures(Dism& dism, const std::filesystem::path& mountDir,
                                                          const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    // Before the session: the hive is loaded and released again while DISM is not looking.
    const std::vector<std::wstring> permanent = readPermanentCapabilities(mountDir);
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
        // "Microsoft.Windows.Sense.Client~~~~" → the identity name the manifest declares.
        const std::wstring_view identity = std::wstring_view(c.name).substr(0, c.name.find(L'~'));
        item.permanent = std::ranges::any_of(permanent, [&](const std::wstring& name) {
            return name.size() == identity.size() && _wcsnicmp(name.c_str(), identity.data(), identity.size()) == 0;
        });
        result.push_back(std::move(item));
        task.report(++done / total, L"capabilities");
    }
    std::ranges::sort(result, [](const OptionalFeature& a, const OptionalFeature& b) {
        return _wcsicmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
    });
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    log::info("dism", std::format(L"{} features + {} capabilities ({} permanent) read in {} ms", features->size(),
                                  capabilities->size(), permanent.size(), ms));
    return result;
}

} // namespace wl::core
