#pragma once
// D-144 addendum (the owner, 2026-11-11: "Uygula sayfasına ISO oluştur ekledin ama kullanıcı ISO oluşturma
// seçeneklerini seçemiyor"): ISO Oluştur's option controls, made once and used by both the ISO page and
// the Apply page's "ISO seçenekleri…" dialog. One source of truth: AppState::isoChoices (+ the media
// update's box, AppState::MediaUpdate::enabled) — a control writes it at once, sync() reads it back, so
// what is changed in one place is what the other shows and what "Bitince ISO oluştur" builds with
// (IsoController::defaultRequest). The host lays the controls out and paints their labels; the hints
// beside three boxes and the summary texts are here, so both places say the same thing.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::app {

class IsoOptionControls {
public:
    struct Hooks {
        std::function<void(std::function<void()>)> postToUi; // the WinRE read's answer (reader thread)
        std::function<void()> changed;                        // a control changed the choices
    };
    // The controls are created as children of `host` (which lays them out).
    IsoOptionControls(ui::Widget& host, AppState& state, const Localization& strings, Hooks hooks);
    ~IsoOptionControls();
    IsoOptionControls(const IsoOptionControls&) = delete;
    IsoOptionControls& operator=(const IsoOptionControls&) = delete;

    ui::SearchBox* label = nullptr;
    ui::RadioGroup* boot = nullptr;
    ui::Dropdown* repack = nullptr;
    ui::CheckField* noPrompt = nullptr;
    ui::CheckField* bootBypass = nullptr;
    ui::CheckField* legacySetup = nullptr;
    ui::CheckField* mediaUpdate = nullptr; // D-080
    ui::CheckField* secureBoot = nullptr;  // D-094
    ui::CheckField* sha = nullptr;         // its row label says it: the box stands alone

    // The controls from AppState (values, enabled states, tooltips). `running`: an ISO is being built.
    void sync(bool running);
    // Which 24H2+ editions have no WinRE (reader thread): the previous Setup's box goes on by itself.
    void checkWinre();
    void setEditionsWithoutWinre(std::vector<int> editions);
    [[nodiscard]] const std::vector<int>& editionsWithoutWinre() const noexcept { return m_noWinre; }
    // Render demo (D-077): the media's own "previous Setup" is not taken into account.
    void setIgnoreMediaSetup(bool ignore) { m_ignoreMediaSetup = ignore; }
    // Media older than 24H2 opens the previous Setup already: nothing to switch.
    [[nodiscard]] bool previousMedia() const;

    // Beside the boxes (each only where the form leaves room, up to `formRight`).
    void paintLegacyHint(ui::Canvas& canvas, float formRight) const;
    void paintMediaHint(ui::Canvas& canvas, float formRight);
    void paintSecureBootHint(ui::Canvas& canvas, float formRight) const;
    // The summary rows: what goes into boot.wim (empty: nothing), the media update, which Setup opens
    // (`cannotInstall`: D-077's editions the media cannot install).
    [[nodiscard]] std::wstring setupImageText() const;
    [[nodiscard]] std::pair<std::wstring, ui::tokens::Color> mediaUpdateSummary() const;
    [[nodiscard]] std::pair<std::wstring, ui::tokens::Color> setupUiSummary(bool cannotInstall) const;

private:
    void store(const std::function<void(AppState::IsoChoices&)>& change);
    void syncMediaSource();
    [[nodiscard]] std::wstring mediaUpdateSource() const;

    AppState& m_state;
    const Localization& m_strings;
    Hooks m_hooks;
    std::vector<int> m_noWinre;
    bool m_ignoreMediaSetup = false;
    std::optional<bool> m_dbTrusts2023; // this PC's firmware db (nullopt: not known)
    // Audit PG-07: the KBs of the media update's two files, asked again only when they are other files.
    std::wstring m_mediaSource;
    std::wstring m_mediaSourceLcu;
    std::wstring m_mediaSourceSetup;
    bool m_mediaSourceKnown = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// The Apply row's one line: "UEFI + BIOS · etiket CCCOMA_X64FRE · ESD · SHA-256" — the options as
// "Bitince ISO oluştur" would build with them now (defaultRequest's view of them).
[[nodiscard]] std::wstring isoChoicesSummary(const AppState& state, const Localization& strings);

// The Apply page's "ISO seçenekleri…": the same controls in a dialog of their own (changes apply at once;
// Tamam / Esc closes). `close` pops the dialog.
struct IsoOptionsDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr;
};
[[nodiscard]] IsoOptionsDialog makeIsoOptionsDialog(AppState& state, const Localization& strings,
                                                    std::function<void(std::function<void()>)> postToUi,
                                                    std::function<void()> close);

} // namespace wl::app
