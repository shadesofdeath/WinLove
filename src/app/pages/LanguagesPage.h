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
// D-139: the list's title row has the summary at its right ("3 dil · 1 eklenecek · 1,2 GB"); the selected
// language in the detail card (beside it the component-language and size columns are the card's).
#include "app/Localization.h"
#include "app/controllers/LanguageController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageHeader.h"
#include "app/shell/PageView.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/TableView.h"

#include <functional>

namespace wl::app {

class LanguagesPage : public ui::Widget, public ScrollableBody, public DetailSource {
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

    // D-139: the detail card.
    enum Action : int { kActionRemove = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

    void layout() override;
    void paint(ui::Canvas& canvas) override;

    // D-107: the list and the region form at the package's row heights can be taller than the page.
    [[nodiscard]] float contentHeight() const override { return m_contentHeight; }

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
    std::vector<std::wstring> m_names; // m_rows' display names (refresh())
    FetchStrip* m_fetch = nullptr;
    SummaryLine* m_summaryLine = nullptr;
    ui::TableView* m_table = nullptr;
    ui::InfoBar* m_lcu = nullptr;
    bool m_lcuClosed = false; // closed for this page's life
    ui::Dropdown* m_fields[5] = {}; // UI language, system locale, user locale, keyboard, time zone
    std::vector<std::wstring> m_values[5]; // the choice values behind each dropdown (index 0 = unchanged)
    // What each dropdown was last given (audit PG-10): the same items again only move the selection,
    // so an open list is not closed under the user by every queue change.
    std::vector<std::wstring> m_shown[5];
    ui::EmptyState* m_empty = nullptr;
    float m_contentHeight = 0;
};

} // namespace wl::app
