#include "app/state/AppState.h"

#include <algorithm>

namespace wl::app {

AppState::AppState(std::filesystem::path recentFile) : m_recent(std::move(recentFile)) {
    m_recent.load();
}

void AppState::setSource(core::SourceInfo source) {
    m_recent.touch(source);
    m_source = std::move(source);
    notify(Change::Recent);
    notify(Change::Source);
}

void AppState::clearSource() {
    m_source.reset();
    notify(Change::Source);
}

void AppState::forgetRecent(const std::filesystem::path& path) {
    m_recent.remove(path);
    notify(Change::Recent);
}

std::size_t AppState::subscribe(Listener listener) {
    const std::size_t id = m_nextId++;
    m_listeners.emplace_back(id, std::move(listener));
    return id;
}

void AppState::unsubscribe(std::size_t id) {
    std::erase_if(m_listeners, [id](const auto& entry) { return entry.first == id; });
}

void AppState::notify(Change change) {
    // Copy: a listener may unsubscribe (page switch) while we iterate.
    const auto listeners = m_listeners;
    for (const auto& [id, listener] : listeners) {
        listener(change);
    }
}

} // namespace wl::app
