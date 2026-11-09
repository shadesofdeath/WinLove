#include "core/image/dism/OptionalFeatures.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/SystemComponents.h"

#include <pugixml.hpp>

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
        const auto mum = readFileBytes(folder / (package.identity + L".mum"));
        if (!mum) {
            continue;
        }
        for (auto& name : permanentCapabilitiesIn(*mum)) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

std::map<std::wstring, std::wstring> featureParentsIn(std::string_view mum) {
    std::map<std::wstring, std::wstring> parents;
    pugi::xml_document doc;
    if (!doc.load_buffer(mum.data(), mum.size())) {
        return parents;
    }
    std::map<std::wstring, std::wstring> deployerOf; // server component UniqueName -> update name
    std::vector<std::pair<std::wstring, std::wstring>> byUnique; // update -> parent UniqueName
    for (const auto& node : doc.select_nodes("//*[local-name()='update'][@name]")) {
        const pugi::xml_node update = node.node();
        const std::wstring name = utf8::toWide(update.attribute("name").value());
        if (name.empty()) {
            continue;
        }
        if (const auto detect = update.select_node(".//*[local-name()='applicable']//*[local-name()='detectUpdate']/*[local-name()='update'][@name]")) {
            const std::wstring parent = utf8::toWide(detect.node().attribute("name").value());
            if (!parent.empty() && !text::iequals(parent, name)) {
                parents.emplace(name, parent);
            }
        }
        if (const auto component = update.select_node(".//*[local-name()='ServerComponent']")) {
            const auto c = component.node();
            if (const char* unique = c.attribute("UniqueName").value(); *unique) {
                deployerOf.emplace(utf8::toWide(unique), name);
            }
            if (const char* parent = c.attribute("Parent").value(); *parent) {
                byUnique.emplace_back(name, utf8::toWide(parent));
            }
        }
    }
    for (const auto& [name, unique] : byUnique) {
        if (const auto it = deployerOf.find(unique); it != deployerOf.end() && !parents.contains(name)) {
            parents.emplace(name, it->second);
        }
    }
    return parents;
}

std::map<std::wstring, std::wstring> readFeatureParents(const std::filesystem::path& mountDir) {
    std::map<std::wstring, std::wstring> parents;
    const auto folder = mountDir / L"Windows" / L"servicing" / L"Packages";
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const auto& path = it->path();
        // Neutral manifests only ("…~amd64~~10.0…"): language packs repeat the same updates.
        const std::wstring file = path.filename().wstring();
        if (_wcsicmp(path.extension().c_str(), L".mum") != 0 || file.find(L"~~") == std::wstring::npos) {
            continue;
        }
        std::ifstream in(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("detectUpdate") == std::string::npos && text.find("ServerComponent") == std::string::npos) {
            continue;
        }
        for (auto& [name, parent] : featureParentsIn(text)) {
            parents.emplace(std::move(name), std::move(parent));
        }
    }
    return parents;
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
            return text::iequals(name, identity);
        });
        result.push_back(std::move(item));
        task.report(++done / total, L"capabilities");
    }
    std::ranges::sort(result, [](const OptionalFeature& a, const OptionalFeature& b) {
        return _wcsicmp(a.displayName.c_str(), b.displayName.c_str()) < 0;
    });
    // D-114: the tree (a parent the image does not list as a feature leaves its child a root).
    const auto parents = readFeatureParents(mountDir);
    for (auto& item : result) {
        if (item.kind != OptionalFeature::Kind::Feature) {
            continue;
        }
        if (const auto it = parents.find(item.name); it != parents.end() &&
            std::ranges::any_of(result, [&](const OptionalFeature& o) { return o.kind == OptionalFeature::Kind::Feature && o.name == it->second; })) {
            item.parent = it->second;
        }
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    log::info("dism", std::format(L"{} features + {} capabilities ({} permanent) read in {} ms", features->size(),
                                  capabilities->size(), permanent.size(), ms));
    return result;
}

} // namespace wl::core
