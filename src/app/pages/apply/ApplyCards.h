#pragma once
// D-138: the head card of each Apply state — the first thing on the screen, one per state, in the
// raised-card language of the rest of the app (bg.raised, radius r3, a status strip on the left where
// the state has a colour). Texts come preformatted from ApplyPage (worked out when the state changes);
// widths are measured in layout(); paint() only draws.
// - PlanHeader (before the run): the queue in one line, the image, the size before → after with its
//   bar, the time, and the screen's one filled button (Uygula · N).
// - RunHeader (running / paused): the phase and the step, the percentage large, steps · elapsed ·
//   left, a meter, what succeeded / was skipped / waits; paused: why it waits and the three choices.
// - ResultCard (done): the outcome in plain words, the time it took, size before → after and the
//   gain, the steps done / skipped / not run, and what to do next.
#include "app/Localization.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/ProgressRing.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

// A coloured dot and a count ("● 5 tamam"), measured in layout().
struct DotCount {
    std::wstring text;
    ui::tokens::Color dot = ui::tokens::Color::TextTertiary;
    float width = 0;
};

class PlanHeader : public ui::Widget {
public:
    static constexpr float kHeight = 92.0f;
    struct Texts {
        std::wstring title;     // "12 değişiklik · 9 adım"
        std::wstring subtitle;  // "Windows 11 Pro · sonunda imaj kaydedilir ve çözülür"
        std::wstring note;      // keep the PC awake
        std::wstring sizeLabel; // "Boyut"
        std::wstring sizeValue; // "24,06 GB → ≈ 21,65 GB"
        std::wstring sizeDelta; // "2,41 GB küçülür · %10"
        ui::tokens::Color deltaInk = ui::tokens::Color::TextSecondary;
        std::wstring time;      // "Süre ~6 dk–12 dk"
        std::uint64_t before = 0;
        std::uint64_t after = 0;
        std::wstring action;    // "Uygula · 12"
    };
    PlanHeader();
    std::function<void()> onApply;
    void set(Texts texts);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    Texts m_texts;
    ui::Button* m_apply = nullptr;
    float m_labelWidth = 0;
    ui::RectF m_middle{};
};

class RunHeader : public ui::Widget {
public:
    static constexpr float kHeight = 104.0f;
    static constexpr float kPausedHeight = 168.0f;
    struct Texts {
        std::optional<ui::icons::Icon> icon; // nullopt: running — the progress ring (D-142)
        ui::tokens::Color ink = ui::tokens::Color::AccentText;
        std::wstring title;  // the phase ("Bileşenleri kaldır")
        std::wstring detail; // the step ("OpenSSH İstemcisi")
        std::wstring pct;    // "%42"
        std::wstring line;   // "3 / 9 adım · geçen 1 dk 35 sn · kalan ≈ 2 dk"
        std::wstring edition; // "Sürüm 2 / 3 · Windows 11 Home" (D-055), else empty
        std::array<DotCount, 3> counts{};
        float fraction = 0;
        bool paused = false;
        std::wstring body; // paused: where the image is and what the buttons do
    };
    explicit RunHeader(const Localization& strings);
    std::function<void()> onOpenFolder;
    std::function<void()> onStop;
    std::function<void()> onResume;
    void set(Texts texts);
    // The elapsed / remaining line only (the page's timer, once a second).
    void setLine(std::wstring line);
    [[nodiscard]] float preferredHeight() const noexcept { return m_texts.paused ? kPausedHeight : kHeight; }
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    Texts m_texts;
    ui::Button* m_folder = nullptr;
    ui::Button* m_stop = nullptr;
    ui::Button* m_resume = nullptr;
    ui::ProgressRing* m_ring = nullptr;
    float m_titleWidth = 0;
    float m_pctWidth = 0;
};

class ResultCard : public ui::Widget {
public:
    struct Texts {
        ui::icons::Icon icon = ui::icons::Icon::SuccessCircle;
        ui::tokens::Color ink = ui::tokens::Color::StatusSuccess;
        std::wstring title; // "Tamamlandı" / "1 işlem atlandı" …
        std::wstring time;  // "1 dk 52 sn sürdü"
        std::wstring body;  // what happened, in plain words
        std::wstring sizeLabel;
        std::wstring sizeValue; // "22,93 GB → 22,52 GB"
        std::uint64_t before = 0;
        std::uint64_t after = 0;
        std::wstring gainLabel;
        std::wstring gainValue; // "422,36 MB · %2" / "—"
        ui::tokens::Color gainInk = ui::tokens::Color::TextPrimary;
        std::wstring stepsLabel;
        std::array<DotCount, 3> counts{};
        // 2026-11-11: a save / discard after the run is going on — its meter under the body.
        std::optional<float> progress;
        std::wstring progressText; // "%42"
        // D-144: "Bitince ISO oluştur" — a band of its own under the body: what the ISO does / did, or
        // why there is none; its meter while it builds.
        struct Iso {
            ui::icons::Icon icon = ui::icons::Icon::IsoBuild;
            ui::tokens::Color ink = ui::tokens::Color::AccentText;
            std::wstring text;             // "ISO oluşturuluyor · ISO yazılıyor" / "ISO hazır: x.iso · 6,1 GB"
            bool running = false;          // the progress ring in place of the icon, and the meter
            float fraction = 0;
            std::wstring progressText;     // "%42"
        };
        std::optional<Iso> iso;
    };
    struct Action {
        std::wstring label;
        std::optional<ui::icons::Icon> icon;
        std::function<void()> run;
        bool primary = false;
    };
    // `isoActions` (D-144): the ISO band's buttons, at its right ("ISO'yu durdur", "Klasörde göster").
    ResultCard(Texts texts, std::vector<Action> actions, std::vector<Action> isoActions = {});
    // The meter of a save / discard going on (Texts::progress), as the engine reports it.
    void setProgress(float fraction, std::wstring text);
    // D-144: the ISO band while it builds (its line, its meter).
    void setIsoProgress(std::wstring text, float fraction, std::wstring progressText);
    // The card's height at `width` (the body wraps).
    [[nodiscard]] float preferredHeight(float width) const;
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    [[nodiscard]] float bodyHeight(float width) const;
    [[nodiscard]] float isoBandHeight() const noexcept;
    [[nodiscard]] float isoBandTop() const;
    Texts m_texts;
    std::vector<ui::Button*> m_buttons;
    std::vector<ui::Button*> m_isoButtons;
    ui::ProgressRing* m_isoRing = nullptr;
    float m_isoPctWidth = 0;
    float m_bodyHeight = 0;
    float m_titleWidth = 0;
    float m_progressWidth = 0;
};

} // namespace wl::app
