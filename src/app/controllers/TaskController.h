#pragma once
// D-048 logic of the Görevler page: the task catalog (resources/catalog/tasks.json) and the
// queue. A task is "off" when a SetTaskState "disabled" is queued for it, or — nothing queued —
// the image's tasks.cmd already switches it off (an earlier run). Turning such a task back on
// queues "enabled", which takes it out of the script. Tasks the user adds by path are shown with
// the catalog's ("Özel").
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/state/AppState.h"
#include "core/image/ScheduledTasks.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct TaskCategory {
    std::string id;
    LocalizedText name;
};

struct TaskEntry {
    std::wstring path;
    std::string category; // "custom" for the user's own
    LocalizedText name;
    LocalizedText notes;
    core::ops::Risk risk = core::ops::Risk::Low;
    bool recommended = false;
};

class TaskController {
public:
    TaskController(AppState& state, std::string_view catalogJson);

    // Malformed entries (bad path, unknown category) are skipped.
    [[nodiscard]] static std::vector<TaskEntry> parseCatalog(std::string_view json, std::vector<TaskCategory>* categories);

    [[nodiscard]] const std::vector<TaskCategory>& categories() const noexcept { return m_categories; }
    // The catalog, then the user's own (queued or in the image, not in the catalog).
    [[nodiscard]] std::vector<TaskEntry> tasks() const;

    [[nodiscard]] bool off(std::wstring_view path) const;
    [[nodiscard]] bool inImage(std::wstring_view path) const; // the image's script switches it off
    [[nodiscard]] bool queued(std::wstring_view path) const;
    void toggle(const TaskEntry& task);
    // "Önerilenleri kapat": every recommended task off, one queue edit. Returns how many changed.
    int applyRecommended();
    // Queues a task by path (off). False: not a task path.
    bool addCustom(std::wstring_view path);

    // ---- D-102: custom recurring tasks the image creates after setup (one CreateTask op) --------
    // Queued tasks, or — nothing queued — what the image already creates (an earlier run).
    [[nodiscard]] std::vector<core::CreatedTask> createdTasks() const;
    // Validates and queues the task (replacing one with the same name); nullopt on success.
    [[nodiscard]] std::optional<core::CreatedTaskProblem> addCreatedTask(const core::CreatedTask& task);
    void removeCreatedTask(std::wstring_view name);
    void clearCreatedTasks();

    [[nodiscard]] int changedCount() const; // nav badge: SetTaskState + CreateTask operations

private:
    void queueCreatedTasks(const std::vector<core::CreatedTask>& tasks);

private:
    AppState& m_state;
    std::vector<TaskCategory> m_categories;
    std::vector<TaskEntry> m_catalog;
};

} // namespace wl::app
