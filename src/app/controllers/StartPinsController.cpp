#include "app/controllers/StartPinsController.h"

#include "base/Text.h"

#include <algorithm>

namespace wl::app {

StartPinsController::StartPinsController(AppState& state, std::function<void(std::function<void()>)> postToUi, Surface surface,
                                         StartPinsController* appsFrom)
    : m_state(state), m_post(std::move(postToUi)), m_surface(surface), m_appsFrom(appsFrom) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            m_apps.reset();
            m_loading = false;
            m_mountDir.clear();
            m_customChosen = false;
            m_merged.clear();
            m_mergedFrom = 0;
        }
    });
}

StartPinsController::~StartPinsController() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

std::optional<StartPinsController::Plan> StartPinsController::queued() const {
    if (m_surface == Surface::Taskbar) {
        const auto plan = core::taskbarPlanFromOperations(m_state.changes().operations());
        if (!plan) {
            return std::nullopt;
        }
        return Plan{!plan->pins.empty(), plan->pins, plan->userMayUnpin};
    }
    const auto plan = core::startPinsPlanFromOperations(m_state.changes().operations());
    if (!plan) {
        return std::nullopt;
    }
    return Plan{plan->custom, plan->pins, plan->applyOnce};
}

std::vector<std::pair<core::ops::OpKind, std::wstring>> StartPinsController::slots() const {
    return m_surface == Surface::Taskbar ? core::taskbarPinsSlots() : core::startPinsSlots();
}

const std::vector<core::StartApp>* StartPinsController::known() const {
    if (m_surface == Surface::Taskbar) {
        return m_merged.empty() ? nullptr : &m_merged;
    }
    return m_apps ? &*m_apps : nullptr;
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
    return !plan || plan->once;
}

std::vector<core::StartApp> StartPinsController::pins() const {
    const auto plan = queued();
    if (!plan) {
        return {};
    }
    auto list = plan->pins;
    if (const auto* apps = known()) {
        for (auto& pin : list) {
            const auto it = std::ranges::find(*apps, pin);
            if (it != apps->end()) {
                pin.name = it->name;
                pin.icon = it->icon;
                pin.iconIndex = it->iconIndex;
            }
        }
    }
    return list;
}

void StartPinsController::store(Plan plan) {
    m_state.unqueueMany(slots());
    if (m_surface == Surface::Taskbar) {
        m_state.queueMany(core::taskbarPinsOperations({plan.custom ? std::move(plan.pins) : std::vector<core::StartApp>{}, plan.once}));
        return;
    }
    m_state.queueMany(core::startPinsOperations({plan.custom, std::move(plan.pins), plan.once}));
}

void StartPinsController::setMode(Mode mode) {
    m_customChosen = mode == Mode::Custom;
    if (mode == Mode::Windows) {
        m_state.unqueueMany(slots());
        return;
    }
    Plan plan = queued().value_or(Plan{});
    plan.custom = mode == Mode::Custom;
    if (!plan.custom) {
        plan.pins.clear();
    } else if (plan.pins.empty() && m_surface == Surface::Taskbar) {
        // A taskbar of one's own starts from File Explorer (a list with nothing is "empty").
        plan.pins.push_back(core::fileExplorerPin());
        if (const auto* apps = known(); apps && !apps->empty()) {
            plan.pins.front() = apps->front();
        }
    }
    store(std::move(plan));
}

void StartPinsController::setApplyOnce(bool once) {
    auto plan = queued();
    if (!plan) {
        return;
    }
    plan->once = once;
    plan->custom = plan->custom || m_customChosen;
    store(std::move(*plan));
}

void StartPinsController::setPins(std::vector<core::StartApp> pins) {
    Plan plan = queued().value_or(Plan{});
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
    if (m_surface == Surface::Taskbar && m_appsFrom) {
        const auto* start = m_appsFrom->apps();
        if (!start) {
            return nullptr;
        }
        if (m_merged.empty() || m_mergedFrom != start->size()) {
            // File Explorer as the taskbar names it, with the name and icon of the image's own
            // shortcut to it (which the list then leaves out: it would be the same pin twice).
            core::StartApp explorer = core::fileExplorerPin();
            m_merged.clear();
            for (const auto& app : *start) {
                if (app.kind == core::StartApp::Kind::DesktopLink && text::iendsWith(app.id, L"\\File Explorer.lnk")) {
                    explorer.name = app.name;
                    explorer.icon = app.icon;
                    explorer.iconIndex = app.iconIndex;
                    continue;
                }
                m_merged.push_back(app);
            }
            m_merged.insert(m_merged.begin(), explorer);
            m_mergedFrom = start->size();
        }
        return &m_merged;
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
    if (m_surface == Surface::Taskbar && m_appsFrom) {
        m_appsFrom->preload();
        (void)apps();
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
