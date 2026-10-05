#pragma once
// D-069 Başlat menüsü › Sabitlenenler: the Start pins of the image — Windows' own (with its
// promoted placeholders), none, or the user's list in order. The plan lives in the queue as the
// operations core::startPinsOperations makes (the policy in two forms + ProgramData\WinLove\
// StartPins.json, + the empty Windows 10 tile layout when there are no pins); it is read back from
// the JSON file's WriteFile operation, so presets carry it.
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
    StartPinsController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~StartPinsController();
    StartPinsController(const StartPinsController&) = delete;
    StartPinsController& operator=(const StartPinsController&) = delete;

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
    void store(core::StartPinsPlan plan);
    [[nodiscard]] std::optional<core::StartPinsPlan> queued() const;

    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::size_t m_subscription = 0;
    std::filesystem::path m_mountDir;
    std::optional<std::vector<core::StartApp>> m_apps;
    bool m_loading = false;
    bool m_customChosen = false; // "Custom" with no pins yet looks like "Empty" in the queue
};

} // namespace wl::app
