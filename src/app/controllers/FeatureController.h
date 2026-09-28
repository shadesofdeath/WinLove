#pragma once
// P04 logic (docs/pages/04-features.md): reads the mounted image's optional features once per
// mount (engine thread) and turns toggle clicks into ChangeSet operations:
//   feature on  → DisableFeature     feature off → EnableFeature
//   capability installed → RemoveCapability      (adding FoD needs source media: not offered)
// Toggling again removes the queued operation (back to the image's state).
#include "app/state/AppState.h"

#include <functional>
#include <memory>

namespace wl::app {

class FeatureController {
public:
    // What the "Durum" column shows.
    enum class Status : std::uint8_t {
        Enabled, Disabled, Removed, Installed, Pending, WillEnable, WillDisable, WillRemove
    };

    FeatureController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~FeatureController();

    // Starts reading when a mounted image has no list yet (or `force`). No-op without a mount.
    void load(bool force = false);

    [[nodiscard]] const core::ops::Operation* queued(const core::OptionalFeature& item) const;
    [[nodiscard]] bool targetOn(const core::OptionalFeature& item) const;   // the toggle's position
    [[nodiscard]] bool canToggle(const core::OptionalFeature& item) const;
    [[nodiscard]] Status status(const core::OptionalFeature& item) const;
    void toggle(const core::OptionalFeature& item);
    // "Değişiklikleri sıfırla": drop every queued feature/capability operation.
    void resetChanges();
    [[nodiscard]] std::size_t queuedCount() const; // feature + capability operations

    // Pure mapping, unit-tested.
    [[nodiscard]] static core::ops::Operation operationFor(const core::OptionalFeature& item);

private:
    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
