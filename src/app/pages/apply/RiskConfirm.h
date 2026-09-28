#pragma once
// Content of the 13b dialog: the irreversible operations (shield icon in status.error, name,
// size mono right, 24px rows with line.subtle separators) and the "Riskleri anladım" checkbox.
#include "ui/widgets/Checkbox.h"

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

private:
    std::vector<Item> m_items;
    ui::CheckField* m_check = nullptr;
};

} // namespace wl::app
