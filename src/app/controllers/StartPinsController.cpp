#include "app/controllers/StartPinsController.h"

#include <algorithm>

namespace wl::app {

StartPinsController::StartPinsController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            m_apps.reset();
            m_loading = false;
            m_mountDir.clear();
            m_customChosen = false;
        }
    });
}

StartPinsController::~StartPinsController() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

std::optional<core::StartPinsPlan> StartPinsController::queued() const {
    return core::startPinsPlanFromOperations(m_state.changes().operations());
}

StartPinsController::Mode StartPinsController::mode() const {
    const auto plan = queued();
    if (!plan) {
        return Mode::Windows;
    }
    return plan->custom || m_customChosen ? Mode::Custom : Mode::Empty;
}

bool StartPinsController::applyOnce() const {
    const auto plan = queued();
    return !plan || plan->applyOnce;
}

std::vector<core::StartApp> StartPinsController::pins() const {
    const auto plan = queued();
    if (!plan) {
        return {};
    }
    auto list = plan->pins;
    if (m_apps) {
        for (auto& pin : list) {
            const auto it = std::ranges::find(*m_apps, pin);
            if (it != m_apps->end()) {
                pin.name = it->name;
                pin.icon = it->icon;
                pin.iconIndex = it->iconIndex;
            }
        }
    }
    return list;
}

void StartPinsController::store(core::StartPinsPlan plan) {
    m_state.unqueueMany(core::startPinsSlots());
    m_state.queueMany(core::startPinsOperations(plan));
}

void StartPinsController::setMode(Mode mode) {
    m_customChosen = mode == Mode::Custom;
    if (mode == Mode::Windows) {
        m_state.unqueueMany(core::startPinsSlots());
        return;
    }
    core::StartPinsPlan plan = queued().value_or(core::StartPinsPlan{});
    plan.custom = mode == Mode::Custom;
    if (!plan.custom) {
        plan.pins.clear();
    }
    store(std::move(plan));
}

void StartPinsController::setApplyOnce(bool once) {
    auto plan = queued();
    if (!plan) {
        return;
    }
    plan->applyOnce = once;
    plan->custom = plan->custom || m_customChosen;
    store(std::move(*plan));
}

void StartPinsController::setPins(std::vector<core::StartApp> pins) {
    core::StartPinsPlan plan = queued().value_or(core::StartPinsPlan{});
    plan.custom = true;
    plan.pins = std::move(pins);
    m_customChosen = true;
    store(std::move(plan));
}

void StartPinsController::add(const core::StartApp& app) {
    auto list = pins();
    if (std::ranges::find(list, app) != list.end()) {
        return;
    }
    list.push_back(app);
    setPins(std::move(list));
}

void StartPinsController::remove(std::size_t index) {
    auto list = pins();
    if (index < list.size()) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
        setPins(std::move(list));
    }
}

void StartPinsController::move(std::size_t index, int delta) {
    auto list = pins();
    const auto to = static_cast<std::ptrdiff_t>(index) + delta;
    if (index >= list.size() || to < 0 || to >= static_cast<std::ptrdiff_t>(list.size())) {
        return;
    }
    std::swap(list[index], list[static_cast<std::size_t>(to)]);
    setPins(std::move(list));
}

std::filesystem::path StartPinsController::pathInImage(const std::wstring& relative) const {
    return m_state.mounted() ? m_state.mounted()->mountDir / relative : std::filesystem::path(relative);
}

const std::vector<core::StartApp>* StartPinsController::apps() {
    if (!m_state.mounted()) {
        return nullptr;
    }
    const auto mountDir = m_state.mounted()->mountDir;
    if (m_apps && m_mountDir == mountDir) {
        return &*m_apps;
    }
    if (m_loading && m_mountDir == mountDir) {
        return nullptr;
    }
    m_mountDir = mountDir;
    m_loading = true;
    auto result = std::make_shared<std::vector<core::StartApp>>();
    std::weak_ptr<bool> alive = m_alive;
    m_state.reader().run<bool>(
        [mountDir, result](const core::TaskContext&) -> Result<bool> {
            *result = core::listStartApps(mountDir);
            return true;
        },
        [this, alive, result, mountDir, post = m_post](Result<bool>) {
            post([this, alive, result, mountDir] {
                if (const auto a = alive.lock(); !a || !*a || m_mountDir != mountDir) {
                    return;
                }
                m_apps = std::move(*result);
                m_loading = false;
                if (onLoaded) {
                    onLoaded();
                }
            });
        });
    return nullptr;
}

void StartPinsController::preload() {
    if (!m_state.mounted()) {
        return;
    }
    m_mountDir = m_state.mounted()->mountDir;
    m_apps = core::listStartApps(m_mountDir);
    m_loading = false;
}

int StartPinsController::changedCount() const {
    return queued() ? 1 : 0;
}

} // namespace wl::app
