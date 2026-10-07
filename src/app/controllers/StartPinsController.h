#pragma once
// D-069 Başlat menüsü › Sabitlenenler: the Start pins of the image — Windows' own (with its
// promoted placeholders), none, or the user's list in order. The plan lives in the queue as the
// operations core::startPinsOperations makes (the policy in two forms + ProgramData\WinLove\
// StartPins.json, + the empty Windows 10 tile layout when there are no pins); it is read back from
// the JSON file's WriteFile operation, so presets carry it.
// D-083: the same for the taskbar (Surface::Taskbar): Microsoft's Start Layout policy with a layout
// that replaces Windows' pins (core::taskbarPinsOperations, ProgramData\WinLove\TaskbarLayout.xml);
// "applyOnce" is then "the user may unpin" (PinGeneration). Its apps are the Start controller's,
// File Explorer first.
#include "app/state/AppState.h"
#include "core/image/StartMenu.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace wl::app {

class StartPinsController {
public:
    enum class Surface : std::uint8_t { Start, Taskbar };
    // `appsFrom`: the controller whose app list this one shows (the taskbar reads no list of its own).
    StartPinsController(AppState& state, std::function<void(std::function<void()>)> postToUi, Surface surface = Surface::Start,
                        StartPinsController* appsFrom = nullptr);
    ~StartPinsController();
    StartPinsController(const StartPinsController&) = delete;
    StartPinsController& operator=(const StartPinsController&) = delete;

    [[nodiscard]] Surface surface() const noexcept { return m_surface; }
    enum class Mode : std::uint8_t { Windows, Empty, Custom };
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] bool applyOnce() const;
    [[nodiscard]] std::vector<core::StartApp> pins() const; // names filled in from the image's apps

    void setMode(Mode mode);
    void setApplyOnce(bool once);
    void add(const core::StartApp& app);   // at the end; already pinned: nothing
    void remove(std::size_t index);
    void move(std::size_t index, int delta); // reorder
    void setPins(std::vector<core::StartApp> pins);

    // The apps of the mounted image that can be pinned (read once per mount, in the background);
    // nullptr while reading — onLoaded fires then.
    [[nodiscard]] const std::vector<core::StartApp>* apps();
    void preload(); // the same, now (render demos)
    std::function<void()> onLoaded;
    [[nodiscard]] std::filesystem::path pathInImage(const std::wstring& relative) const;
    [[nodiscard]] int changedCount() const; // nav badge: 1 when the queue holds a plan

private:
    // Either surface's plan, in one shape.
    struct Plan {
        bool custom = false;
        std::vector<core::StartApp> pins;
        bool once = true; // Start: applyOnce · taskbar: the user may unpin
    };
    void store(Plan plan);
    [[nodiscard]] std::optional<Plan> queued() const;
    [[nodiscard]] std::vector<std::pair<core::ops::OpKind, std::wstring>> slots() const;
    [[nodiscard]] const std::vector<core::StartApp>* known() const; // the apps read so far (names, icons)

    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    Surface m_surface = Surface::Start;
    StartPinsController* m_appsFrom = nullptr;
    std::vector<core::StartApp> m_merged; // taskbar: File Explorer + the Start list
    std::size_t m_mergedFrom = 0;         // size of the Start list it was made from
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::size_t m_subscription = 0;
    std::filesystem::path m_mountDir;
    std::optional<std::vector<core::StartApp>> m_apps;
    bool m_loading = false;
    bool m_customChosen = false; // "Custom" with no pins yet looks like "Empty" in the queue
};

} // namespace wl::app
