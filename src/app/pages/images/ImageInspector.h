#pragma once
// Inspector for the selected edition (screen 02, right column 280, bg.panel): title + source
// line, KeyValue rows (key 96), "SIKIŞTIRMA" and "İŞLEMLER" sections, primary "Bağla" (or
// "Çöz" when this edition is mounted) and "Sil…" at the bottom.
#include "app/Localization.h"
#include "core/image/Source.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <optional>

namespace wl::app {

class ImageInspector : public ui::Widget {
public:
    ImageInspector(const Localization& strings, Language language);

    std::function<void()> onMount;
    std::function<void()> onUnmount;
    std::function<void()> onDelete;

    // `image` null → empty panel. `mountedHere`: this edition is the mounted one.
    void set(const core::SourceInfo* source, const core::ImageInfo* image, bool mountedHere, bool canMount,
             bool canDelete, std::wstring mountTooltip, std::wstring deleteTooltip);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    const Localization& m_strings;
    Language m_language;
    std::optional<core::SourceInfo> m_source; // copy: the panel must not dangle across source switches
    std::optional<core::ImageInfo> m_image;
    bool m_mountedHere = false;
    ui::Button* m_primary = nullptr;
    ui::Button* m_delete = nullptr;
};

} // namespace wl::app
