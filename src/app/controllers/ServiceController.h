#pragma once
// P10 logic (docs/pages/10-services.md): reads the mounted image's services once per mount
// (engine thread; loads the SYSTEM hive briefly) and turns start-type choices into
// SetServiceStart operations (value = core::startTypeKey). Choosing the image's own start type
// removes the queued operation. Risk and notes come from resources/catalog/services.json.
#include "app/Localization.h"
#include "app/state/AppState.h"

#include <functional>
#include <map>
#include <memory>

namespace wl::app {

struct ServiceNote {
    core::ops::Risk risk = core::ops::Risk::Medium;
    std::wstring notesTr;
    std::wstring notesEn;
};

class ServiceController {
public:
    ServiceController(AppState& state, std::string_view catalogJson, std::function<void(std::function<void()>)> postToUi);
    ~ServiceController();

    void load(bool force = false);

    // The start type the image will have after Uygula (queued or current).
    [[nodiscard]] core::StartType target(const core::ServiceEntry& service) const;
    [[nodiscard]] bool changed(const core::ServiceEntry& service) const;
    void set(const core::ServiceEntry& service, core::StartType start);
    void resetChanges();
    [[nodiscard]] std::size_t queuedCount() const;

    [[nodiscard]] core::ops::Risk risk(const core::ServiceEntry& service) const;
    [[nodiscard]] std::wstring notes(const core::ServiceEntry& service, Language language) const;
    // Dependents (transitively) that stay enabled after `service` is disabled: the warning list.
    [[nodiscard]] std::vector<std::wstring> activeDependents(const core::ServiceEntry& service) const;

    // Pure mapping, unit-tested.
    [[nodiscard]] static core::ops::Operation operationFor(const core::ServiceEntry& service, core::StartType start,
                                                           core::ops::Risk risk);
    [[nodiscard]] static std::map<std::wstring, ServiceNote> parseCatalog(std::string_view json);

private:
    AppState& m_state;
    std::map<std::wstring, ServiceNote> m_catalog; // key: lower-case service name
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
