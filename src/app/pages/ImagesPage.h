#pragma once
// P02 İmajlar (docs/pages/02-images.md): summary line, error InfoBar (with "Mount'ları temizle"),
// operation strip while the engine works, editions table. With no source: EmptyState.
// The inspector column is owned by the Shell (it spans the full content height).
#include "app/controllers/ImageController.h"
#include "app/pages/images/EditionTable.h"
#include "app/pages/images/OperationStrip.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"

namespace wl::app {

class ImagesPage : public ui::Widget {
public:
    ImagesPage(AppState& state, ImageController& controller, const Localization& strings, Language language,
               std::function<void()> chooseSource);
    ~ImagesPage() override;

    void showFailure(const std::wstring& title, const std::wstring& message, bool offerCleanup);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh(AppState::Change change);

    AppState& m_state;
    ImageController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    ui::EmptyState* m_empty = nullptr;
    ui::InfoBar* m_error = nullptr;
    OperationStrip* m_strip = nullptr;
    EditionTable* m_table = nullptr;
};

} // namespace wl::app
