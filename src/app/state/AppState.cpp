#include "app/state/AppState.h"

#include "app/state/AnswerStore.h"
#include "core/image/HostsFile.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::app {

AppState::AppState(std::filesystem::path recentFile, std::filesystem::path settingsFile, std::filesystem::path answersFile)
    : m_settings(AppSettings::load(settingsFile)), m_settingsFile(settingsFile), m_answersFile(std::move(answersFile)),
      m_recent(std::move(recentFile)) {
    log::addSink(m_logBuffer);
    m_recent.load();
    if (!m_answersFile.empty()) {
        if (auto answers = loadAnswers(m_answersFile)) {
            m_unattend = Unattend{std::move(answers->options), answers->includeInIso};
            log::info("app", L"answer file of the last run restored");
        }
    }
}

AppState::~AppState() {
    log::removeSink(m_logBuffer);
}

void AppState::setSource(core::SourceInfo source) {
    m_recent.touch(source);
    m_source = std::move(source);
    // Default selection: the first edition, so the inspector has something to show.
    m_selected = m_source->install.images.empty() ? std::nullopt : std::optional<int>(m_source->install.images.front().index);
    m_selection = m_selected ? std::vector<int>{*m_selected} : std::vector<int>{};
    notify(Change::Recent);
    notify(Change::Source);
    notify(Change::Selection);
}

void AppState::clearSource() {
    m_source.reset();
    m_selected.reset();
    m_selection.clear();
    notify(Change::Source);
    notify(Change::Selection);
}

void AppState::select(std::optional<int> index) {
    std::vector<int> only = index ? std::vector<int>{*index} : std::vector<int>{};
    if (index != m_selected || only != m_selection) {
        m_selected = index;
        m_selection = std::move(only);
        notify(Change::Selection);
    }
}

void AppState::selectMany(std::vector<int> indexes, int primary) {
    std::ranges::sort(indexes);
    indexes.erase(std::ranges::unique(indexes).begin(), indexes.end());
    if (!std::ranges::binary_search(indexes, primary)) {
        indexes.insert(std::ranges::lower_bound(indexes, primary), primary);
    }
    if (m_selected != primary || indexes != m_selection) {
        m_selected = primary;
        m_selection = std::move(indexes);
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
        m_system.reset();
        m_services.reset();
        m_imageValues.reset();
        m_imageDrivers.reset();
        m_imageIntl.reset();
        if (!m_changes.empty()) {
            m_changes.clear();
            notify(Change::Queue);
        }
        notify(Change::Features);
        notify(Change::Components);
        notify(Change::Services);
        notify(Change::ImageValues);
    }
    notify(Change::Mount);
}

bool AppState::queueLocked() const noexcept {
    return m_apply && m_apply->stage != ApplyRun::Stage::Done;
}

void AppState::queue(core::ops::Operation op) {
    if (queueLocked()) {
        return;
    }
    m_changes.add(std::move(op));
    notify(Change::Queue);
}

void AppState::queueMany(std::vector<core::ops::Operation> ops) {
    if (queueLocked() || ops.empty()) {
        return;
    }
    m_changes.addAll(std::move(ops));
    notify(Change::Queue);
}

void AppState::unqueueMany(const std::vector<std::pair<core::ops::OpKind, std::wstring>>& slots) {
    if (queueLocked()) {
        return;
    }
    if (m_changes.removeAll(slots) > 0) {
        notify(Change::Queue);
    }
}

bool AppState::unqueue(core::ops::OpKind kind, std::wstring_view target) {
    if (queueLocked()) {
        return false;
    }
    const bool removed = m_changes.remove(kind, target);
    if (removed) {
        notify(Change::Queue);
    }
    return removed;
}

void AppState::unqueueIf(const std::function<bool(const core::ops::Operation&)>& which) {
    if (queueLocked()) {
        return;
    }
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

void AppState::setSystemComponents(std::optional<SystemComponents> components) {
    m_system = std::move(components);
    notify(Change::Components);
}

void AppState::setIsoFolder(std::filesystem::path folder) {
    m_settings.isoFolder = std::move(folder);
    m_settings.save(m_settingsFile);
}

void AppState::setSettings(AppSettings settings) {
    if (settings == m_settings) {
        return;
    }
    m_settings = std::move(settings);
    m_settings.save(m_settingsFile);
    notify(Change::Settings);
}

void AppState::setUpdateFetch(std::optional<UpdateFetch> fetch) {
    m_updateFetch = std::move(fetch);
    notify(Change::UpdateFetch);
}

void AppState::setIsoRun(std::optional<IsoRun> run) {
    m_iso = std::move(run);
    notify(Change::Iso);
}

void AppState::setApplyRun(std::optional<ApplyRun> run) {
    m_apply = std::move(run);
    notify(Change::Apply);
}

void AppState::addRegImport(RegImport import) {
    // Re-importing the same file replaces it.
    std::erase_if(m_regImports, [&](const RegImport& r) { return r.file == import.file; });
    m_regImports.push_back(std::move(import));
    notify(Change::Registry);
}

void AppState::removeRegImport(std::size_t index) {
    if (index < m_regImports.size()) {
        m_regImports.erase(m_regImports.begin() + static_cast<std::ptrdiff_t>(index));
        notify(Change::Registry);
    }
}

void AppState::setUnattend(Unattend unattend) {
    m_unattend = std::move(unattend);
    if (!m_answersFile.empty()) {
        saveAnswers(m_answersFile, StoredAnswers{m_unattend.options, m_unattend.includeInIso});
    }
    notify(Change::Unattend);
}

void AppState::setServiceList(std::optional<ServiceList> list) {
    m_services = std::move(list);
    notify(Change::Services);
}

void AppState::setImageIntl(std::optional<ImageIntl> intl) {
    m_imageIntl = std::move(intl);
    notify(Change::Intl);
}

void AppState::setImageDrivers(std::optional<ImageDrivers> drivers) {
    m_imageDrivers = std::move(drivers);
    notify(Change::Drivers);
}

void AppState::setImageValues(std::optional<ImageValues> values) {
    m_imageValues = std::move(values);
    notify(Change::ImageValues);
}

std::wstring AppState::imageValueKey(core::ops::OpKind kind, std::wstring_view target, std::wstring_view value) {
    using core::ops::OpKind;
    const bool registry = kind == OpKind::SetRegistryValue || kind == OpKind::SetRegistryFirstLogon;
    std::wstring key = registry                     ? std::wstring(L"reg\n")
                       : kind == OpKind::WriteFile ? std::wstring(L"file\n")
                                                   : std::format(L"op{}\n", static_cast<int>(kind));
    if (kind == OpKind::SetTaskState) { // task paths compare without case
        for (const wchar_t c : target) {
            key.push_back(static_cast<wchar_t>(std::towlower(c)));
        }
    } else {
        key.append(target);
    }
    key += L'\n';
    if (kind == OpKind::SetHosts) { // entries compare as parsed ("0.0.0.0 name" lines)
        key += core::formatHostEntries(core::parseHosts(value));
    } else {
        key.append(value);
    }
    return key;
}

bool AppState::imageHas(const core::ops::Operation& op) const {
    using core::ops::OpKind;
    if (!m_mounted) {
        return false;
    }
    if (op.kind == OpKind::SetServiceStart) {
        if (!m_services || m_services->mountDir != m_mounted->mountDir ||
            m_services->status != ServiceList::Status::Ready) {
            return false;
        }
        const auto it = std::ranges::find_if(m_services->items, [&](const core::ServiceEntry& s) {
            return _wcsicmp(s.name.c_str(), op.target.c_str()) == 0;
        });
        return it != m_services->items.end() && core::startTypeKey(it->start) == op.value;
    }
    if (!m_imageValues || m_imageValues->mountDir != m_mounted->mountDir ||
        m_imageValues->status != ImageValues::Status::Ready) {
        return false;
    }
    return m_imageValues->held.contains(imageValueKey(op.kind, op.target, op.value));
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

void AppState::updateOperation(double fraction, int stage) {
    if (m_operation) {
        m_operation->fraction = fraction;
        m_operation->stage = stage;
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
