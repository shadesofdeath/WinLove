#pragma once
// Image operations for P02 (docs/pages/02-images.md): prepare (ISO → work folder), mount,
// unmount, export, ESD → WIM, delete index, clean up mounts, adopt an existing mount at startup.
// Lives on the UI thread; heavy work runs on AppState::engine(); results come back through
// `postToUi`. Knows nothing about widgets: it reports through Events.
#include "app/generated/StringKeys.g.h"
#include "app/state/AppState.h"
#include "base/Result.h"

#include <functional>
#include <memory>
#include <string>

namespace wl::app {

class ImageController {
public:
    enum class Failure : std::uint8_t { Mount, Unmount, Export, Delete, Prepare, Cleanup };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(Failure, const Error&, int index)> failed;
        std::function<void(Str title, std::wstring detail)> succeeded;       // success toast
        std::function<void(Str title)> refused;                                // busy / not allowed toast
        std::function<void(std::wstring relaunchArguments)> needsAdmin;        // show s4, relaunch with these args
        // A mount from a previous run is still attached: open `source`, then show it as mounted
        // (`edition` is left empty; the shell fills it from the opened source).
        std::function<void(std::filesystem::path source, MountedImage mounted)> restored;
    };

    ImageController(AppState& state, Events events);
    ~ImageController();

    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canMount() const;          // source mountable (not ESD), nothing mounted, idle
    [[nodiscard]] bool canDelete() const;         // WIM file on disk (not inside an ISO)
    [[nodiscard]] bool isEsdSource() const;
    [[nodiscard]] std::optional<int> failedIndex() const noexcept { return m_failedIndex; }

    void mount(int index);
    void unmount(bool commit);
    void exportIndex(int index, const std::filesystem::path& destination);
    void convertEsd(const std::filesystem::path& destination);
    void deleteIndex(int index);
    // Repairs the WinLove mount folder whatever its state (MountHealth: remount / discard /
    // clear leftovers, Explorer windows moved away first) plus DISM's own mount point cleanup.
    void cleanupMounts();
    // Inspects the mount folder on the engine and publishes it as AppState::mountFolder.
    void inspectMountFolder();
    void cancel();
    // Elevated start: restore the WinLove mount left from a previous run (remounts it if DISM
    // says "needs remount", discards it if invalid) and report it through Events::restored.
    void adoptExistingMount();

private:
    using Work = std::function<Result<void>(const core::TaskContext&)>;
    // Runs `work` as EngineOperation `op`; `onSuccess` runs on the UI thread.
    void run(EngineOperation op, Work work, std::function<void()> onSuccess, Failure failure);
    // For ISO sources: extracts to the work folder, switches the source to it, then `next`.
    void withWritableSource(int index, std::function<void()> next);
    [[nodiscard]] std::optional<std::filesystem::path> installWimPath() const;
    [[nodiscard]] std::wstring editionName(int index) const;

    AppState& m_state;
    Events m_events;
    std::optional<int> m_failedIndex;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
