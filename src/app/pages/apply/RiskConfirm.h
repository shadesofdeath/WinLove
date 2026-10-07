#pragma once
// Content of the 13b dialog: the high-risk operations (shield icon, name, size mono right, 24px
// rows with line.subtle separators) and the "Riskleri anladım" checkbox. Removals (irreversible)
// have the icon in status.error and the size they free; a high-risk setting of Ayarlar / Tweaks
// is one row, icon in status.warning, with the option it gets. When the dialog gets less height
// than heightFor() (short window, long list) the list scrolls and the checkbox stays pinned below it.
#include "app/Localization.h"
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/state/AppState.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace wl::app {

class RiskConfirm : public ui::Widget {
public:
    struct Item {
        std::wstring name;
        std::wstring size; // preformatted, may be empty; for a setting: the option it gets
        bool removal = true;
    };
    static constexpr float kRow = 24.0f;

    // The rows for these operations: a removal by its name with the size it frees; the operations
    // of one Ayarlar / Tweaks setting once, by the setting's name; anything else by its target.
    [[nodiscard]] static std::vector<Item> itemsFor(const AppState& state, const ImageSettingsCatalog& settings,
                                                    const Localization& strings, Language language,
                                                    std::span<const core::ops::Operation> risky);
    [[nodiscard]] static bool removes(core::ops::OpKind kind) noexcept;

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
