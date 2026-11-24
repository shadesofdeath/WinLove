#pragma once
// D-138 Görüntü › Özet (docs/pages/02-images.md): where the open image stands, at a glance.
// - What it is: the mounted edition (else the selected one) with its build, architecture, language,
//   size and WIM file, mounted or not; under it the next step in one sentence and the page's one
//   filled button (Bağla / Uygula'ya geç / ISO Oluştur …).
// - The running Apply, or the last one while it is the news, with a link to the Apply screen.
// - What is in it: a tile per list the app has read (components by category, provisioned apps,
//   features, third-party drivers, languages, services, queued updates); a list not read yet says
//   so — the overview never reads one itself. Each tile opens its page.
// - What is queued, grouped as the navigation is (Entegre et · Kaldır · Yapılandır), the size
//   before → after, the time, the high-risk steps and what the compatibility report says.
// Everything is worked out on the state's notifications (OverviewModel + texts); paint() only draws.
#include "app/Localization.h"
#include "app/pages/overview/OverviewModel.h"
#include "app/pages/overview/OverviewTiles.h"
#include "app/shell/PageView.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/ProgressRing.h"

#include <array>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace wl::app {

class OverviewPage : public ui::Widget, public ScrollableBody {
public:
    // The parts of OverviewInputs the Shell's controllers know, asked for again only when the state
    // they come from changed.
    enum Part : unsigned {
        kQueuePart = 1,      // nav counts, risky steps, the plan's time, a download Uygula waits for
        kCompatPart = 2,     // the compatibility report
        kComponentsPart = 4, // the component tree's categories
        kImagePart = 8,      // can the selected edition be mounted, is the source an ESD
        kAllParts = 15,
    };
    struct Intents {
        std::function<void(OverviewInputs&, unsigned parts)> gather;
        std::function<void(PageId)> open;               // a tile, a chip, a link
        std::function<void(OverviewModel::Next)> act;   // the filled button
    };

    OverviewPage(AppState& state, const Localization& strings, Language language, Intents intents);
    ~OverviewPage() override;

    [[nodiscard]] const OverviewModel& model() const noexcept { return m_model; }
    [[nodiscard]] float contentHeight() const override { return m_contentHeight; }
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    enum class BadgeKind : std::uint8_t { None, Mounted, Idle, Working };
    struct RunCount {
        std::wstring text;
        ui::tokens::Color dot = ui::tokens::Color::TextTertiary;
        float width = 0;
    };

    void refresh(unsigned parts);
    void rebuildTexts();
    void rebuildChips();
    [[nodiscard]] std::wstring tileValue(const OverviewModel::Tile& tile, bool& quiet) const;

    AppState& m_state;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    OverviewInputs m_inputs;
    OverviewModel m_model;

    ui::EmptyState* m_empty = nullptr;
    ui::Button* m_primary = nullptr;
    ui::Button* m_runLink = nullptr;
    ui::ProgressRing* m_runRing = nullptr; // D-142: the run card's, while it runs
    std::array<OverviewTile*, OverviewModel::kTiles> m_tiles{};
    std::vector<OverviewChip*> m_chips;
    std::vector<std::size_t> m_chipGroup; // per chip: its group in m_model.queue.groups
    std::vector<std::wstring> m_groupCaptions;
    std::vector<std::tuple<Str, PageId, int>> m_chipKey; // what the chips were made from

    // Texts (rebuildTexts) and their measured widths (layout).
    std::wstring m_name, m_badge, m_facts, m_fileFacts, m_mountDir;
    BadgeKind m_badgeKind = BadgeKind::None;
    std::wstring m_nextLabel, m_nextText;
    std::wstring m_runSection, m_contentsSection, m_contentsHint, m_queueSection, m_queueHint;
    std::wstring m_runTitle, m_runTime;
    ui::icons::Icon m_runIcon = ui::icons::Icon::QueueClock;
    ui::tokens::Color m_runInk = ui::tokens::Color::AccentText;
    std::array<RunCount, 3> m_runCounts{};
    std::wstring m_queueMessage, m_heldText;
    std::wstring m_sizeLabel, m_sizeValue, m_sizeDelta, m_timeText;
    ui::tokens::Color m_sizeDeltaInk = ui::tokens::Color::TextSecondary;
    std::wstring m_riskText, m_guardText;
    ui::tokens::Color m_riskInk = ui::tokens::Color::TextTertiary;
    ui::tokens::Color m_guardInk = ui::tokens::Color::StatusSuccess;
    bool m_guardWarn = false;
    float m_nameWidth = 0, m_badgeWidth = 0, m_nextLabelWidth = 0, m_sizeLabelWidth = 0;

    // Geometry (layout).
    ui::RectF m_identity{}, m_band{}, m_runCard{}, m_runTitleArea{}, m_contentsTitleArea{}, m_queueTitleArea{}, m_queueCard{};
    std::vector<ui::RectF> m_captionRects; // per queue group
    ui::RectF m_queueLeft{}, m_queueRight{}, m_heldRect{};
    float m_contentHeight = 0;
    bool m_twoColumns = false; // the queue card: groups | size, risk, guards
    float m_closeFraction = 0; // the save / discard after a run: the engine's fraction
    float m_isoFraction = 0; // D-144: the ISO after the run, while it builds
    bool m_rightOnly = false;  // ... with nothing on the left: the right column alone, the whole width
};

} // namespace wl::app
