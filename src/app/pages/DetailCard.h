#pragma once
// D-139: the detail card of the working pages — one card, beside the page card, for the selected row
// of Bileşenler, Özellikler, Servisler, Kayıt Defteri, Sürücüler, Görevler, Güncellemeler, Uygulamalar,
// Programlar and Diller. The page says what its row is (DetailModel, built from what the page already
// knows: no new reads), the card draws it the same way on every page:
//   head      a 32 tile (the row's icon, a file's own icon or a letter), the name, a quiet subtitle,
//             the identity in mono (package name, service key, path)
//   state     the row's marks in words: "Uygula'da kaldırılacak" (accent) / "Uyumluluk koruyor" …
//   about     one or two sentences where the data has a description (catalogs, the image's own text)
//   facts     label / value rows under "Ayrıntılar" (size, in the image, at Uygula, risk, …)
//   notes     why a row is held or locked, a risk note: an icon and the wrapped text
//   lists     what is in it / what it needs / what needs it, cut to the room left
//   actions   the row's actions as quiet buttons at the foot (the very actions of the row's click,
//             menu and keys)
// Nothing selected: the page's icon and "Bir satır seç". The card is as wide as the side panels
// (size::inspector) and shows only when the page card keeps kDetailMinPageCard beside it
// (detailCardFits); below that the row's tooltip still says what it is. Texts are measured in
// layout(), paint() only draws.
#include "app/Localization.h"
#include "ui/widgets/Button.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

enum class DetailTone : std::uint8_t { Normal, Quiet, Change, Success, Warning, Error };

struct DetailFact {
    std::wstring label;
    std::wstring value;
    DetailTone tone = DetailTone::Normal;
    bool mono = false;
    bool dot = false; // a 6 px dot in the tone's colour before the value (a risk)
};

struct DetailNote {
    ui::icons::Icon icon = ui::icons::Icon::InfoCircle;
    std::wstring text;
    DetailTone tone = DetailTone::Normal;
};

struct DetailList {
    std::wstring title;
    std::vector<std::wstring> lines;
    bool mono = false;
};

struct DetailAction {
    int id = 0;
    std::wstring label;
    bool enabled = true;
};

struct DetailModel {
    ui::icons::Icon icon = ui::icons::Icon::File;
    std::wstring iconFile; // a file's own icon (a program's, a driver class's); drawn when it loads
    int iconIndex = 0;
    bool letter = false;   // no glyph: the title's first letter on the tile (a program without its icon)
    std::wstring title;
    std::wstring subtitle;
    std::wstring identity;
    DetailNote state;      // its text empty: no state line
    std::wstring about;
    std::vector<DetailFact> facts;
    std::vector<DetailNote> notes;
    std::vector<DetailList> lists;
    std::vector<DetailAction> actions;

    [[nodiscard]] bool empty() const noexcept { return title.empty(); }
    // Tests: a fact by its label, an action by its id (nullptr: none).
    [[nodiscard]] const DetailFact* fact(std::wstring_view label) const;
    [[nodiscard]] const DetailAction* action(int id) const;
};

// What a page with a detail card offers the shell (pages are plain widgets; this is mixed in).
class DetailSource {
public:
    virtual ~DetailSource() = default;
    // The selected row; an empty model when nothing (or a row with nothing more to say) is selected.
    [[nodiscard]] virtual DetailModel detail() const = 0;
    // The page shows rows to choose from (not its empty state): the card belongs beside it.
    [[nodiscard]] virtual bool detailAvailable() const = 0;
    // A card button: the page runs the action it stands for (the same path as its row's click / key).
    virtual void detailAction(int id) = 0;
    // The shell's layout: the card is beside the page now (the table leaves out what the card repeats).
    void setDetailShown(bool shown) noexcept { m_detailShown = shown; }
    [[nodiscard]] bool detailShown() const noexcept { return m_detailShown; }
    // The selection or what the selected row says changed (also: the rows came, the list went).
    std::function<void()> onDetailChanged;

protected:
    void detailChanged() const {
        if (onDetailChanged) {
            onDetailChanged();
        }
    }

private:
    bool m_detailShown = false;
};

// The breakpoint: the card shows when the page card keeps at least this much beside it (decided on
// renders at 1100x700 and 1207x700: 1207 keeps 627, 1100 only 519 — the tables would be squeezed).
inline constexpr float kDetailMinPageCard = 600.0f;
// `pageCardWidth`: the page card's width without the side card.
[[nodiscard]] bool detailCardFits(float pageCardWidth) noexcept;

class DetailCard : public ui::Widget {
public:
    static constexpr int kMaxActions = 3;
    explicit DetailCard(const Localization& strings);

    std::function<void(int id)> onAction;

    void setEmptyIcon(ui::icons::Icon icon);
    void set(DetailModel model);
    [[nodiscard]] const DetailModel& model() const noexcept { return m_model; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    struct Block {
        float y = 0;
        float height = 0;
    };
    const Localization& m_strings;
    DetailModel m_model;
    ui::icons::Icon m_emptyIcon = ui::icons::Icon::File;
    std::wstring m_emptyTitle;
    std::wstring m_emptyBody;
    std::wstring m_letter;
    std::wstring m_detailsTitle;
    std::vector<ui::Button*> m_buttons;
    int m_actionIds[kMaxActions] = {};
    // layout(): where each part goes (paint draws at these).
    Block m_head, m_identity, m_state, m_about, m_factsTitle;
    std::vector<Block> m_facts;
    std::vector<Block> m_notes;
    struct ListBlock {
        Block title;
        float linesY = 0;
        int shown = 0; // lines that fit
        std::wstring more; // "+n daha" when some do not
    };
    std::vector<ListBlock> m_lists;
    float m_foot = 0;   // the top of the actions
    float m_titleH = 0; // the name's one or two lines
};

} // namespace wl::app
