#pragma once
// The image header of the WinLoveUI package (D-107, docs/LAYOUT.md "Görüntü başlığı"): under the
// title bar on every screen — the edition's name (Display 28, −0.025em) with a state badge, one
// line of facts (version · build, architecture, language, the source file) and a link to change
// it; on the right the size before → after and what the queue saves, over a 180×4 bar.
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

class ImageHeader : public ui::Widget {
public:
    static constexpr float kHeight = 64.0f; // D-108: 12 above, 26 name, 4, 16 facts, 6

    enum class BadgeKind : std::uint8_t { Plain, Mounted, Working };
    struct Badge {
        std::wstring text;
        BadgeKind kind = BadgeKind::Plain;
    };
    struct Sizes {
        std::wstring before;  // "17,82 GB"
        std::wstring after;   // empty: nothing queued that changes the size
        std::wstring delta;   // "−0,86 GB"
        float ratio = 1.0f;   // after / before, the bar's fill
    };
    struct Model {
        std::wstring title;
        bool placeholder = false; // no image: the title in text.tertiary
        std::optional<Badge> badge;
        std::vector<std::wstring> facts;
        std::wstring link; // "Değiştir" / "Aç…"; empty = none
        std::optional<Sizes> sizes;
    };

    ImageHeader();
    std::function<void()> onLink;
    void set(Model model);

    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    [[nodiscard]] ui::Cursor cursor() const override;

private:
    Model m_model;
    ui::RectF m_linkRect{};
    ui::PointF m_pointer{};
    bool m_overLink = false;
};

} // namespace wl::app
