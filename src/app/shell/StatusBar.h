#pragma once
// The status bar (D-109: the reference tool's, on the panel color, size::statusBar, text 12 in).
// 2026-11-10 (UI polish, the owner's request — a step past the reference tool): the left side is a
// row of quiet segments, each one a link to where it belongs — the image (a dot in the accent color
// and the mounted edition, or "Bağlı imaj yok": the Görüntü page), the queued changes ("12
// değişiklik": Uygula) and their estimated effect on the image's size ("≈ 1,2 GB kazanç": Uygula).
// A segment with nothing to say is not drawn. Then the running task (label, 80×4 bar, percent);
// right, the facts: the image's version or this PC's DISM, and the free space where WinLove works.
// Segments are apart by a short line like the ribbon's groups; hover fades in like a Button's.
// Everything is measured and placed when it changes (layout), never while painting.
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Widget.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

class StatusBar : public ui::Widget {
public:
    // D-140: Update — a newer WinLove is out (until the About page has been seen), in the accent with its icon.
    enum class Segment : std::uint8_t { Image, Changes, Size, Update };
    static constexpr std::size_t kSegments = 4;
    struct Model {
        std::wstring image; // the mounted edition, or "Bağlı imaj yok"
        bool mounted = false;
        std::wstring imageTip;
        std::wstring changes; // "12 değişiklik"; empty: not drawn
        std::wstring changesTip;
        std::wstring size;    // "≈ 1,2 GB kazanç"; empty: not drawn
        std::wstring sizeTip;
        std::wstring update;  // "Yeni sürüm: WinLove 1.4.0 Beta"; empty: not drawn (D-140)
        std::wstring updateTip;
        bool operator==(const Model&) const = default;
    };

    StatusBar();
    std::function<void(Segment)> onSegment;
    void setModel(Model model);
    // Background task: label + bar + percent; nullopt hides it.
    void setTask(std::optional<std::wstring> label, float fraction);
    // Right-aligned facts, apart like the segments.
    void setInfo(std::vector<std::wstring> info);

    [[nodiscard]] const Model& model() const noexcept { return m_model; }
    // Where a segment is (its hover / click area); empty when it is not drawn.
    [[nodiscard]] ui::RectF segmentRect(Segment segment) const;
    [[nodiscard]] ui::RectF infoRect() const noexcept { return m_infoRect; }
    [[nodiscard]] bool animating() const noexcept { return m_hoverFade.running(); }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool tick(double now) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    [[nodiscard]] ui::Cursor cursor() const override;
    [[nodiscard]] ui::RectF tooltipAnchor() const override; // the segment under the pointer

private:
    struct Placed {
        ui::RectF rect;      // hover / click area; width 0: not drawn
        ui::RectF text;      // where its text goes
        float separator = 0; // x of the line before it (0: none)
    };
    [[nodiscard]] const std::wstring& textOf(std::size_t segment) const;
    [[nodiscard]] const std::wstring& tipOf(std::size_t segment) const;
    void hoverTo(int segment);

    Model m_model;
    std::array<Placed, kSegments> m_placed{};
    std::optional<std::wstring> m_task;
    std::wstring m_taskPercent;
    float m_taskFraction = 0;
    bool m_taskMeasured = true; // false: the work's words only (setTask with a fraction below 0)
    ui::RectF m_taskLabel{};
    ui::RectF m_taskBar{};
    ui::RectF m_taskPercentRect{};
    std::vector<std::wstring> m_info;
    std::vector<ui::RectF> m_infoRects;
    std::vector<float> m_infoSeparators;
    ui::RectF m_infoRect{};
    int m_hover = -1;
    ui::ItemHover m_hoverFade;
};

} // namespace wl::app
