#pragma once
// P06 ISO Oluştur (docs/pages/06-iso.md, screen 16): tabs ISO / USB, a form (ÇIKTI, ÖNYÜKLEME,
// DOĞRULAMA) and a 320px summary box. The header's "ISO Oluştur" asks this page for the request.
// While building: progress row instead of the result bar, the form is disabled.
// USB tab (D-047): drive (USB / SD only, never the system disk), partition scheme, FAT32 label;
// the same boot and setup-image options; the header button writes the stick after a confirmation.
// D-074: "Önceki kurulumu kullan" — on by itself when an edition of a 24H2+ image has no WinRE (the
// new Setup cannot install it), read from install.wim's file lists on the reader thread.
#include "app/Localization.h"
#include "app/controllers/IsoController.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"

#include <functional>

namespace wl::app {

class IsoPage : public ui::Widget {
public:
    struct Intents {
        std::function<std::optional<std::filesystem::path>()> pickFolder;
        std::function<void(const std::filesystem::path&)> openFolder; // Explorer, file selected
        std::function<void(std::function<void()>)> postToUi;
        std::function<void()> changed; // tab / drive / form: the header button follows
    };

    IsoPage(AppState& state, IsoController& controller, const Localization& strings, Language language, Intents intents);
    ~IsoPage() override;

    [[nodiscard]] IsoController::Request request() const;
    [[nodiscard]] bool formValid() const;
    [[nodiscard]] bool usbTab() const { return m_tabs->selected() == 1; }
    [[nodiscard]] const core::UsbDisk* selectedDisk() const;
    void refreshDisks(); // re-reads the USB drives (reader thread)
    void setDisks(std::vector<core::UsbDisk> disks); // the list as read (also the render demo)
    // The 24H2+ editions without WinRE, as read (also the render demo): the box goes on by itself.
    void setEditionsWithoutWinre(std::vector<int> editions);
    void showUsbTab();

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void updateBlocker();
    void computeSize();
    void checkWinre(); // editions the new Setup cannot install (reader thread)
    void paintLegacyHint(ui::Canvas& canvas, float formRight);
    [[nodiscard]] std::wstring setupImageText() const;
    void notifyChanged();
    void paintIsoForm(ui::Canvas& canvas, float y, float formRight);
    void paintUsbForm(ui::Canvas& canvas, float y, float formRight);
    [[nodiscard]] double estimateSeconds() const;

    AppState& m_state;
    IsoController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::uint64_t m_sourceBytes = 0; // files to write (0 while computing)
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    ui::TabBar* m_tabs = nullptr;
    ui::InfoBar* m_bar = nullptr;
    ui::SearchBox* m_fileName = nullptr;
    ui::SearchBox* m_folder = nullptr;
    ui::Button* m_browse = nullptr;
    ui::SearchBox* m_label = nullptr;
    ui::RadioGroup* m_boot = nullptr;
    ui::Dropdown* m_repack = nullptr;
    ui::CheckField* m_noPrompt = nullptr;
    ui::CheckField* m_bootBypass = nullptr;
    ui::CheckField* m_legacySetup = nullptr;
    bool m_legacyTouched = false;  // the user's choice is kept from then on
    std::vector<int> m_noWinre;    // 24H2+ editions without Winre.wim
    ui::CheckField* m_sha = nullptr;
    ui::CheckField* m_open = nullptr;
    // USB tab
    std::vector<core::UsbDisk> m_disks;
    bool m_disksRead = false;
    ui::Dropdown* m_disk = nullptr;
    ui::Button* m_refresh = nullptr;
    ui::SearchBox* m_usbLabel = nullptr;
    ui::RadioGroup* m_scheme = nullptr;
};

} // namespace wl::app
