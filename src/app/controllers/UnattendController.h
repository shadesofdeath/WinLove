#pragma once
// P13 logic (docs/pages/13-unattended.md): the answer file options live in AppState (they survive
// page switches and are picked up by the ISO build); this edits them, builds the XML text for the
// preview, saves / imports a file and lists the choices the form offers.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "core/unattend/Welcome.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class UnattendController {
public:
    // A dropdown entry: what is written into the file, and what the list shows.
    struct Choice {
        std::wstring value;
        std::wstring label;
    };

    explicit UnattendController(AppState& state);

    // The options as they go into the file: architecture follows the open source.
    [[nodiscard]] static core::UnattendOptions effective(const AppState& state);
    [[nodiscard]] core::UnattendOptions options() const { return effective(m_state); }
    // The first answer given (and an import) also turns "ISO'ya ekle" on: answers that silently
    // stay out of the ISO are never what was meant. The box remains to opt out.
    void edit(const std::function<void(core::UnattendOptions&)>& change);
    [[nodiscard]] bool includeInIso() const noexcept { return m_state.unattend().includeInIso; }
    void setIncludeInIso(bool include);

    [[nodiscard]] std::wstring xml() const;
    [[nodiscard]] std::vector<core::UnattendProblem> problems() const;
    // UTF-8, "\r\n" line ends (the file is opened in Notepad more often than not).
    [[nodiscard]] Result<void> save(const std::filesystem::path& file) const;
    // Replaces the options with what the file says (options WinLove does not know are dropped).
    [[nodiscard]] Result<void> import(const std::filesystem::path& file);

    // Languages of the open source's editions; without a source, the common locales.
    [[nodiscard]] std::vector<Choice> languages() const;
    [[nodiscard]] static const std::vector<Choice>& locales();
    [[nodiscard]] static const std::vector<Choice>& keyboards();
    [[nodiscard]] static const std::vector<Choice>& timeZones();
    // Editions of the open source: value = index.
    [[nodiscard]] std::vector<Choice> editions() const;

    // The bytes written to the ISO root; empty when "ISO'ya ekle" is off.
    [[nodiscard]] static std::string isoFile(const AppState& state);

    // ---- D-084: WinLove's welcome in place of the account ------------------------------------
    // On: the answer file writes the setup account (a fresh random password) and the queue puts the
    // script and its oobe.json into the image; off: both go. The plan (pages, pre-selected choices)
    // lives in the queue's oobe.json; its texts are in the language of the answer file's UI
    // language (Turkish → Turkish, otherwise English; none given: the app's).
    [[nodiscard]] bool welcome() const { return m_state.unattend().options.welcome; }
    void setWelcome(bool on);
    [[nodiscard]] core::WelcomePlan welcomePlan() const; // from the queue; the defaults without one
    void setWelcomePlan(core::WelcomePlan plan);          // queued again (texts filled in here)
    [[nodiscard]] bool welcomeQueued() const;            // the image gets the script on the next Apply
    // The window's texts of one language (the "welcome" section of the strings).
    [[nodiscard]] static std::vector<std::pair<std::string, std::wstring>> welcomeTexts(const Localization& strings);
    // Supplied by the app: the strings of a language (both are in the executable).
    std::function<const Localization*(Language)> stringsOf;
    // The window on this PC as a preview (%TEMP%\WinLove\welcome-preview): windowed, nothing done.
    [[nodiscard]] Result<void> previewWelcome() const;

private:
    [[nodiscard]] std::vector<std::pair<std::string, std::wstring>> welcomeTextsNow() const;

public:

private:
    AppState& m_state;
};

} // namespace wl::app
