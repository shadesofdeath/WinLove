#pragma once
// P06 ISO Oluştur (docs/pages/06-iso.md, screen 16): tabs ISO / USB, a form (ÇIKTI, ÖNYÜKLEME,
// DOĞRULAMA) and a 320px summary box. The header's "ISO Oluştur" asks this page for the request.
// While building: progress row instead of the result bar, the form is disabled.
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
        std::function<void()> goSource;
        std::function<void(std::function<void()>)> postToUi;
    };

    IsoPage(AppState& state, IsoController& controller, const Localization& strings, Language language, Intents intents);
    ~IsoPage() override;

    [[nodiscard]] IsoController::Request request() const;
    [[nodiscard]] bool formValid() const;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void updateBlocker();
    void computeSize();
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
    ui::CheckField* m_sha = nullptr;
    ui::CheckField* m_open = nullptr;
    ui::EmptyState* m_usb = nullptr;
};

} // namespace wl::app
