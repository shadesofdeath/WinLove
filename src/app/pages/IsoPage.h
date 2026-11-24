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
#include "app/pages/iso/IsoOptions.h"
#include "app/shell/PageView.h"
#include "core/image/SetupMedia.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/ProgressRing.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"

#include <functional>

namespace wl::app {

class IsoPage : public ui::Widget, public ScrollableBody {
public:
    struct Intents {
        std::function<std::optional<std::filesystem::path>()> pickFolder;
        std::function<void(const std::filesystem::path&)> openFolder; // Explorer, file selected
        std::function<void(std::function<void()>)> postToUi;
        std::function<void()> changed; // tab / drive / form: the header button follows
        std::function<void()> replaceMedia; // D-077: pick Windows 10 media for the setup files
        std::function<void()> goDrivers;    // D-134: Sürücüler (a storage driver only in boot.wim)
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
    // Render demo (D-077): the editions the setup media "cannot install", whatever the source says.
    void setMediaCannotInstallDemo(std::vector<int> editions);
    void showUsbTab();
    // D-144 addendum: the options shared with the Apply page's dialog (AppState::isoChoices).
    [[nodiscard]] IsoOptionControls& options() { return *m_options; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void updateBlocker();
    void computeSize();
    [[nodiscard]] std::vector<int> mediaCannotInstall() const; // D-077 (or the render demo's)
    // D-134: the boot.wim drivers in words for the summary ("pvscsi.inf, iastorvd.inf") and the storage
    // ones the installed Windows would not get — worked out in refresh() (they read the scan, the queue
    // and the media's driver lists), never in paint.
    void syncBootDrivers();
    std::wstring m_bootDriversText;
    std::vector<std::filesystem::path> m_storageOnlyInBoot;
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
    float m_contentHeight = 0; // D-107: the form at 48px rows can be taller than the page

public:
    [[nodiscard]] float contentHeight() const override { return m_contentHeight; }

private:
    ui::InfoBar* m_bar = nullptr;
    ui::ProgressRing* m_ring = nullptr; // D-142: the progress row's, while building
    ui::SearchBox* m_fileName = nullptr;
    ui::SearchBox* m_folder = nullptr;
    ui::Button* m_browse = nullptr;
    // D-144 addendum: label, boot mode, compression and the boxes — the controls the Apply page's "ISO
    // seçenekleri…" shows too, on AppState::isoChoices (pages/iso/IsoOptions).
    std::unique_ptr<IsoOptionControls> m_options;
    std::optional<std::vector<int>> m_cannotInstallDemo; // D-077 render demo
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
