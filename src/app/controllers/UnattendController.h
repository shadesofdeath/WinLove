#pragma once
// P13 logic (docs/pages/13-unattended.md): the answer file options live in AppState (they survive
// page switches and are picked up by the ISO build); this edits them, builds the XML text for the
// preview, saves / imports a file and lists the choices the form offers.
#include "app/state/AppState.h"

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

private:
    AppState& m_state;
};

} // namespace wl::app
