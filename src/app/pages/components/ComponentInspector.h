#pragma once
// Right column (280) of screen 04 for the selected app: title + identity (mono), KeyValue rows
// (Kategori, Boyut, Risk, Durum, Sürüm, Geri alınabilir), UYUMLULUK notes from the catalog,
// İÇERİK (package full name; for a system component its packages and paths), and
// "Kuyruğa ekle / Kuyruktan çıkar" at the bottom.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <optional>

namespace wl::app {

class ComponentInspector : public ui::Widget {
public:
    ComponentInspector(const Localization& strings, Language language);

    std::function<void()> onToggle;

    void set(std::optional<ComponentController::Item> item, std::wstring group, bool queued);
    [[nodiscard]] static ui::icons::Icon iconOf(ComponentController::Item::Kind kind) noexcept;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    const Localization& m_strings;
    Language m_language;
    std::optional<ComponentController::Item> m_item;
    std::wstring m_group;
    bool m_queued = false;
    ui::Button* m_toggle = nullptr;
};

} // namespace wl::app
