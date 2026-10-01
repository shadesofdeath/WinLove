#pragma once
// P02 İmajlar (docs/pages/02-images.md): summary line, error InfoBar (remedy text + "Onar"),
// mount-folder InfoBar when the WinLove mount folder is not clean (MountHealth), operation strip
// while the engine works, editions table. With no source: EmptyState.
// The inspector column is owned by the Shell (it spans the full content height).
#include "app/controllers/ImageController.h"
#include "app/pages/images/EditionTable.h"
#include "app/pages/images/OperationStrip.h"
#include "ui/widgets/EmptyState.h"
#include "core/image/dism/DismErrors.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"

namespace wl::app {

class ImagesPage : public ui::Widget {
public:
    ImagesPage(AppState& state, ImageController& controller, const Localization& strings, Language language,
               std::function<void()> chooseSource);
    ~ImagesPage() override;

    // `offerRepair`: show "Onar" (ImageController::cleanupMounts).
    void showFailure(const std::wstring& title, const std::wstring& message, bool offerRepair);

    // "Devam et" on the mount-folder bar: restore the mount found there (Shell::continueFolderMount).
    std::function<void()> onContinueMount;
    // Row menu / Del / F2, on the marked editions (the Shell owns the dialogs).
    std::function<void()> onExport;
    std::function<void()> onDelete;
    std::function<void()> onKeepOnly;
    std::function<void()> onRename;
    std::function<void()> onUpgrade; // the mounted edition: "Sürümü yükselt…"
    std::function<void()> onDuplicate; // D-058 "Çoğalt…": a copy of the primary edition
    // Shortcuts into Explorer / a prompt / the clipboard (Shell: exploreMount, …).
    std::function<void()> onExploreMount;
    std::function<void()> onTerminal;
    std::function<void()> onReveal;
    std::function<void()> onCopyInfo;
    // A result that stays on the page until closed (e.g. what "Doğrula" found).
    void showNotice(ui::InfoKind kind, const std::wstring& title, const std::wstring& message);

    [[nodiscard]] static Str remedyText(core::Remedy remedy) noexcept;
    [[nodiscard]] static Str folderStateText(core::MountState state) noexcept;
    [[nodiscard]] static bool remedyRepairs(core::Remedy remedy) noexcept; // "Onar" helps
    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override; // "/" = search
    void focusSearch();

private:
    // D-058: the editions the search and the architecture filter let through.
    [[nodiscard]] std::vector<core::ImageInfo> visibleImages() const;
    void refresh(AppState::Change change);
    void updateFolderBar();
    // One edition: Bağla / Dışa aktar / (mounted: Sürümü yükselt…) / Yeniden adlandır… / Sürümü sil… /
    // Yalnız bu sürümü tut… / (mounted: Bağlama klasörünü aç / Komut istemi) / Dosya konumunu aç /
    // Bilgileri kopyala. Several: export / delete / keep them / copy.
    bool showRowMenu(ui::PointF at);

    AppState& m_state;
    ImageController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    ui::EmptyState* m_empty = nullptr;
    ui::InfoBar* m_error = nullptr;
    ui::InfoBar* m_folder = nullptr;
    OperationStrip* m_strip = nullptr;
    EditionTable* m_table = nullptr;
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_arch = nullptr;
    std::wstring m_needle;
    int m_archFilter = 0; // 0 all, 1 x64, 2 arm64, 3 x86
};

} // namespace wl::app
