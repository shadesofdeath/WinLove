#include "app/state/AppState.h"

#include <algorithm>

namespace wl::app {

AppState::AppState(std::filesystem::path recentFile, std::filesystem::path settingsFile)
    : m_settings(AppSettings::load(settingsFile)), m_recent(std::move(recentFile)) {
    m_recent.load();
}

void AppState::setSource(core::SourceInfo source) {
    m_recent.touch(source);
    m_source = std::move(source);
    // Default selection: the first edition, so the inspector has something to show.
    m_selected = m_source->install.images.empty() ? std::nullopt : std::optional<int>(m_source->install.images.front().index);
    notify(Change::Recent);
    notify(Change::Source);
    notify(Change::Selection);
}

void AppState::clearSource() {
    m_source.reset();
    m_selected.reset();
    notify(Change::Source);
    notify(Change::Selection);
}

void AppState::select(std::optional<int> index) {
    if (index != m_selected) {
        m_selected = index;
        notify(Change::Selection);
    }
}

const core::ImageInfo* AppState::selectedImage() const {
    if (!m_source || !m_selected) {
        return nullptr;
    }
    for (const auto& image : m_source->install.images) {
        if (image.index == *m_selected) {
            return &image;
        }
    }
    return nullptr;
}

void AppState::setMounted(std::optional<MountedImage> mounted) {
    m_mounted = std::move(mounted);
    notify(Change::Mount);
}

void AppState::setMountFolder(std::optional<core::MountCheck> check) {
    m_mountFolder = std::move(check);
    notify(Change::MountFolder);
}

void AppState::beginOperation(EngineOperation operation) {
    m_operation = std::move(operation);
    notify(Change::Operation);
}

void AppState::updateOperation(double fraction) {
    if (m_operation) {
        m_operation->fraction = fraction;
        notify(Change::Operation);
    }
}

void AppState::endOperation() {
    m_operation.reset();
    notify(Change::Operation);
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
