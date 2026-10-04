#pragma once
// D-053 / D-061 Diller: one row per language, then the region settings.
// - DİLLER: every language the image has or gets — Dil · Durum · Özellikler · Bileşen dilleri ·
//   Boyut. In the image: secondary ink; queued: accent. Delete takes a queued language out (its
//   pack, features, component languages and the fonts only it needed). While "Dil ekle…"
//   downloads, a strip above the list shows it with "Durdur". When languages are queued on an
//   image that has a cumulative update, an info bar advises adding that update again (Microsoft:
//   a language added later keeps the base version of its files) with "Güncellemeleri bul".
// - BÖLGE VE DİL: Arayüz dili, Sistem yerel ayarı, Kullanıcı yerel ayarı, Klavye, Saat dilimi —
//   each "Değiştirme" by default; a choice queues SetIntl.
// Header actions (Shell): "Klasörden ekle…", "Dil ekle…".
#include "app/Localization.h"
#include "app/controllers/LanguageController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class LanguagesPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<void()> stopFetch;   // "Durdur" on the download strip
        std::function<void()> findUpdates; // the info bar's action
    };
    LanguagesPage(AppState& state, LanguageController& controller, const Localization& strings, Language language, Intents intents);
    ~LanguagesPage() override;

    // "Dil paketi", "Temel (yazım, klavye)" …
    [[nodiscard]] static std::wstring kindLabel(const Localization& strings, core::LanguagePackFile::Kind kind);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    class FetchStrip;
    void refresh();
    void rebuildDropdowns();
    void picked(int field, int index);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, bool selected);

    AppState& m_state;
    LanguageController& m_controller;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<LanguageRow> m_rows;
    FetchStrip* m_fetch = nullptr;
    ui::TableView* m_table = nullptr;
    ui::InfoBar* m_lcu = nullptr;
    bool m_lcuClosed = false; // closed for this page's life
    ui::Dropdown* m_fields[5] = {}; // UI language, system locale, user locale, keyboard, time zone
    std::vector<std::wstring> m_values[5]; // the choice values behind each dropdown (index 0 = unchanged)
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
