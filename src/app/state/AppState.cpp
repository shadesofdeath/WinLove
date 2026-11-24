#include "app/state/AppState.h"

#include "app/state/AnswerStore.h"
#include "base/File.h"
#include "base/Path.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/HostsFile.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::app {

std::vector<int> operationRows(const EngineOperation& op) {
    if (!op.indexes.empty()) {
        return op.indexes;
    }
    return op.index > 0 ? std::vector<int>{op.index} : std::vector<int>{};
}

AppState::AppState(std::filesystem::path recentFile, std::filesystem::path settingsFile, std::filesystem::path answersFile)
    : m_settings(AppSettings::load(settingsFile)), m_settingsFile(settingsFile), m_answersFile(std::move(answersFile)),
      m_queueFile(m_answersFile.empty() ? std::filesystem::path() : m_answersFile.parent_path() / L"queue.json"),
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
    // The ISO options' label and previous-Setup choice were this source's (the same one read again
    // after Uygula keeps them); another source starts from its own.
    if (!m_isoChoices.source.empty() && !text::iequals(m_isoChoices.source.native(), source.path.native())) {
        m_isoChoices.label.reset();
        m_isoChoices.legacySetup.reset();
        m_isoChoices.source.clear();
    }
    m_source = std::move(source);
    m_mediaDrivers.reset(); // D-134: read again for this source (after Uygula the image has changed)
    // Default selection: the first edition, so the inspector has something to show.
    m_selected = m_source->install.images.empty() ? std::nullopt : std::optional<int>(m_source->install.images.front().index);
    m_selection = m_selected ? std::vector<int>{*m_selected} : std::vector<int>{};
    notify(Change::Recent);
    notify(Change::Source);
    notify(Change::Selection);
}

void AppState::clearSource() {
    m_source.reset();
    m_mediaDrivers.reset();
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
        ++m_mountSerial; // what a run left mounted is no longer the mounted image
        // Queue and feature list belong to the image that was mounted.
        m_features.reset();
        m_appx.reset();
        m_system.reset();
        m_inventory.reset();
        m_services.reset();
        m_policies.reset();
        m_contextMenu.reset();
        m_imageValues.reset();
        m_imageDrivers.reset();
        m_imageIntl.reset();
        if (!m_changes.empty() || m_changes.canUndo()) {
            // A new image: a new queue, and no undo back into the old image's.
            const bool had = !m_changes.empty();
            m_changes = core::ops::ChangeSet{};
            if (had) {
                notify(Change::Queue);
            }
        }
        notify(Change::Features);
        notify(Change::Components);
        notify(Change::Services);
        notify(Change::ImageValues);
    }
    notify(Change::Mount);
}

bool AppState::isOwnMountFolder(const std::filesystem::path& folder) const {
    const auto plain = [](const std::filesystem::path& p) {
        std::wstring s = withoutLongPathPrefix(nativePath(p)).wstring();
        while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) {
            s.pop_back();
        }
        return s;
    };
    const std::wstring mine = plain(folder);
    for (const auto& own : {m_settings.mountDirectory(), AppSettings::legacyMountDirectory(), m_settings.vhdMountDirectory()}) {
        if (_wcsicmp(plain(own).c_str(), mine.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

bool AppState::mountedElsewhere() const {
    return m_mounted && !isOwnMountFolder(m_mounted->mountDir);
}

bool AppState::runLeftImageMounted() const {
    if (!m_apply || m_apply->stage != ApplyRun::Stage::Done || !m_mounted || m_apply->mountSerial != m_mountSerial) {
        return false;
    }
    // Saved runs unmounted their image (committed); a run that never started has no result.
    return !m_apply->result || !m_apply->result->committed;
}

void AppState::beginRunClose(bool commit) {
    if (!runLeftImageMounted()) {
        return;
    }
    m_apply->closing = ApplyRun::Closing{commit, ApplyRun::Closing::State::Running, std::nullopt};
    notify(Change::Apply);
}

void AppState::endRunClose(const std::optional<Error>& error, bool cancelled) {
    if (!m_apply || !m_apply->closing || m_apply->closing->state != ApplyRun::Closing::State::Running) {
        return;
    }
    if (cancelled) {
        m_apply->closing.reset(); // the image stays as the run left it
    } else if (error) {
        m_apply->closing->state = ApplyRun::Closing::State::Failed;
        m_apply->closing->error = error;
    } else {
        m_apply->closing->state = ApplyRun::Closing::State::Done;
    }
    notify(Change::Apply);
}

bool AppState::queueLocked() const noexcept {
    return m_apply && m_apply->stage != ApplyRun::Stage::Done;
}

bool AppState::undoQueue() {
    if (queueLocked() || !m_changes.undo()) {
        return false;
    }
    notify(Change::Queue);
    return true;
}

bool AppState::redoQueue() {
    if (queueLocked() || !m_changes.redo()) {
        return false;
    }
    notify(Change::Queue);
    return true;
}

template <class Edit>
auto AppState::editQueue(Edit&& edit) {
    if (!m_companion) {
        return edit();
    }
    const std::vector<std::wstring> before = m_companion->watched(m_changes);
    m_changes.beginGroup();
    auto result = edit();
    m_companion->reconcile(m_changes, before);
    m_changes.endGroup();
    return result;
}

void AppState::queue(core::ops::Operation op) {
    if (queueLocked()) {
        return;
    }
    editQueue([&] {
        m_changes.add(std::move(op));
        return true;
    });
    notify(Change::Queue);
}

void AppState::queueMany(std::vector<core::ops::Operation> ops) {
    if (queueLocked() || ops.empty()) {
        return;
    }
    editQueue([&] {
        m_changes.addAll(std::move(ops));
        return true;
    });
    notify(Change::Queue);
}

void AppState::unqueueMany(const std::vector<std::pair<core::ops::OpKind, std::wstring>>& slots) {
    if (queueLocked()) {
        return;
    }
    if (editQueue([&] { return m_changes.removeAll(slots); }) > 0) {
        notify(Change::Queue);
    }
}

bool AppState::unqueue(core::ops::OpKind kind, std::wstring_view target) {
    if (queueLocked()) {
        return false;
    }
    const bool removed = editQueue([&] { return m_changes.remove(kind, target); });
    if (removed) {
        notify(Change::Queue);
    }
    return removed;
}

void AppState::unqueueIf(const std::function<bool(const core::ops::Operation&)>& which) {
    if (queueLocked()) {
        return;
    }
    // One batch, one undo step (D-152): removed one by one, a page's Sıfırla was as many steps as it
    // took rows out, and Geri al brought back only the last.
    std::vector<std::pair<core::ops::OpKind, std::wstring>> slots;
    for (const auto& op : m_changes.operations()) {
        if (which(op)) {
            slots.emplace_back(op.kind, op.target);
        }
    }
    if (!slots.empty() && editQueue([&] { return m_changes.removeAll(slots); }) > 0) {
        notify(Change::Queue);
    }
}

bool AppState::isBootDriver(const std::filesystem::path& inf) const {
    return std::ranges::find(m_bootDrivers, inf) != m_bootDrivers.end();
}

void AppState::setMediaUpdate(MediaUpdate update) {
    if (update == m_mediaUpdate) {
        return;
    }
    m_mediaUpdate = std::move(update);
    notify(Change::Iso);
}

void AppState::setIsoChoices(IsoChoices choices) {
    choices.source = m_source ? m_source->path : std::filesystem::path();
    if (choices == m_isoChoices) {
        return;
    }
    m_isoChoices = std::move(choices);
    notify(Change::Iso);
}

void AppState::setBootDriver(const std::filesystem::path& inf, bool on) {
    if (isBootDriver(inf) == on) {
        return;
    }
    if (on) {
        m_bootDrivers.push_back(inf);
    } else {
        std::erase(m_bootDrivers, inf);
    }
    notify(Change::Drivers);
}

void AppState::setBootDrivers(const std::vector<std::filesystem::path>& infs, bool on) {
    bool changed = false;
    for (const auto& inf : infs) {
        if (isBootDriver(inf) == on) {
            continue;
        }
        changed = true;
        if (on) {
            m_bootDrivers.push_back(inf);
        } else {
            std::erase(m_bootDrivers, inf);
        }
    }
    if (changed) {
        notify(Change::Drivers);
    }
}

void AppState::setMediaDrivers(std::optional<MediaDrivers> drivers) {
    m_mediaDrivers = std::move(drivers);
    notify(Change::Drivers);
}

void AppState::addDriverScan(const std::filesystem::path& folder, std::vector<core::DriverInf> infs) {
    if (std::ranges::find(m_drivers.folders, folder) == m_drivers.folders.end()) {
        m_drivers.folders.push_back(folder);
    }
    // A set of the paths already listed: comparing each new INF with the whole list was n² path
    // comparisons on the UI thread — a driver pack folder has tens of thousands (audit APP-08).
    auto keyOf = [](const std::filesystem::path& path) {
        std::wstring key = path.lexically_normal().native();
        for (auto& c : key) {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        return key;
    };
    m_drivers.infs.reserve(m_drivers.infs.size() + infs.size());
    for (auto& inf : infs) {
        if (m_driverPaths.insert(keyOf(inf.path)).second) {
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

void AppState::setImageInventory(std::optional<ImageInventory> inventory) {
    m_inventory = std::move(inventory);
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

void AppState::setWindowsDownload(std::optional<WindowsDownload> job) {
    m_windowsDownload = std::move(job);
    notify(Change::WindowsDownload);
}

void AppState::setWindowsDownloadNotice(std::optional<WindowsDownloadNotice> notice) {
    m_windowsDownloadNotice = std::move(notice);
    notify(Change::WindowsDownload);
}

void AppState::setStoreFetch(std::optional<StoreFetch> fetch) {
    m_storeFetch = std::move(fetch);
    notify(Change::StoreFetch);
}

void AppState::setAppUpdate(AppUpdate update) {
    m_appUpdate = std::move(update);
    notify(Change::AppUpdate);
}

void AppState::markAppUpdateToastShown() {
    m_appUpdate.toastShown = true;
}

void AppState::dismissAppUpdateNotice() {
    if (m_appUpdate.notice) {
        m_appUpdate.notice = false;
        notify(Change::AppUpdate);
    }
}

void AppState::setSystemMounts(std::optional<SystemMounts> mounts) {
    m_systemMounts = std::move(mounts);
    notify(Change::SystemMounts);
}

void AppState::setHostDism(core::HostDismReport report) {
    m_hostDism = std::move(report);
    notify(Change::Host);
}

void AppState::setLanguageFetch(std::optional<LanguageFetch> fetch) {
    m_languageFetch = std::move(fetch);
    notify(Change::LanguageFetch);
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
    if (!import.typed) {
        std::erase_if(m_regImports, [&](const RegImport& r) { return !r.typed && r.file == import.file; });
    }
    m_regImports.push_back(std::move(import));
    notify(Change::Registry);
}

void AppState::replaceRegImport(std::size_t index, RegImport import) {
    if (index < m_regImports.size()) {
        m_regImports[index] = std::move(import);
        notify(Change::Registry);
    }
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

void AppState::setPolicyList(std::optional<PolicyList> list) {
    m_policies = std::move(list);
    notify(Change::Policies);
}

void AppState::setContextMenuList(std::optional<ContextMenuList> list) {
    m_contextMenu = std::move(list);
    notify(Change::ContextMenu);
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

void AppState::rememberSource(const core::SourceInfo& source) {
    m_recent.touch(source);
    notify(Change::Recent);
}

void AppState::forgetRecent(const std::filesystem::path& path) {
    m_recent.remove(path);
    notify(Change::Recent);
}

std::size_t AppState::subscribe(Listener listener) {
    const std::size_t id = m_nextId++;
    m_listeners.emplace_back(id, std::make_shared<const Listener>(std::move(listener)));
    return id;
}

void AppState::unsubscribe(std::size_t id) {
    std::erase_if(m_listeners, [id](const auto& entry) { return entry.first == id; });
}

namespace {

bool sameImagePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}

} // namespace

void AppState::saveQueue() const {
    if (m_queueFile.empty()) {
        return;
    }
    std::error_code ec;
    if (m_changes.empty() || !m_mounted) {
        std::filesystem::remove(m_queueFile, ec);
        return;
    }
    using Json = nlohmann::json;
    // The change set's own text goes in as it is: parsing it back to dump it again was two more
    // passes over a queue that can be megabytes (an imported hosts list, the welcome script).
    std::string doc = "{\"version\":1,\"image\":" + Json(utf8::fromWide(m_mounted->imagePath.wstring())).dump() +
                      ",\"index\":" + std::to_string(m_mounted->index) + ",\"changes\":";
    doc += m_changes.toJson();
    doc += "}";
    if (auto written = writeFileAtomic(m_queueFile, doc); !written) {
        log::warn("app", L"could not keep the queue: " + describe(written.error()));
    }
}

std::optional<core::ops::ChangeSet> AppState::savedQueue(const MountedImage& mounted) const {
    if (m_queueFile.empty()) {
        return std::nullopt;
    }
    const auto bytes = readFileBytes(m_queueFile);
    if (!bytes) {
        return std::nullopt;
    }
    using Json = nlohmann::json;
    const Json doc = Json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("changes") || !doc.value("image", Json()).is_string() ||
        !doc.value("index", Json()).is_number_integer()) {
        log::warn("app", L"queue.json is damaged; not restored");
        return std::nullopt;
    }
    if (!sameImagePath(utf8::toWide(doc["image"].get<std::string>()), mounted.imagePath) ||
        doc["index"].get<int>() != mounted.index) {
        return std::nullopt; // the queue of another image: stays until this one's queue is saved over it
    }
    auto changes = core::ops::ChangeSet::fromJson(doc["changes"].dump());
    if (!changes || changes->empty()) {
        return std::nullopt;
    }
    return std::move(*changes);
}

std::size_t AppState::restoreQueue() {
    if (!m_mounted || !m_changes.empty()) {
        return 0;
    }
    return restoreQueue(savedQueue(*m_mounted));
}

std::size_t AppState::restoreQueue(std::optional<core::ops::ChangeSet> saved) {
    if (!m_mounted || queueLocked() || !saved || saved->empty()) {
        return 0;
    }
    std::vector<core::ops::Operation> back;
    for (const auto& op : saved->operations()) {
        if (!m_changes.find(op.kind, op.target)) {
            back.push_back(op);
        }
    }
    if (back.empty()) {
        return 0;
    }
    const std::size_t restored = back.size();
    m_changes.addAll(std::move(back));
    log::info("app", std::format(L"queue of the last run restored: {} changes", restored));
    notify(Change::Queue);
    return restored;
}

AppState::QueueBatch::QueueBatch(AppState& state) : m_state(state) {
    if (m_state.m_queueBatch++ == 0) {
        m_state.m_changes.beginGroup();
    }
}

AppState::QueueBatch::~QueueBatch() {
    if (--m_state.m_queueBatch == 0) {
        m_state.m_changes.endGroup();
        if (std::exchange(m_state.m_queueBatchDirty, false)) {
            m_state.notify(Change::Queue);
        }
    }
}

void AppState::notify(Change change) {
    ++m_revision;
    if (change == Change::Queue) {
        if (m_queueBatch > 0) {
            m_queueBatchDirty = true;
            return;
        }
        saveQueue();
    }
    // Copy: a listener may unsubscribe (page switch) while we iterate. The copy shares the
    // functions, so one that unsubscribes itself stays alive until it returns.
    const auto listeners = m_listeners;
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        const std::size_t id = listeners[i].first;
        // One that an earlier listener of this round unsubscribed (it destroyed the page that
        // listens) is not called on what is left of it (audit APP-16). The first one is still there.
        if (i > 0 && std::ranges::none_of(m_listeners, [id](const auto& entry) { return entry.first == id; })) {
            continue;
        }
        (*listeners[i].second)(change);
    }
}

} // namespace wl::app
