#include "core/image/AppxInstall.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/dism/DismExe.h"

#include <json.hpp>

#include <AppxPackaging.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::core {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

// Takes a CoTaskMem string.
std::wstring take(LPWSTR text) {
    std::wstring out = text ? text : L"";
    CoTaskMemFree(text);
    return out;
}

std::wstring versionText(UINT64 v) {
    return std::format(L"{}.{}.{}.{}", (v >> 48) & 0xFFFF, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF);
}

const wchar_t* archName(APPX_PACKAGE_ARCHITECTURE a) {
    switch (a) {
    case APPX_PACKAGE_ARCHITECTURE_X86: return L"x86";
    case APPX_PACKAGE_ARCHITECTURE_ARM: return L"arm";
    case APPX_PACKAGE_ARCHITECTURE_X64: return L"x64";
    case APPX_PACKAGE_ARCHITECTURE_ARM64: return L"arm64";
    default: return L"neutral";
    }
}

Error comError(HRESULT hr, std::wstring what, const std::filesystem::path& file) {
    return Error{ErrorCode::ParseError, std::move(what), file.wstring(), static_cast<std::int32_t>(hr)};
}

Result<ComPtr<IStream>> openStream(const std::filesystem::path& file) {
    ComPtr<IStream> stream;
    const HRESULT hr = SHCreateStreamOnFileEx(file.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream);
    if (FAILED(hr)) {
        return std::unexpected(Error{ErrorCode::IoError, L"cannot open the package", file.wstring(), static_cast<std::int32_t>(hr)});
    }
    return stream;
}

// Name, publisher, version, architecture, display name, framework flag, dependencies of one
// package manifest (an .appx / .msix or a payload of a bundle).
void readManifest(IAppxManifestReader* manifest, AppxPackageInfo& info, bool identity) {
    if (identity) {
        ComPtr<IAppxManifestPackageId> id;
        if (SUCCEEDED(manifest->GetPackageId(&id))) {
            LPWSTR text = nullptr;
            if (SUCCEEDED(id->GetName(&text))) {
                info.name = take(text);
            }
            if (SUCCEEDED(id->GetPublisher(&text))) {
                info.publisher = take(text);
            }
            UINT64 version = 0;
            if (SUCCEEDED(id->GetVersion(&version))) {
                info.version = versionText(version);
            }
        }
    }
    ComPtr<IAppxManifestPackageId> id;
    if (SUCCEEDED(manifest->GetPackageId(&id))) {
        APPX_PACKAGE_ARCHITECTURE arch{};
        if (SUCCEEDED(id->GetArchitecture(&arch))) {
            const std::wstring a = archName(arch);
            if (std::ranges::find(info.architectures, a) == info.architectures.end()) {
                info.architectures.push_back(a);
            }
        }
    }
    ComPtr<IAppxManifestProperties> props;
    if (SUCCEEDED(manifest->GetProperties(&props))) {
        LPWSTR text = nullptr;
        if (info.displayName.empty() && SUCCEEDED(props->GetStringValue(L"DisplayName", &text))) {
            std::wstring name = take(text);
            if (!name.starts_with(L"ms-resource:")) {
                info.displayName = std::move(name);
            }
        }
        if (info.publisherDisplay.empty() && SUCCEEDED(props->GetStringValue(L"PublisherDisplayName", &text))) {
            std::wstring name = take(text);
            if (!name.starts_with(L"ms-resource:")) {
                info.publisherDisplay = std::move(name);
            }
        }
        BOOL framework = FALSE;
        if (SUCCEEDED(props->GetBoolValue(L"Framework", &framework)) && framework) {
            info.framework = true;
        }
    }
    ComPtr<IAppxManifestPackageDependenciesEnumerator> deps;
    if (SUCCEEDED(manifest->GetPackageDependencies(&deps))) {
        BOOL has = FALSE;
        while (SUCCEEDED(deps->GetHasCurrent(&has)) && has) {
            ComPtr<IAppxManifestPackageDependency> dep;
            if (SUCCEEDED(deps->GetCurrent(&dep))) {
                LPWSTR name = nullptr;
                UINT64 min = 0;
                AppxPackageInfo::Dependency d;
                if (SUCCEEDED(dep->GetName(&name))) {
                    d.name = take(name);
                }
                if (SUCCEEDED(dep->GetMinVersion(&min))) {
                    d.minVersion = versionText(min);
                }
                if (!d.name.empty() && std::ranges::none_of(info.dependencies, [&](const auto& x) { return x.name == d.name; })) {
                    info.dependencies.push_back(std::move(d));
                }
            }
            deps->MoveNext(&has);
        }
    }
}

Result<AppxPackageInfo> readPlain(const std::filesystem::path& file) {
    ComPtr<IAppxFactory> factory;
    HRESULT hr = CoCreateInstance(__uuidof(AppxFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return std::unexpected(comError(hr, L"the Windows packaging API is not available", file));
    }
    auto stream = openStream(file);
    if (!stream) {
        return std::unexpected(stream.error());
    }
    ComPtr<IAppxPackageReader> reader;
    if (hr = factory->CreatePackageReader(stream->Get(), &reader); FAILED(hr)) {
        return std::unexpected(comError(hr, L"not a valid app package", file));
    }
    ComPtr<IAppxManifestReader> manifest;
    if (hr = reader->GetManifest(&manifest); FAILED(hr)) {
        return std::unexpected(comError(hr, L"the package has no readable manifest", file));
    }
    AppxPackageInfo info;
    info.path = file;
    readManifest(manifest.Get(), info, true);
    return info;
}

Result<AppxPackageInfo> readBundle(const std::filesystem::path& file) {
    ComPtr<IAppxBundleFactory> bundleFactory;
    HRESULT hr = CoCreateInstance(__uuidof(AppxBundleFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&bundleFactory));
    ComPtr<IAppxFactory> factory;
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(__uuidof(AppxFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    if (FAILED(hr)) {
        return std::unexpected(comError(hr, L"the Windows packaging API is not available", file));
    }
    auto stream = openStream(file);
    if (!stream) {
        return std::unexpected(stream.error());
    }
    ComPtr<IAppxBundleReader> reader;
    if (hr = bundleFactory->CreateBundleReader(stream->Get(), &reader); FAILED(hr)) {
        return std::unexpected(comError(hr, L"not a valid app bundle", file));
    }
    ComPtr<IAppxBundleManifestReader> manifest;
    if (hr = reader->GetManifest(&manifest); FAILED(hr)) {
        return std::unexpected(comError(hr, L"the bundle has no readable manifest", file));
    }
    AppxPackageInfo info;
    info.path = file;
    info.bundle = true;
    ComPtr<IAppxManifestPackageId> id;
    if (SUCCEEDED(manifest->GetPackageId(&id))) {
        LPWSTR text = nullptr;
        if (SUCCEEDED(id->GetName(&text))) {
            info.name = take(text);
        }
        if (SUCCEEDED(id->GetPublisher(&text))) {
            info.publisher = take(text);
        }
        UINT64 version = 0;
        if (SUCCEEDED(id->GetVersion(&version))) {
            info.version = versionText(version);
        }
    }
    // The application payloads carry the architectures, the display name and the dependencies.
    ComPtr<IAppxBundleManifestPackageInfoEnumerator> items;
    if (SUCCEEDED(manifest->GetPackageInfoItems(&items))) {
        BOOL has = FALSE;
        while (SUCCEEDED(items->GetHasCurrent(&has)) && has) {
            ComPtr<IAppxBundleManifestPackageInfo> item;
            APPX_BUNDLE_PAYLOAD_PACKAGE_TYPE type{};
            LPWSTR name = nullptr;
            if (SUCCEEDED(items->GetCurrent(&item)) && SUCCEEDED(item->GetPackageType(&type)) &&
                type == APPX_BUNDLE_PAYLOAD_PACKAGE_TYPE_APPLICATION && SUCCEEDED(item->GetFileName(&name))) {
                const std::wstring fileName = take(name);
                ComPtr<IAppxFile> payload;
                ComPtr<IStream> payloadStream;
                ComPtr<IAppxPackageReader> payloadReader;
                ComPtr<IAppxManifestReader> payloadManifest;
                if (SUCCEEDED(reader->GetPayloadPackage(fileName.c_str(), &payload)) &&
                    SUCCEEDED(payload->GetStream(&payloadStream)) &&
                    SUCCEEDED(factory->CreatePackageReader(payloadStream.Get(), &payloadReader)) &&
                    SUCCEEDED(payloadReader->GetManifest(&payloadManifest))) {
                    readManifest(payloadManifest.Get(), info, false);
                }
            }
            items->MoveNext(&has);
        }
    }
    return info;
}

// Frameworks an image of `arch` can take.
bool archFits(std::wstring_view packageArch, std::wstring_view imageArch) {
    if (packageArch == L"neutral" || packageArch == imageArch) {
        return true;
    }
    if (imageArch == L"x64") {
        return packageArch == L"x86";
    }
    if (imageArch == L"arm64") {
        return packageArch == L"arm" || packageArch == L"x64" || packageArch == L"x86";
    }
    return false;
}

std::size_t commonPrefix(std::wstring_view a, std::wstring_view b) {
    std::size_t n = 0;
    while (n < a.size() && n < b.size() && std::towlower(a[n]) == std::towlower(b[n])) {
        ++n;
    }
    return n;
}

} // namespace

bool isAppxFile(const std::filesystem::path& file) {
    const std::wstring ext = lower(file.extension().wstring());
    return ext == L".appx" || ext == L".msix" || ext == L".appxbundle" || ext == L".msixbundle";
}

Result<AppxPackageInfo> readAppxPackage(const std::filesystem::path& file) {
    // The engine threads have COM; a tool's main thread may not (a thread in another apartment
    // mode keeps it and the call still works).
    struct ComScope {
        bool owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ~ComScope() {
            if (owned) {
                CoUninitialize();
            }
        }
    } com;
    const std::wstring ext = lower(file.extension().wstring());
    if (!isAppxFile(file)) {
        return fail(ErrorCode::InvalidArgument, L"not an app package (.appx, .msix or a bundle of them)", file.wstring());
    }
    // The name may lie: winget saves a bundle as "….msix". Try the other kind when the first fails.
    const bool bundleName = ext == L".appxbundle" || ext == L".msixbundle";
    auto first = bundleName ? readBundle(file) : readPlain(file);
    if (first || first.error().code == ErrorCode::IoError) {
        return first;
    }
    auto second = bundleName ? readPlain(file) : readBundle(file);
    return second ? second : first;
}

Result<AppxInstall> planAppxInstall(const std::filesystem::path& package, std::wstring_view imageArchitecture) {
    auto info = readAppxPackage(package);
    if (!info) {
        return std::unexpected(info.error());
    }
    AppxInstall install;
    install.package = package;
    install.name = info->name;
    install.displayName = info->displayName;
    install.publisher = info->publisherDisplay.empty() ? info->publisher : info->publisherDisplay;
    install.version = info->version;
    install.architectures = info->architectures;
    install.framework = info->framework;
    const std::wstring arch = lower(imageArchitecture.empty() ? L"x64" : imageArchitecture);
    // Candidates: packages next to it and in a "Dependencies" folder (both levels).
    std::vector<std::filesystem::path> candidates;
    std::vector<std::filesystem::path> licenses;
    std::error_code ec;
    const auto folder = package.parent_path();
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::end(it);
         it.increment(ec)) {
        if (it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || it->path() == package) {
            continue;
        }
        if (isAppxFile(it->path())) {
            candidates.push_back(it->path());
        } else if (lower(it->path().extension().wstring()) == L".xml" &&
                   lower(it->path().filename().wstring()).find(L"license") != std::wstring::npos) {
            licenses.push_back(it->path());
        }
    }
    for (const auto& dep : info->dependencies) {
        bool found = false;
        for (const auto& candidate : candidates) {
            auto other = readAppxPackage(candidate);
            if (!other || _wcsicmp(other->name.c_str(), dep.name.c_str()) != 0) {
                continue;
            }
            if (std::ranges::any_of(other->architectures, [&](const std::wstring& a) { return archFits(a, arch); })) {
                install.dependencies.push_back(candidate);
                found = true;
            }
        }
        if (!found) {
            install.missing.push_back(dep.name);
        }
    }
    if (!licenses.empty()) {
        const std::wstring stem = package.stem().wstring();
        install.license = *std::ranges::max_element(licenses, {}, [&](const std::filesystem::path& l) {
            return commonPrefix(l.filename().wstring(), stem);
        });
    }
    return install;
}

std::string appxInstallToJson(const AppxInstall& install) {
    nlohmann::json j = nlohmann::json::object();
    j["dependencies"] = nlohmann::json::array();
    for (const auto& d : install.dependencies) {
        j["dependencies"].push_back(utf8::fromWide(d.wstring()));
    }
    if (!install.license.empty()) {
        j["license"] = utf8::fromWide(install.license.wstring());
    }
    j["missing"] = nlohmann::json::array();
    for (const auto& m : install.missing) {
        j["missing"].push_back(utf8::fromWide(m));
    }
    j["name"] = utf8::fromWide(install.name);
    j["displayName"] = utf8::fromWide(install.displayName);
    j["publisher"] = utf8::fromWide(install.publisher);
    j["version"] = utf8::fromWide(install.version);
    j["architectures"] = nlohmann::json::array();
    for (const auto& a : install.architectures) {
        j["architectures"].push_back(utf8::fromWide(a));
    }
    j["framework"] = install.framework;
    return j.dump();
}

Result<AppxInstall> appxInstallFromJson(const std::filesystem::path& package, std::string_view json) {
    const auto j = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return fail(ErrorCode::ParseError, L"app package details are not a JSON object", package.wstring());
    }
    AppxInstall install;
    install.package = package;
    for (const auto& d : j.value("dependencies", nlohmann::json::array())) {
        if (d.is_string()) {
            install.dependencies.emplace_back(utf8::toWide(d.get<std::string>()));
        }
    }
    install.license = utf8::toWide(j.value("license", std::string{}));
    for (const auto& m : j.value("missing", nlohmann::json::array())) {
        if (m.is_string()) {
            install.missing.push_back(utf8::toWide(m.get<std::string>()));
        }
    }
    install.name = utf8::toWide(j.value("name", std::string{}));
    install.displayName = utf8::toWide(j.value("displayName", std::string{}));
    install.publisher = utf8::toWide(j.value("publisher", std::string{}));
    install.version = utf8::toWide(j.value("version", std::string{}));
    for (const auto& a : j.value("architectures", nlohmann::json::array())) {
        if (a.is_string()) {
            install.architectures.push_back(utf8::toWide(a.get<std::string>()));
        }
    }
    install.framework = j.value("framework", false);
    return install;
}

std::wstring appxArguments(const AppxInstall& install) {
    std::wstring args = std::format(L"/Add-ProvisionedAppxPackage /PackagePath:\"{}\"", install.package.wstring());
    for (const auto& d : install.dependencies) {
        args += std::format(L" /DependencyPackagePath:\"{}\"", d.wstring());
    }
    args += install.license.empty() ? std::wstring(L" /SkipLicense")
                                    : std::format(L" /LicensePath:\"{}\"", install.license.wstring());
    args += L" /Region:all";
    return args;
}

Result<void> provisionAppx(DismSession& session, const AppxInstall& install, const TaskContext& task) {
    std::error_code ec;
    for (const auto& file : [&] {
             std::vector<std::filesystem::path> all{install.package};
             all.insert(all.end(), install.dependencies.begin(), install.dependencies.end());
             return all;
         }()) {
        if (!std::filesystem::is_regular_file(file, ec)) {
            return fail(ErrorCode::NotFound, L"the app package is not there any more", file.wstring());
        }
    }
    task.report(-1.0, L"appx");
    const auto run = runDismExe(session, appxArguments(install), [&](double f) { task.report(f, L"appx"); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismExeFailure(*run, L"the app could not be provisioned"));
    }
    log::info("appx", std::format(L"provisioned {} ({} dependencies, {})", install.package.filename().wstring(),
                                  install.dependencies.size(), install.license.empty() ? L"no licence" : L"licence"));
    return {};
}

} // namespace wl::core
