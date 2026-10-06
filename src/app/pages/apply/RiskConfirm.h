#pragma once
// Content of the 13b dialog: the irreversible operations (shield icon in status.error, name,
// size mono right, 24px rows with line.subtle separators) and the "Riskleri anladım" checkbox.
// When the dialog gets less height than heightFor() (short window, long list) the list scrolls
// and the checkbox stays pinned below it.
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class RiskConfirm : public ui::Widget {
public:
    struct Item {
        std::wstring name;
        std::wstring size; // preformatted, may be empty
    };
    static constexpr float kRow = 24.0f;

    RiskConfirm(std::vector<Item> items, std::wstring ack);
    [[nodiscard]] static float heightFor(std::size_t items) { return kRow * static_cast<float>(items) + 8 + kRow; }

    std::function<void(bool)> onAck;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onWheel(ui::PointF p, float lines) override;

private:
    [[nodiscard]] ui::RectF listRect() const;
    void scrollTo(float offset);

    std::vector<Item> m_items;
    ui::CheckField* m_check = nullptr;
    ui::ScrollBar* m_scroll = nullptr;
    float m_offset = 0;
};

} // namespace wl::app
