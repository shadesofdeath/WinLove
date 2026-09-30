#pragma once
// D-053 Diller: three sections.
// - İMAJDAKİ DİLLER: what the image has (dism /Get-Intl, read on first show): its languages and
//   current settings.
// - EKLENECEK DİL PAKETLERİ: queued language packs and features — Dil · Tür · Mimari · Boyut ·
//   Dosya; Delete removes. Header action (Shell): "Dil paketi klasörü tara…".
// - BÖLGE VE DİL: Arayüz dili, Sistem yerel ayarı, Kullanıcı yerel ayarı, Klavye, Saat dilimi —
//   each "Değiştirme" by default; a choice queues SetIntl.
#include "app/Localization.h"
#include "app/controllers/LanguageController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class LanguagesPage : public ui::Widget {
public:
    LanguagesPage(AppState& state, LanguageController& controller, const Localization& strings, Language language,
                  std::function<void()> goImages);
    ~LanguagesPage() override;

    // "Dil paketi", "Temel (yazım, klavye)" …
    [[nodiscard]] static std::wstring kindLabel(const Localization& strings, core::LanguagePackFile::Kind kind);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    void refresh();
    void rebuildDropdowns();
    void picked(int field, int index);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect);

    AppState& m_state;
    LanguageController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<core::LanguagePackFile> m_packs;
    ui::TableView* m_table = nullptr;
    ui::Dropdown* m_fields[5] = {}; // UI language, system locale, user locale, keyboard, time zone
    std::vector<std::wstring> m_values[5]; // the choice values behind each dropdown (index 0 = unchanged)
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
