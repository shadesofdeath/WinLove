#pragma once
// Application state shared by the shell and pages (ARCHITECTURE §4): the engine thread, the open
// source, recent sources. Pages observe changes through subscribe(); all mutation happens on the
// UI thread (engine results are posted back with ui::Window::post before reaching here).
#include "app/state/RecentSources.h"
#include "core/image/Source.h"
#include "core/tasks/TaskRunner.h"

#include <functional>
#include <optional>
#include <vector>

namespace wl::app {

class AppState {
public:
    enum class Change : std::uint8_t { Source, Recent };
    using Listener = std::function<void(Change)>;

    explicit AppState(std::filesystem::path recentFile = RecentSources::defaultFile());

    [[nodiscard]] core::TaskRunner& engine() noexcept { return m_engine; }

    [[nodiscard]] const std::optional<core::SourceInfo>& source() const noexcept { return m_source; }
    void setSource(core::SourceInfo source); // also records it in the recent list
    void clearSource();

    [[nodiscard]] RecentSources& recent() noexcept { return m_recent; }
    void forgetRecent(const std::filesystem::path& path);

    std::size_t subscribe(Listener listener);
    void unsubscribe(std::size_t id);

private:
    void notify(Change change);

    core::TaskRunner m_engine;
    std::optional<core::SourceInfo> m_source;
    RecentSources m_recent;
    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextId = 1;
};

} // namespace wl::app
