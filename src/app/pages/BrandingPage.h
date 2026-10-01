#pragma once
// D-056 Kişiselleştirme: a form (OEM bilgileri + logo, varsayılan görseller) above a font list.
//   OEM BİLGİLERİ      Üretici · Model · Destek telefonu · Destek saatleri · Destek adresi · Logo
//   VARSAYILAN GÖRSELLER   Masaüstü arka planı · Kilit ekranı · Hesap resmi
//   YAZI TİPLERİ       Yazı tipi · Dosya · Boyut · ×     (header action "Yazı tipi ekle…", drop)
// A picture row: "Seç…", the file's name (or "Windows varsayılanı"), × to go back to the default.
// Everything is a queue operation (BrandingController), so presets carry it.
#include "app/Localization.h"
#include "app/controllers/BrandingController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>

namespace wl::app {

class BrandingPictureField;

class BrandingPage : public ui::Widget {
public:
    struct Intents {
        std::function<std::optional<std::filesystem::path>()> pickPicture;
        std::function<void(const std::wstring& error)> refused; // a file that is not a picture
        std::function<void()> goImages;
    };
    BrandingPage(AppState& state, BrandingController& controller, const Localization& strings, Language language,
                 Intents intents);
    ~BrandingPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void paintFont(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);

    AppState& m_state;
    BrandingController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    ui::FormView* m_form = nullptr;
    std::array<ui::SearchBox*, 5> m_oem{};
    std::array<BrandingPictureField*, 4> m_pictures{}; // by core::PictureSlot
    ui::TableView* m_fonts = nullptr;
    std::vector<BrandingController::Font> m_fontRows;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
