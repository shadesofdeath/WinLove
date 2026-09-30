#pragma once
// Reads everything the pages need from a freshly mounted image, right after the mount
// (docs/pages/02-images.md "İçerik okuma", D-027): optional features (P04), provisioned apps
// (P07), services (P10) — one engine job, one after the other. It runs as
// EngineOperation::Kind::Reading, so the Images page strip and the status bar show a second
// progress after the mount's.
// Each list is published as soon as it is read. A failed read marks only that list Failed (its
// page offers "Yeniden dene"). Cancelling leaves the unread lists empty: their pages read them
// on entry, as they did before.
#include "app/state/AppState.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class PreloadController {
public:
    static constexpr int kStages = 3; // EngineOperation::stage: 0 features, 1 apps, 2 services

    // The reads (engine thread). Replaceable in unit tests; the default talks to DISM / the hive.
    struct Readers {
        std::function<Result<std::vector<core::OptionalFeature>>(const std::filesystem::path& mountDir,
                                                                 const core::TaskContext&)> features;
        std::function<Result<std::vector<core::AppxComponent>>(const std::filesystem::path& mountDir,
                                                               const core::TaskContext&)> apps;
        std::function<Result<std::vector<core::ServiceEntry>>(const std::filesystem::path& mountDir,
                                                              const core::TaskContext&)> services;
    };
    [[nodiscard]] static Readers engineReaders();

    PreloadController(AppState& state, std::function<void(std::function<void()>)> postToUi,
                      Readers readers = engineReaders());
    ~PreloadController();

    // Reads the lists the mounted image does not have yet. No-op without a mount, while another
    // operation runs, or when every list is there (or being read) already.
    void start();

    std::function<void()> onCancelled; // the user stopped it before every list was read

private:
    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    Readers m_readers;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
