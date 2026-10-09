#include "app/controllers/TaskController.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/ScheduledTasks.h"

#include <json.hpp>

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

namespace {

bool samePath(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

Risk riskFrom(const std::string& text) {
    return text == "high" ? Risk::High : text == "medium" ? Risk::Medium : Risk::Low;
}

// The task's name as the user sees it: the last part of the path.
std::wstring leaf(std::wstring_view path) {
    const auto slash = path.rfind(L'\\');
    return std::wstring(slash == std::wstring_view::npos ? path : path.substr(slash + 1));
}

} // namespace

TaskController::TaskController(AppState& state, std::string_view catalogJson)
    : m_state(state), m_catalog(parseCatalog(catalogJson, &m_categories)) {}

std::vector<TaskEntry> TaskController::parseCatalog(std::string_view json, std::vector<TaskCategory>* categories) {
    std::vector<TaskEntry> tasks;
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.tasks") {
        return tasks;
    }
    std::vector<TaskCategory> cats;
    for (const auto& c : doc.value("categories", nlohmann::json::array())) {
        cats.push_back({c.value("id", std::string{}),
                        {utf8::toWide(c.value("tr", std::string{})), utf8::toWide(c.value("en", std::string{}))}});
    }
    for (const auto& t : doc.value("tasks", nlohmann::json::array())) {
        TaskEntry e;
        e.path = utf8::toWide(t.value("path", std::string{}));
        e.category = t.value("category", std::string{});
        e.name = {utf8::toWide(t.value("tr", std::string{})), utf8::toWide(t.value("en", std::string{}))};
        e.notes = {utf8::toWide(t.value("notes_tr", std::string{})), utf8::toWide(t.value("notes_en", std::string{}))};
        e.risk = riskFrom(t.value("risk", std::string{"low"}));
        e.recommended = t.value("recommended", false);
        if (!core::validTaskPath(e.path) ||
            std::ranges::none_of(cats, [&](const TaskCategory& c) { return c.id == e.category; })) {
            log::warn("app", L"task catalog: skipped " + e.path);
            continue;
        }
        tasks.push_back(std::move(e));
    }
    if (categories) {
        *categories = std::move(cats);
    }
    return tasks;
}

std::vector<TaskEntry> TaskController::tasks() const {
    std::vector<TaskEntry> all = m_catalog;
    auto known = [&](std::wstring_view path) {
        return std::ranges::any_of(all, [&](const TaskEntry& t) { return samePath(t.path, path); });
    };
    auto addCustomRow = [&](const std::wstring& path) {
        if (!known(path)) {
            TaskEntry e;
            e.path = path;
            e.category = "custom";
            e.name = {leaf(path), leaf(path)};
            e.risk = Risk::Medium;
            all.push_back(std::move(e));
        }
    };
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::SetTaskState) {
            addCustomRow(op.target);
        }
    }
    if (const auto& values = m_state.imageValues(); values && m_state.mounted() && values->mountDir == m_state.mounted()->mountDir) {
        for (const auto& path : values->disabledTasks) {
            addCustomRow(path);
        }
    }
    return all;
}

bool TaskController::queued(std::wstring_view path) const {
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::SetTaskState && samePath(op.target, path)) {
            return true;
        }
    }
    return false;
}

bool TaskController::inImage(std::wstring_view path) const {
    return m_state.imageHas(Operation{OpKind::SetTaskState, std::wstring(path), L"disabled"});
}

bool TaskController::off(std::wstring_view path) const {
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::SetTaskState && samePath(op.target, path)) {
            return op.value != L"enabled";
        }
    }
    return inImage(path);
}

void TaskController::toggle(const TaskEntry& task) {
    const bool image = inImage(task.path);
    const bool nowOff = off(task.path);
    // Unqueue whatever is there (any case of the path), then queue what differs from the image.
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::SetTaskState && samePath(op.target, task.path)) {
            slots.emplace_back(op.kind, op.target);
        }
    }
    m_state.unqueueMany(slots);
    const bool wantOff = !nowOff;
    if (wantOff != image) {
        Operation op{OpKind::SetTaskState, task.path, wantOff ? L"disabled" : L"enabled"};
        op.risk = wantOff ? task.risk : Risk::Low;
        m_state.queue(std::move(op));
    }
}

int TaskController::applyRecommended() {
    std::vector<Operation> ops;
    for (const auto& task : m_catalog) {
        if (task.recommended && !off(task.path)) {
            Operation op{OpKind::SetTaskState, task.path, L"disabled"};
            op.risk = task.risk;
            ops.push_back(std::move(op));
        }
    }
    const int n = static_cast<int>(ops.size());
    m_state.queueMany(std::move(ops));
    return n;
}

bool TaskController::addCustom(std::wstring_view path) {
    std::wstring p(path);
    while (!p.empty() && (p.back() == L' ' || p.back() == L'\\')) {
        p.pop_back();
    }
    if (!p.empty() && p.front() != L'\\') {
        p.insert(p.begin(), L'\\');
    }
    if (!core::validTaskPath(p)) {
        return false;
    }
    if (!off(p)) {
        TaskEntry e;
        e.path = p;
        e.risk = Risk::Medium;
        toggle(e);
    }
    return true;
}

namespace {
constexpr wchar_t kCreateSlot[] = L"tasks-create"; // the one CreateTask op's target
} // namespace

std::vector<core::CreatedTask> TaskController::createdTasks() const {
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::CreateTask) {
            auto parsed = core::createdTasksFromJson(utf8::fromWide(op.value));
            return parsed ? std::move(*parsed) : std::vector<core::CreatedTask>{};
        }
    }
    if (const auto& mounted = m_state.mounted()) {
        return core::readCreatedTasks(mounted->mountDir);
    }
    return {};
}

void TaskController::queueCreatedTasks(const std::vector<core::CreatedTask>& tasks) {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::CreateTask) {
            slots.emplace_back(op.kind, op.target);
        }
    }
    m_state.unqueueMany(slots);
    const bool imageHasAny =
        m_state.mounted() && !core::readCreatedTasks(m_state.mounted()->mountDir).empty();
    if (tasks.empty() && !imageHasAny) {
        return; // nothing queued, and the image already creates none
    }
    Operation op{OpKind::CreateTask, std::wstring(kCreateSlot), utf8::toWide(core::createdTasksToJson(tasks))};
    op.risk = Risk::Low;
    m_state.queue(std::move(op));
}

std::optional<core::CreatedTaskProblem> TaskController::addCreatedTask(const core::CreatedTask& task) {
    if (auto problem = core::validCreatedTask(task)) {
        return problem;
    }
    auto list = createdTasks();
    const auto it = std::ranges::find_if(list, [&](const core::CreatedTask& t) { return text::iequals(t.name, task.name); });
    if (it != list.end()) {
        *it = task;
    } else {
        list.push_back(task);
    }
    queueCreatedTasks(list);
    return std::nullopt;
}

void TaskController::removeCreatedTask(std::wstring_view name) {
    auto list = createdTasks();
    std::erase_if(list, [&](const core::CreatedTask& t) { return text::iequals(t.name, name); });
    queueCreatedTasks(list);
}

void TaskController::clearCreatedTasks() {
    queueCreatedTasks({});
}

int TaskController::changedCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::SetTaskState) + m_state.changes().count(OpKind::CreateTask));
}

} // namespace wl::app
