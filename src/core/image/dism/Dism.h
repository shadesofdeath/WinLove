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

    // Servicing mutations — called by ops::Applier only (never directly from UI code).
    [[nodiscard]] Result<void> disableFeature(const std::wstring& name, const TaskContext& task);
    [[nodiscard]] Result<void> enableFeature(const std::wstring& name, const TaskContext& task);
    [[nodiscard]] Result<void> removePackage(const std::wstring& name, const TaskContext& task);
    [[nodiscard]] Result<void> removeCapability(const std::wstring& name, const TaskContext& task);

private:
    friend class Dism;
    DismSession(Dism& dism, unsigned session, std::filesystem::path path)
        : m_dism(dism), m_session(session), m_path(std::move(path)) {}
    Dism& m_dism;
    unsigned m_session;
    std::filesystem::path m_path;
};

} // namespace wl::core
