#pragma once
// D-050 / D-054 logic of the Uygulamalar page.
// - Apps to provision: packages are opened (manifest, dependencies next to them, licence) on the
//   reader thread and queued as AddAppx, one per package file.
// - Default apps: one SetDefaultApps operation holds the whole XML. Imports (a file, this PC's
//   own associations) merge into it by identifier; a row can be taken out; the browser quick
//   pick writes http / https / .htm / .html for a known browser.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "core/image/AppxInstall.h"
#include "core/image/dism/DefaultApps.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

class AppsController {
public:
    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(int added, std::vector<Error> errors)> packagesAdded;
        std::function<void(int associations)> hostImported;
        std::function<void(const Error&)> failed;
    };
    AppsController(AppState& state, Events events);
    ~AppsController();

    // ---- apps ----
    [[nodiscard]] std::wstring imageArchitecture() const; // of the mounted edition ("x64")
    void addPackages(std::vector<std::filesystem::path> files); // reader thread, then queued
    [[nodiscard]] std::vector<core::AppxInstall> queuedApps() const;
    [[nodiscard]] int appCount() const;
    [[nodiscard]] static core::ops::Operation operationFor(const core::AppxInstall& install);

    // ---- default apps ----
    [[nodiscard]] std::vector<core::AppAssociation> associations() const; // the queued XML
    // Merges `list` into the queued associations (an identifier already there is replaced).
    void mergeAssociations(const std::vector<core::AppAssociation>& list);
    void removeAssociation(std::wstring_view identifier);
    [[nodiscard]] Result<int> importAssociationsFile(const std::filesystem::path& file);
    void importHostAssociations(); // background: dism /Online /Export-DefaultAppAssociations
    [[nodiscard]] bool importingHost() const noexcept { return m_importingHost; }

    struct Browser {
        Str label;
        std::wstring progId;
        std::wstring name;
    };
    [[nodiscard]] static const std::vector<Browser>& browsers();
    // Which of browsers() owns http in the queued XML (-1: none of them).
    [[nodiscard]] int browser() const;
    void setBrowser(int index); // -1: takes the browser rows out

private:
    void setAssociations(const std::vector<core::AppAssociation>& list);

    AppState& m_state;
    Events m_events;
    bool m_importingHost = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_background; // last: joined first
};

} // namespace wl::app
