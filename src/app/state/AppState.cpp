#include "app/state/AppState.h"

#include <algorithm>

namespace wl::app {

AppState::AppState(std::filesystem::path recentFile, std::filesystem::path settingsFile)
    : m_settings(AppSettings::load(settingsFile)), m_settingsFile(settingsFile), m_recent(std::move(recentFile)) {
    log::addSink(m_logBuffer);
    m_recent.load();
}

AppState::~AppState() {
    log::removeSink(m_logBuffer);
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
    const bool sameImage = m_mounted && mounted && m_mounted->mountDir == mounted->mountDir &&
                           m_mounted->imagePath == mounted->imagePath && m_mounted->index == mounted->index;
    m_mounted = std::move(mounted);
    if (!sameImage) {
        // Queue and feature list belong to the image that was mounted.
        m_features.reset();
        m_appx.reset();
        if (!m_changes.empty()) {
            m_changes.clear();
            notify(Change::Queue);
        }
        notify(Change::Features);
        notify(Change::Components);
    }
    notify(Change::Mount);
}

void AppState::queue(core::ops::Operation op) {
    m_changes.add(std::move(op));
    notify(Change::Queue);
}

bool AppState::unqueue(core::ops::OpKind kind, std::wstring_view target) {
    const bool removed = m_changes.remove(kind, target);
    if (removed) {
        notify(Change::Queue);
    }
    return removed;
}

void AppState::unqueueIf(const std::function<bool(const core::ops::Operation&)>& which) {
    const auto ops = m_changes.operations(); // copy: remove() edits the list
    bool any = false;
    for (const auto& op : ops) {
        if (which(op)) {
            any = m_changes.remove(op.kind, op.target) || any;
        }
    }
    if (any) {
        notify(Change::Queue);
    }
}

void AppState::addDriverScan(const std::filesystem::path& folder, std::vector<core::DriverInf> infs) {
    if (std::ranges::find(m_drivers.folders, folder) == m_drivers.folders.end()) {
        m_drivers.folders.push_back(folder);
    }
    for (auto& inf : infs) {
        const bool known = std::ranges::any_of(m_drivers.infs, [&](const core::DriverInf& d) { return d.path == inf.path; });
        if (!known) {
            m_drivers.infs.push_back(std::move(inf));
        }
    }
    notify(Change::Drivers);
}

void AppState::setAppxList(std::optional<AppxList> list) {
    m_appx = std::move(list);
    notify(Change::Components);
}

void AppState::setIsoFolder(std::filesystem::path folder) {
    m_settings.isoFolder = std::move(folder);
    m_settings.save(m_settingsFile);
}

void AppState::setIsoRun(std::optional<IsoRun> run) {
    m_iso = std::move(run);
    notify(Change::Iso);
}

void AppState::setApplyRun(std::optional<ApplyRun> run) {
    m_apply = std::move(run);
    notify(Change::Apply);
}

void AppState::setOptionalFeatures(std::optional<OptionalFeatures> features) {
    m_features = std::move(features);
    notify(Change::Features);
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
