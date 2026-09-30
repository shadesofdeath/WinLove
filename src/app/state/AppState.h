#pragma once
// Application state shared by the shell and pages (ARCHITECTURE §4): the engine thread, settings,
// the open source, the selected edition, the mounted image and the running engine operation.
// Pages observe changes through subscribe(); all mutation happens on the UI thread (engine
// results are posted back with ui::Window::post before reaching here).
#include "app/state/AppSettings.h"
#include "app/state/RecentSources.h"
#include "base/Log.h"
#include "core/image/DriverInf.h"
#include "core/image/Source.h"
#include "core/image/dism/Appx.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/image/dism/OptionalFeatures.h"
#include "core/iso/IsoBuilder.h"
#include "core/ops/ApplyJob.h"
#include "core/ops/ChangeSet.h"
#include "core/tasks/TaskRunner.h"
#include "core/unattend/Unattend.h"

#include <functional>
#include <optional>
#include <vector>

namespace wl::app {

struct MountedImage {
    std::filesystem::path mountDir;
    std::filesystem::path imagePath; // the WIM that is mounted
    int index = 0;
    std::wstring edition;
    bool readOnly = false;
};

// One engine operation at a time (TaskRunner is single-threaded anyway); drives the progress
// strip on the Images page and the status-bar task segment.
struct EngineOperation {
    // Reading: the mounted image's lists are read for the pages (PreloadController, D-027).
    enum class Kind : std::uint8_t { Preparing, Mounting, Unmounting, Exporting, Deleting, Cleaning, Reading };
    Kind kind;
    std::wstring edition;      // "Windows 11 Pro"
    std::filesystem::path path; // mount dir, work dir or export target (shown in the strip)
    int index = 0;
    double fraction = 0;       // 0..1
    double startedMs = 0;      // ui::nowMs() at start, for the ETA
    core::CancelToken cancel;
    int stage = 0;             // Reading: 0 features, 1 apps, 2 services
};

class AppState {
public:
    enum class Change : std::uint8_t { Source, Recent, Selection, Mount, Operation, MountFolder, Queue, Features, Apply, Iso, Components, Drivers, Services, Registry, Unattend };
    using Listener = std::function<void(Change)>;

    explicit AppState(std::filesystem::path recentFile = RecentSources::defaultFile(),
                      std::filesystem::path settingsFile = AppSettings::defaultFile());
    ~AppState();
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    // DISM / WIMGAPI work, one at a time (mount, servicing, feature reads, exports).
    [[nodiscard]] core::TaskRunner& engine() noexcept { return m_engine; }
    // Quick file reads that must not wait behind a long DISM job (opening a source: ~50 ms).
    [[nodiscard]] core::TaskRunner& reader() noexcept { return m_reader; }
    [[nodiscard]] const AppSettings& settings() const noexcept { return m_settings; }
    void setIsoFolder(std::filesystem::path folder); // saved to settings.json

    [[nodiscard]] const std::optional<core::SourceInfo>& source() const noexcept { return m_source; }
    void setSource(core::SourceInfo source); // records it in the recent list; resets the selection
    void clearSource();

    [[nodiscard]] std::optional<int> selectedIndex() const noexcept { return m_selected; }
    void select(std::optional<int> index);
    [[nodiscard]] const core::ImageInfo* selectedImage() const;

    [[nodiscard]] const std::optional<MountedImage>& mounted() const noexcept { return m_mounted; }
    void setMounted(std::optional<MountedImage> mounted);

    // The change queue (D-003): nothing touches the image until "Uygula". Cleared when the
    // mounted image changes (the queue belongs to that image).
    [[nodiscard]] const core::ops::ChangeSet& changes() const noexcept { return m_changes; }
    void queue(core::ops::Operation op);
    bool unqueue(core::ops::OpKind kind, std::wstring_view target);
    void unqueueIf(const std::function<bool(const core::ops::Operation&)>& which);
    void queueMany(std::vector<core::ops::Operation> ops);
    void unqueueMany(const std::vector<std::pair<core::ops::OpKind, std::wstring>>& slots);
    // While Uygula runs the queue is frozen: edits would be lost (commit clears it) or undone
    // by the post-run cleanup. Pages call the mutators freely; they are ignored then.
    [[nodiscard]] bool queueLocked() const noexcept;

    // P04 data for the mounted image (read once per mount, FeatureController).
    struct OptionalFeatures {
        enum class Status : std::uint8_t { Loading, Ready, Failed };
        Status status = Status::Loading;
        std::filesystem::path mountDir;
        std::vector<core::OptionalFeature> items;
        Error error;
    };
    [[nodiscard]] const std::optional<OptionalFeatures>& optionalFeatures() const noexcept { return m_features; }
    void setOptionalFeatures(std::optional<OptionalFeatures> features);

    // P07 data: provisioned apps of the mounted image (ComponentController), read once per mount.
    struct AppxList {
        enum class Status : std::uint8_t { Loading, Ready, Failed };
        Status status = Status::Loading;
        std::filesystem::path mountDir;
        std::vector<core::AppxComponent> items;
        Error error;
    };
    [[nodiscard]] const std::optional<AppxList>& appxList() const noexcept { return m_appx; }
    void setAppxList(std::optional<AppxList> list);

    // P10 data: services of the mounted image's SYSTEM hive (ServiceController), read once per mount.
    struct ServiceList {
        enum class Status : std::uint8_t { Loading, Ready, Failed };
        Status status = Status::Loading;
        std::filesystem::path mountDir;
        std::vector<core::ServiceEntry> items;
        Error error;
    };
    [[nodiscard]] const std::optional<ServiceList>& serviceList() const noexcept { return m_services; }
    void setServiceList(std::optional<ServiceList> list);

    // P11: .reg files the user imported (kept across mounts; queued writes live in the ChangeSet).
    struct RegImport {
        std::filesystem::path file;
        std::vector<core::RegistryWrite> writes; // those the image can take
        std::size_t skipped = 0;                 // unsupported roots (HKLM\SAM, other users…)
    };
    [[nodiscard]] const std::vector<RegImport>& regImports() const noexcept { return m_regImports; }
    void addRegImport(RegImport import);
    void removeRegImport(std::size_t index);

    // P09: driver folders the user scanned (kept across pages and mounts) and their INFs.
    struct DriverScan {
        std::vector<std::filesystem::path> folders;
        std::vector<core::DriverInf> infs;
    };
    [[nodiscard]] const DriverScan& driverScan() const noexcept { return m_drivers; }
    void addDriverScan(const std::filesystem::path& folder, std::vector<core::DriverInf> infs);

    // P13: the answer file being edited (kept across pages and sources); written to the root of
    // the next ISO when `includeInIso` is on.
    struct Unattend {
        core::UnattendOptions options;
        bool includeInIso = false;
    };
    [[nodiscard]] const Unattend& unattend() const noexcept { return m_unattend; }
    void setUnattend(Unattend unattend);

    // P05: the running / last "Uygula" run (ApplyController). Lives until the next run.
    struct ApplyRun {
        enum class Stage : std::uint8_t { Running, Committing, Done };
        Stage stage = Stage::Running;
        core::ops::ChangeSet changes;          // what was applied (for "Presete kaydet")
        core::ops::ApplyPlan plan;
        std::vector<core::ops::PlanGroup> groups;
        std::vector<int> stepState;            // per plan step: 0 pending, 1 running, 2 ok, 3 failed
        int currentStep = -1;
        double fraction = 0;
        double startedMs = 0;
        std::uint64_t logVersion = 0;          // log buffer version at start (live log begins here)
        std::wstring edition;
        std::uint64_t sizeBefore = 0;          // expanded bytes of the edition
        std::uint64_t sizeAfter = 0;           // re-read from the WIM after commit (0 = unknown)
        std::optional<core::ops::ApplyJobResult> result;
        std::optional<Error> error;            // the run could not start (session refused, …)
        core::CancelToken cancel;
    };
    [[nodiscard]] const std::optional<ApplyRun>& applyRun() const noexcept { return m_apply; }
    [[nodiscard]] std::optional<ApplyRun>& applyRunMutable() noexcept { return m_apply; }
    void setApplyRun(std::optional<ApplyRun> run);
    void notifyApply() { notify(Change::Apply); }

    // P06: the running / last ISO build (IsoController).
    struct IsoRun {
        bool running = false;
        double fraction = 0;
        int stage = 0; // 0 extract, 1 repack, 2 write, 3 sha256
        double startedMs = 0;
        std::filesystem::path output;
        std::optional<core::IsoResult> result;
        std::optional<Error> error;
        core::CancelToken cancel;
    };
    [[nodiscard]] const std::optional<IsoRun>& isoRun() const noexcept { return m_iso; }
    [[nodiscard]] std::optional<IsoRun>& isoRunMutable() noexcept { return m_iso; }
    void setIsoRun(std::optional<IsoRun> run);
    void notifyIso() { notify(Change::Iso); }

    // Last inspection of the WinLove mount folder (MountHealth.h); empty until first checked.
    [[nodiscard]] const std::optional<core::MountCheck>& mountFolder() const noexcept { return m_mountFolder; }
    void setMountFolder(std::optional<core::MountCheck> check);

    [[nodiscard]] const std::optional<EngineOperation>& operation() const noexcept { return m_operation; }
    void beginOperation(EngineOperation operation);
    void updateOperation(double fraction);
    void updateOperation(double fraction, int stage);
    void endOperation();

    // Process log ring buffer (installed as a sink for the lifetime of AppState); Loglar reads it.
    [[nodiscard]] std::shared_ptr<log::RingBufferSink> logBuffer() const noexcept { return m_logBuffer; }
    // "Temizle" on Loglar: the page shows only entries written after this buffer version.
    [[nodiscard]] std::uint64_t logClearedVersion() const noexcept { return m_logCleared; }
    void setLogClearedVersion(std::uint64_t version) noexcept { m_logCleared = version; }

    [[nodiscard]] RecentSources& recent() noexcept { return m_recent; }
    void forgetRecent(const std::filesystem::path& path);

    std::size_t subscribe(Listener listener);
    void unsubscribe(std::size_t id);

private:
    void notify(Change change);

    core::TaskRunner m_engine;
    core::TaskRunner m_reader;
    AppSettings m_settings;
    std::optional<core::SourceInfo> m_source;
    std::optional<int> m_selected;
    std::optional<MountedImage> m_mounted;
    std::optional<EngineOperation> m_operation;
    std::optional<core::MountCheck> m_mountFolder;
    core::ops::ChangeSet m_changes;
    std::optional<OptionalFeatures> m_features;
    std::optional<ApplyRun> m_apply;
    std::optional<AppxList> m_appx;
    std::optional<ServiceList> m_services;
    std::vector<RegImport> m_regImports;
    DriverScan m_drivers;
    Unattend m_unattend;
    std::optional<IsoRun> m_iso;
    std::filesystem::path m_settingsFile;
    std::shared_ptr<log::RingBufferSink> m_logBuffer = std::make_shared<log::RingBufferSink>();
    std::uint64_t m_logCleared = 0;
    RecentSources m_recent;
    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextId = 1;
};

} // namespace wl::app
