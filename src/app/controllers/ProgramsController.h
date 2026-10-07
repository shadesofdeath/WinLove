#pragma once
// D-078 Programlar: programs the installed Windows gets at the first sign-in, from winget's whole
// repository. winget's signed index is kept current in <work>\winget (once a day, on a thread of
// its own) and searched here; the catalog (programs.json) is what the page shows before a search.
// The picks live in the post-setup plan (PostSetupController::setPrograms): one queue operation,
// carried by presets, written by Uygula. A package's details and icon are fetched when its row is
// on screen, one at a time, and cached on disk (everything checked back to the signed index).
#include "app/Localization.h"
#include "app/catalog/ProgramCatalog.h"
#include "app/controllers/PostSetupController.h"
#include "app/state/AppState.h"
#include "core/programs/Winget.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wl::core {
class TaskRunner;
}

namespace wl::app {

class ProgramsController {
public:
    struct Events {
        std::function<void(std::function<void()>)> postToUi;
    };
    enum class Status : std::uint8_t { Idle, Loading, Ready, Failed };
    struct Category {
        std::wstring name;
        std::vector<core::WingetPackage> programs; // the catalog's, as the index has them now
        std::size_t more = 0;                      // other packages winget tags as this category
    };
    struct Bundle {
        std::string id; // programs.json's
        std::wstring name;
        std::wstring description;
        std::vector<core::WingetPackage> programs;
    };
    enum class BundleState : std::uint8_t { None, Some, All };

    ProgramsController(AppState& state, PostSetupController& postSetup, ProgramCatalog catalog, const Localization& strings,
                       Language language, Events events);
    ~ProgramsController();

    // Starts keeping the index current (no-op while loading or when ready, unless `refresh`).
    void load(bool refresh = false);
    [[nodiscard]] Status status() const noexcept { return m_status; }
    [[nodiscard]] const Error& error() const noexcept { return m_error; }
    [[nodiscard]] std::size_t packageCount() const noexcept { return m_count; }
    [[nodiscard]] std::filesystem::path cacheFolder() const; // <work>\winget

    [[nodiscard]] std::vector<Category> categories() const;
    // The rest of category `index`: every package with one of its tags, its own left out, by name.
    [[nodiscard]] std::vector<core::WingetPackage> moreIn(std::size_t index) const;
    // The whole repository by name.
    [[nodiscard]] std::vector<core::WingetPackage> everything() const;
    [[nodiscard]] std::vector<Bundle> bundles() const;
    [[nodiscard]] BundleState bundleState(const Bundle& bundle) const;
    // Picks every program of the bundle not picked yet, or — when all are — takes them out: one
    // queue edit. Returns how many changed.
    int toggleBundle(const Bundle& bundle);
    [[nodiscard]] std::vector<core::WingetPackage> search(std::wstring_view text, std::size_t limit) const;
    // What the index says about a package (a pick that is not in the catalog shows from here too).
    [[nodiscard]] std::optional<core::WingetPackage> package(std::wstring_view id) const;

    [[nodiscard]] bool picked(std::wstring_view id) const;
    void toggle(const core::WingetPackage& package);
    void clear();
    [[nodiscard]] const std::vector<core::PostSetupProgram>& picks() const { return m_postSetup.programs(); }
    [[nodiscard]] std::size_t pickCount() const { return picks().size(); }
    // App Installer (winget) is queued for removal: nothing could install the programs.
    [[nodiscard]] bool wingetRemoved() const;

    // Details of a package: nullptr while they are on their way (the fetch is started) or when they
    // could not be read (detailsFailed). Change::Programs when they arrive.
    [[nodiscard]] const core::WingetDetails* details(std::wstring_view id);
    [[nodiscard]] bool detailsFailed(std::wstring_view id) const;
    // The package's icon file (after its details); empty when it has none or it is on its way.
    [[nodiscard]] std::filesystem::path icon(std::wstring_view id);

    // The install window on this PC, as it would go, nothing installed (programs.ps1 dryRun).
    [[nodiscard]] Result<void> preview() const;
    // The window's texts in the app's language (programs.ps1 "texts").
    [[nodiscard]] std::vector<std::pair<std::wstring, std::wstring>> windowTexts() const;

private:
    void fetchNext();

    AppState& m_state;
    PostSetupController& m_postSetup;
    ProgramCatalog m_catalog;
    const Localization& m_strings;
    Language m_language;
    Events m_events;
    Status m_status = Status::Idle;
    Error m_error;
    std::size_t m_count = 0;
    std::optional<core::WingetIndex> m_index;
    std::map<std::wstring, core::WingetDetails> m_details; // by lower-case id
    std::map<std::wstring, std::filesystem::path> m_icons; // empty path: none
    std::set<std::wstring> m_failed;
    std::vector<std::wstring> m_wanted; // lower-case ids waiting for a fetch, newest last
    bool m_fetching = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_net; // last: joined before the rest goes
};

} // namespace wl::app
