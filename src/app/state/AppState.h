#pragma once
// Application state shared by the shell and pages (ARCHITECTURE §4): the engine thread, settings,
// the open source, the selected edition, the mounted image and the running engine operation.
// Pages observe changes through subscribe(); all mutation happens on the UI thread (engine
// results are posted back with ui::Window::post before reaching here).
#include "app/state/AppSettings.h"
#include "app/state/RecentSources.h"
#include "base/Log.h"
#include "core/image/Source.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/dism/OptionalFeatures.h"
#include "core/ops/ChangeSet.h"
#include "core/tasks/TaskRunner.h"

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
    enum class Kind : std::uint8_t { Preparing, Mounting, Unmounting, Exporting, Deleting, Cleaning };
    Kind kind;
    std::wstring edition;      // "Windows 11 Pro"
    std::filesystem::path path; // mount dir, work dir or export target (shown in the strip)
    int index = 0;
    double fraction = 0;       // 0..1
    double startedMs = 0;      // ui::nowMs() at start, for the ETA
    core::CancelToken cancel;
};

class AppState {
public:
    enum class Change : std::uint8_t { Source, Recent, Selection, Mount, Operation, MountFolder, Queue, Features };
    using Listener = std::function<void(Change)>;

    explicit AppState(std::filesystem::path recentFile = RecentSources::defaultFile(),
                      std::filesystem::path settingsFile = AppSettings::defaultFile());
    ~AppState();
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    [[nodiscard]] core::TaskRunner& engine() noexcept { return m_engine; }
    [[nodiscard]] const AppSettings& settings() const noexcept { return m_settings; }

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

    // Last inspection of the WinLove mount folder (MountHealth.h); empty until first checked.
    [[nodiscard]] const std::optional<core::MountCheck>& mountFolder() const noexcept { return m_mountFolder; }
    void setMountFolder(std::optional<core::MountCheck> check);

    [[nodiscard]] const std::optional<EngineOperation>& operation() const noexcept { return m_operation; }
    void beginOperation(EngineOperation operation);
    void updateOperation(double fraction);
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
    AppSettings m_settings;
    std::optional<core::SourceInfo> m_source;
    std::optional<int> m_selected;
    std::optional<MountedImage> m_mounted;
    std::optional<EngineOperation> m_operation;
    std::optional<core::MountCheck> m_mountFolder;
    core::ops::ChangeSet m_changes;
    std::optional<OptionalFeatures> m_features;
    std::shared_ptr<log::RingBufferSink> m_logBuffer = std::make_shared<log::RingBufferSink>();
    std::uint64_t m_logCleared = 0;
    RecentSources m_recent;
    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextId = 1;
};

} // namespace wl::app
