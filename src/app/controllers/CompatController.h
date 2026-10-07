#pragma once
// D-082: the compatibility guards in the app. Which guards are on (AppSettings::guards; a new user
// gets the catalog's defaults), what keeps one operation out of the queue (Bileşenler and Servisler
// lock the row and say why), and the queue kept clean: after a guard is switched on, an app taken
// back off the queue or a preset loaded, whatever a guard or a kept app now holds leaves the queue
// and `onDropped` says what.
// Two guards are not the user's: the runtimes kept apps need (always), and App Installer while the
// Programlar page has programs to install (its window installs them with winget).
#include "app/catalog/AppxCatalog.h"
#include "app/catalog/CompatCatalog.h"
#include "app/state/AppState.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class PostSetupController;

class CompatController {
public:
    CompatController(AppState& state, const Localization& strings, Language language, CompatCatalog catalog,
                     const AppxCatalog& appxNames, const PostSetupController& postSetup);
    ~CompatController();
    CompatController(const CompatController&) = delete;
    CompatController& operator=(const CompatController&) = delete;

    [[nodiscard]] const CompatCatalog& catalog() const noexcept { return m_catalog; }
    [[nodiscard]] bool isOn(std::wstring_view id) const;
    [[nodiscard]] std::size_t onCount() const;
    void setOn(std::wstring_view id, bool on);
    // The Uyumluluk dialog's answer: exactly these are on (unknown ids are dropped).
    void setGuards(const std::vector<std::wstring>& on);
    [[nodiscard]] std::vector<std::wstring> onIds() const;

    // The guards that hold now: the user's that are on, and App Installer for the Programlar page.
    [[nodiscard]] std::vector<core::ops::CompatGuard> active() const;
    // What keeps `op` out of the current queue (empty: nothing).
    [[nodiscard]] core::ops::CompatBlock block(const core::ops::Operation& op) const;
    // One line for a tooltip: "Korunuyor: Windows Update" / "Kullanan: Hesap Makinesi, Microsoft Store".
    [[nodiscard]] std::wstring explain(const core::ops::CompatBlock& block) const;

    // A toast line when the queue lost operations to a guard.
    std::function<void(const std::wstring& title, const std::wstring& body)> onDropped;
    // The name of a queued operation for that line (the Shell knows the catalogs); the target if unset.
    std::function<std::wstring(const core::ops::Operation&)> describe;

    static constexpr const wchar_t* kProgramsGuard = L"programs";

private:
    [[nodiscard]] core::ops::AppxNeeds needs() const;
    [[nodiscard]] std::wstring appName(const std::wstring& identity) const;
    [[nodiscard]] std::wstring guardName(const std::wstring& id) const;
    void enforce();

    AppState& m_state;
    const Localization& m_strings;
    CompatCatalog m_catalog;
    const AppxCatalog& m_appxNames;
    const PostSetupController& m_postSetup;
    Language m_language;
    std::size_t m_subscription = 0;
    bool m_enforcing = false;
};

} // namespace wl::app
