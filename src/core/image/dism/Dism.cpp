#include "core/image/dism/Dism.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/dism/DismApi.h"
#include "core/system/Privileges.h"

#include <format>
#include <mutex>

namespace wl::core {

struct Dism::Api : dismapi::Api {};

namespace {

constexpr auto kCode = ErrorCode::DismFailure;

ServicingState stateFrom(dismapi::PackageFeatureState state) {
    return static_cast<ServicingState>(static_cast<int>(state));
}

// DISM progress → TaskContext (Current/Total are "units", usually 0..100).
struct ProgressBridge {
    const TaskContext* task;
    const wchar_t* stage;
};
void CALLBACK onProgress(UINT current, UINT total, PVOID user) {
    const auto* bridge = static_cast<const ProgressBridge*>(user);
    if (bridge && bridge->task && total > 0) {
        bridge->task->report(static_cast<double>(current) / static_cast<double>(total), bridge->stage);
    }
}

std::filesystem::path scratchDirectory() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return std::filesystem::path(temp) / L"WinLove" / L"scratch";
}

template <class F>
bool load(HMODULE module, const char* name, F& target) {
    target = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    return target != nullptr;
}

} // namespace

const wchar_t* servicingStateName(ServicingState state) noexcept {
    switch (state) {
    case ServicingState::NotPresent: return L"NotPresent";
    case ServicingState::UninstallPending: return L"UninstallPending";
    case ServicingState::Staged: return L"Staged";
    case ServicingState::Removed: return L"Removed";
    case ServicingState::Installed: return L"Installed";
    case ServicingState::InstallPending: return L"InstallPending";
    case ServicingState::Superseded: return L"Superseded";
    case ServicingState::PartiallyInstalled: return L"PartiallyInstalled";
    }
    return L"?";
}

Result<Dism*> Dism::instance() {
    if (!isElevated()) {
        return fail(ErrorCode::AccessDenied, L"DISM needs an elevated (administrator) process", L"Dism");
    }
    static std::mutex mutex;
    static std::unique_ptr<Dism> dism;
    std::scoped_lock lock(mutex);
    if (dism) {
        return dism.get();
    }
    auto created = std::unique_ptr<Dism>(new Dism());
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const auto dllPath = std::filesystem::path(system) / L"dismapi.dll";
    created->m_module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!created->m_module) {
        return fail(ErrorCode::NotFound, L"dismapi.dll not found", dllPath.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    created->m_api = std::make_unique<Api>();
    auto& a = *created->m_api;
    const HMODULE m = created->m_module;
    const bool ok = load(m, "DismInitialize", a.initialize) && load(m, "DismShutdown", a.shutdown) &&
                    load(m, "DismMountImage", a.mountImage) && load(m, "DismUnmountImage", a.unmountImage) &&
                    load(m, "DismOpenSession", a.openSession) && load(m, "DismCloseSession", a.closeSession) &&
                    load(m, "DismGetLastErrorMessage", a.getLastErrorMessage) &&
                    load(m, "DismGetMountedImageInfo", a.getMountedImageInfo) &&
                    load(m, "DismCleanupMountpoints", a.cleanupMountpoints) && load(m, "DismRemountImage", a.remountImage) && load(m, "DismDelete", a.deleteStructure) &&
                    load(m, "DismGetPackages", a.getPackages) && load(m, "DismGetFeatures", a.getFeatures) &&
                    load(m, "DismGetCapabilities", a.getCapabilities) && load(m, "DismGetFeatureInfo", a.getFeatureInfo) &&
                    load(m, "DismGetCapabilityInfo", a.getCapabilityInfo) &&
                    load(m, "DismDisableFeature", a.disableFeature) && load(m, "DismEnableFeature", a.enableFeature) &&
                    load(m, "DismRemovePackage", a.removePackage) &&
                    load(m, "DismRemoveCapability", a.removeCapability);
    if (!ok) {
        return fail(ErrorCode::Unsupported, L"dismapi.dll is missing expected entry points", dllPath.wstring());
    }

    std::error_code ec;
    const auto scratch = scratchDirectory();
    std::filesystem::create_directories(scratch, ec);
    // DISM will not create the log's folder: DismInitialize then fails with LOGGING_DISABLED
    // (ENGINE.md saha notu 2026-09-28).
    std::filesystem::create_directories(log::defaultDirectory(), ec);
    const auto logFile = log::defaultDirectory() / L"dism.log";
    constexpr HRESULT kLoggingDisabled = static_cast<HRESULT>(0xC0040009); // DISMAPI_E_LOGGING_DISABLED
    const HRESULT hr = a.initialize(dismapi::LogErrorsWarningsInfo, logFile.c_str(), scratch.c_str());
    if (hr == kLoggingDisabled) {
        log::warn("dism", std::format(L"DISM logging disabled (cannot write {}); continuing", logFile.wstring()));
    } else if (FAILED(hr)) {
        return fail(kCode, L"DismInitialize failed", logFile.wstring(), hr);
    }
    log::info("dism", std::format(L"DISM API initialized (log: {})", logFile.wstring()));
    dism = std::move(created);
    return dism.get();
}

Dism::~Dism() {
    if (m_api && m_api->shutdown) {
        m_api->shutdown();
    }
    if (m_module) {
        FreeLibrary(m_module);
    }
}

Error Dism::error(HRESULT hr, std::wstring context) const {
    std::wstring message = L"DISM error";
    dismapi::String* last = nullptr;
    if (SUCCEEDED(m_api->getLastErrorMessage(&last)) && last && last->value) {
        message = last->value;
        while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' ')) {
            message.pop_back();
        }
        m_api->deleteStructure(last);
    }
    log::error("dism", std::format(L"{} ({}) [0x{:08X}]", message, context, static_cast<unsigned>(hr)));
    return Error{kCode, std::move(message), std::move(context), static_cast<std::int32_t>(hr)};
}

Result<void> Dism::mount(const std::filesystem::path& wimInput, int index, const std::filesystem::path& mountDirInput,
                         bool readOnly, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const std::filesystem::path mountDir = nativePath(mountDirInput);
    std::error_code ec;
    std::filesystem::create_directories(mountDir, ec);
    if (!std::filesystem::is_empty(mountDir, ec)) {
        return fail(ErrorCode::InvalidArgument, L"mount directory must be empty", mountDir.wstring());
    }
    log::info("dism", std::format(L"mount {} index {} -> {}{}", wim.wstring(), index, mountDir.wstring(),
                                  readOnly ? L" (read-only)" : L""));
    ProgressBridge bridge{&task, L"mount"};
    const HRESULT hr = m_api->mountImage(wim.c_str(), mountDir.c_str(), static_cast<UINT>(index), nullptr,
                                         dismapi::ImageIndex, readOnly ? dismapi::kMountReadOnly : dismapi::kMountReadWrite,
                                         task.cancel.event(), &onProgress, &bridge);
    if (FAILED(hr)) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"mount cancelled", mountDir.wstring(), hr);
        }
        return std::unexpected(error(hr, L"mount " + wim.wstring()));
    }
    return {};
}

Result<void> Dism::unmount(const std::filesystem::path& mountDirInput, bool commit, const TaskContext& task) {
    const std::filesystem::path mountDir = nativePath(mountDirInput);
    log::info("dism", std::format(L"unmount {} ({})", mountDir.wstring(), commit ? L"commit" : L"discard"));
    ProgressBridge bridge{&task, commit ? L"commit" : L"discard"};
    const HRESULT hr = m_api->unmountImage(mountDir.c_str(), commit ? dismapi::kCommitImage : dismapi::kDiscardImage,
                                           task.cancel.event(), &onProgress, &bridge);
    if (FAILED(hr)) {
        return std::unexpected(error(hr, L"unmount " + mountDir.wstring()));
    }
    return {};
}

Result<std::vector<MountInfo>> Dism::mounts() {
    dismapi::MountedImageInfo* info = nullptr;
    UINT count = 0;
    const HRESULT hr = m_api->getMountedImageInfo(&info, &count);
    if (FAILED(hr)) {
        return std::unexpected(error(hr, L"list mounts"));
    }
    std::vector<MountInfo> result;
    for (UINT i = 0; i < count; ++i) {
        const auto& m = info[i];
        result.push_back({m.mountPath ? m.mountPath : L"", m.imageFilePath ? m.imageFilePath : L"",
                          static_cast<int>(m.imageIndex), m.mountMode == dismapi::ReadOnly,
                          m.mountStatus == dismapi::MountOk             ? DismMountStatus::Ok
                          : m.mountStatus == dismapi::MountNeedsRemount ? DismMountStatus::NeedsRemount
                                                                        : DismMountStatus::Invalid});
    }
    if (info) {
        m_api->deleteStructure(info);
    }
    return result;
}

const wchar_t* dismMountStatusName(DismMountStatus status) noexcept {
    switch (status) {
    case DismMountStatus::Ok: return L"ok";
    case DismMountStatus::NeedsRemount: return L"needs remount";
    case DismMountStatus::Invalid: return L"invalid";
    }
    return L"?";
}

Result<void> Dism::remount(const std::filesystem::path& mountDirInput) {
    const std::filesystem::path mountDir = nativePath(mountDirInput);
    log::info("dism", L"remount " + mountDir.wstring());
    const HRESULT hr = m_api->remountImage(mountDir.c_str());
    if (FAILED(hr)) {
        return std::unexpected(error(hr, L"remount " + mountDir.wstring()));
    }
    return {};
}

Result<void> Dism::cleanupMountpoints() {
    log::info("dism", L"cleanup mount points");
    const HRESULT hr = m_api->cleanupMountpoints();
    if (FAILED(hr)) {
        return std::unexpected(error(hr, L"cleanup mount points"));
    }
    return {};
}

Result<std::unique_ptr<DismSession>> Dism::openSession(const std::filesystem::path& mountDirInput) {
    const std::filesystem::path mountDir = nativePath(mountDirInput);
    dismapi::Session session = 0;
    const HRESULT hr = m_api->openSession(mountDir.c_str(), nullptr, nullptr, &session);
    if (FAILED(hr)) {
        return std::unexpected(error(hr, L"open session " + mountDir.wstring()));
    }
    return std::unique_ptr<DismSession>(new DismSession(*this, session, mountDir));
}

DismSession::~DismSession() {
    m_dism.m_api->closeSession(m_session);
}

Result<std::vector<PackageEntry>> DismSession::packages() {
    dismapi::Package* list = nullptr;
    UINT count = 0;
    const HRESULT hr = m_dism.m_api->getPackages(m_session, &list, &count);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"get packages " + m_path.wstring()));
    }
    std::vector<PackageEntry> result;
    result.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        result.push_back({list[i].packageName ? list[i].packageName : L"", stateFrom(list[i].state),
                          static_cast<int>(list[i].releaseType)});
    }
    m_dism.m_api->deleteStructure(list);
    return result;
}

Result<std::vector<FeatureEntry>> DismSession::features() {
    dismapi::Feature* list = nullptr;
    UINT count = 0;
    const HRESULT hr = m_dism.m_api->getFeatures(m_session, nullptr, dismapi::PackageNone, &list, &count);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"get features " + m_path.wstring()));
    }
    std::vector<FeatureEntry> result;
    result.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        result.push_back({list[i].featureName ? list[i].featureName : L"", stateFrom(list[i].state)});
    }
    m_dism.m_api->deleteStructure(list);
    return result;
}

Result<std::vector<CapabilityEntry>> DismSession::capabilities() {
    dismapi::Capability* list = nullptr;
    UINT count = 0;
    const HRESULT hr = m_dism.m_api->getCapabilities(m_session, &list, &count);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"get capabilities " + m_path.wstring()));
    }
    std::vector<CapabilityEntry> result;
    result.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        result.push_back({list[i].name ? list[i].name : L"", stateFrom(list[i].state)});
    }
    m_dism.m_api->deleteStructure(list);
    return result;
}

Result<FeatureDetail> DismSession::featureInfo(const std::wstring& name) {
    dismapi::FeatureInfo* info = nullptr;
    const HRESULT hr = m_dism.m_api->getFeatureInfo(m_session, name.c_str(), nullptr, dismapi::PackageNone, &info);
    if (FAILED(hr) || !info) {
        return std::unexpected(m_dism.error(hr, L"feature info " + name));
    }
    FeatureDetail detail{name, stateFrom(info->featureState), info->displayName ? info->displayName : L"",
                         info->description ? info->description : L"", info->restartRequired != dismapi::RestartNo};
    m_dism.m_api->deleteStructure(info);
    return detail;
}

Result<CapabilityDetail> DismSession::capabilityInfo(const std::wstring& name) {
    dismapi::CapabilityDetail* info = nullptr;
    const HRESULT hr = m_dism.m_api->getCapabilityInfo(m_session, name.c_str(), &info);
    if (FAILED(hr) || !info) {
        return std::unexpected(m_dism.error(hr, L"capability info " + name));
    }
    CapabilityDetail detail{name, stateFrom(info->state), info->displayName ? info->displayName : L"",
                            info->description ? info->description : L"", info->downloadSize, info->installSize};
    m_dism.m_api->deleteStructure(info);
    return detail;
}

Result<void> DismSession::disableFeature(const std::wstring& name, const TaskContext& task) {
    ProgressBridge bridge{&task, name.c_str()};
    const HRESULT hr = m_dism.m_api->disableFeature(m_session, name.c_str(), nullptr, FALSE, task.cancel.event(),
                                                    &onProgress, &bridge);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"disable feature " + name));
    }
    return {};
}

Result<void> DismSession::enableFeature(const std::wstring& name, const TaskContext& task) {
    ProgressBridge bridge{&task, name.c_str()};
    // LimitAccess: never reach out to Windows Update from an offline image; EnableAll: parent features too.
    const HRESULT hr = m_dism.m_api->enableFeature(m_session, name.c_str(), nullptr, dismapi::PackageNone, TRUE,
                                                   nullptr, 0, TRUE, task.cancel.event(), &onProgress, &bridge);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"enable feature " + name));
    }
    return {};
}

Result<void> DismSession::removePackage(const std::wstring& name, const TaskContext& task) {
    ProgressBridge bridge{&task, name.c_str()};
    const HRESULT hr = m_dism.m_api->removePackage(m_session, name.c_str(), dismapi::PackageName, task.cancel.event(),
                                                   &onProgress, &bridge);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"remove package " + name));
    }
    return {};
}

Result<void> DismSession::removeCapability(const std::wstring& name, const TaskContext& task) {
    ProgressBridge bridge{&task, name.c_str()};
    const HRESULT hr = m_dism.m_api->removeCapability(m_session, name.c_str(), task.cancel.event(), &onProgress, &bridge);
    if (FAILED(hr)) {
        return std::unexpected(m_dism.error(hr, L"remove capability " + name));
    }
    return {};
}

} // namespace wl::core
