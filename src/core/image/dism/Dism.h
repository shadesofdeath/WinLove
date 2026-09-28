#pragma once
// DismApiBackend (docs/ENGINE.md §1): mount/unmount and servicing queries over dismapi.dll.
// Rules: call only from the engine thread (TaskRunner); requires an elevated process — every
// entry point returns ErrorCode::AccessDenied up front when not elevated, with a clear message.
#include "core/tasks/Task.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wl::core {

enum class ServicingState : std::uint8_t {
    NotPresent,
    UninstallPending,
    Staged,
    Removed,
    Installed,
    InstallPending,
    Superseded,
    PartiallyInstalled,
};
[[nodiscard]] const wchar_t* servicingStateName(ServicingState state) noexcept;

// DismMountStatus as DISM reports it. Our own, richer view is MountState (MountHealth.h).
enum class DismMountStatus : std::uint8_t { Ok, NeedsRemount, Invalid };
[[nodiscard]] const wchar_t* dismMountStatusName(DismMountStatus status) noexcept;

struct MountInfo {
    std::filesystem::path mountPath;
    std::filesystem::path imagePath;
    int index = 0;
    bool readOnly = false;
    DismMountStatus status = DismMountStatus::Ok;
};

struct PackageEntry {
    std::wstring name;
    ServicingState state = ServicingState::NotPresent;
    int releaseType = 0;
};

struct FeatureEntry {
    std::wstring name;
    ServicingState state = ServicingState::NotPresent;
};

struct CapabilityEntry {
    std::wstring name;
    ServicingState state = ServicingState::NotPresent;
};

struct FeatureDetail {
    std::wstring name;
    ServicingState state = ServicingState::NotPresent;
    std::wstring displayName;  // localized by DISM from the image ("Windows Sandbox")
    std::wstring description;
    bool restartRequired = false;
};

struct CapabilityDetail {
    std::wstring name;         // "OpenSSH.Client~~~~0.0.1.0"
    ServicingState state = ServicingState::NotPresent;
    std::wstring displayName;
    std::wstring description;
    std::uint32_t downloadSize = 0; // bytes
    std::uint32_t installSize = 0;  // bytes
};

// A provisioned AppX package (installed for every new user of the image).
struct AppxEntry {
    std::wstring packageName;  // "Microsoft.BingWeather_4.53.51922.0_neutral_~_8wekyb3d8bbwe"
    std::wstring displayName;  // "Microsoft.BingWeather" (the package identity name, not a UI string)
    std::wstring publisherId;  // "8wekyb3d8bbwe"
    std::wstring version;
    std::uint32_t architecture = 0; // 0 x86, 5 ARM, 9 x64, 11 neutral, 12 ARM64
    std::wstring installLocation;
};

class DismSession;

class Dism {
public:
    // Loads dismapi.dll and calls DismInitialize once (log into the WinLove log folder).
    [[nodiscard]] static Result<Dism*> instance();
    ~Dism();

    [[nodiscard]] Result<void> mount(const std::filesystem::path& wim, int index, const std::filesystem::path& mountDir,
                                     bool readOnly, const TaskContext& task);
    [[nodiscard]] Result<void> unmount(const std::filesystem::path& mountDir, bool commit, const TaskContext& task);
    [[nodiscard]] Result<std::vector<MountInfo>> mounts();
    // Reattaches a mount left in "needs remount" state (e.g. after a reboot).
    [[nodiscard]] Result<void> remount(const std::filesystem::path& mountDir);
    [[nodiscard]] Result<void> cleanupMountpoints();
    [[nodiscard]] Result<std::unique_ptr<DismSession>> openSession(const std::filesystem::path& mountDir);

private:
    friend class DismSession;
    Dism() = default;
    [[nodiscard]] Error error(HRESULT hr, std::wstring context) const; // HRESULT + DISM's last message

    HMODULE m_module = nullptr;
    struct Api;
    std::unique_ptr<Api> m_api;
};

// An open servicing session on a mounted image (DismOpenSession). Close before unmounting.
class DismSession {
public:
    ~DismSession();
    [[nodiscard]] Result<std::vector<PackageEntry>> packages();
    [[nodiscard]] Result<std::vector<FeatureEntry>> features();
    [[nodiscard]] Result<std::vector<CapabilityEntry>> capabilities();
    // Per-item details (one DISM call each: ~10–50 ms). Display names come from the image.
    [[nodiscard]] Result<FeatureDetail> featureInfo(const std::wstring& name);
    [[nodiscard]] Result<CapabilityDetail> capabilityInfo(const std::wstring& name);
    [[nodiscard]] Result<std::vector<AppxEntry>> appxPackages();

    // Servicing mutations — called by ops::Applier only (never directly from UI code).
    [[nodiscard]] Result<void> disableFeature(const std::wstring& name, const TaskContext& task);
    // `sources`: folders with the feature payload (e.g. the setup media's sources\sxs for
    // NetFx3 or a feature whose payload was removed). Windows Update is never used (LimitAccess).
    [[nodiscard]] Result<void> enableFeature(const std::wstring& name, const TaskContext& task,
                                             const std::vector<std::filesystem::path>& sources = {});
    [[nodiscard]] Result<void> removePackage(const std::wstring& name, const TaskContext& task);
    [[nodiscard]] Result<void> removeCapability(const std::wstring& name, const TaskContext& task);
    // DismRemoveProvisionedAppxPackage: new users no longer get the app (no progress / cancel in the API).
    [[nodiscard]] Result<void> removeAppx(const std::wstring& packageName);
    // DismAddPackage: .msu / .cab (servicing stack, cumulative, .NET, language packs…).
    [[nodiscard]] Result<void> addPackage(const std::filesystem::path& package, const TaskContext& task);
    // DismAddDriver: one .inf into the driver store of the image (unsigned only with forceUnsigned).
    [[nodiscard]] Result<void> addDriver(const std::filesystem::path& inf, bool forceUnsigned = false);

private:
    friend class Dism;
    DismSession(Dism& dism, unsigned session, std::filesystem::path path)
        : m_dism(dism), m_session(session), m_path(std::move(path)) {}
    Dism& m_dism;
    unsigned m_session;
    std::filesystem::path m_path;
};

} // namespace wl::core
