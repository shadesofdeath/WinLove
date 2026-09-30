#pragma once
// Inspector for the selected edition (screen 02, right column 280, bg.panel): title + source
// line, KeyValue rows (key 96), "SIKIŞTIRMA" and "İŞLEMLER" sections, primary "Bağla" (or
// "Çöz" when this edition is mounted) and "Sil…" at the bottom; a pencil next to the title renames.
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
    std::function<void()> onRename;
    std::function<void()> onUpgrade; // the second button of a mounted edition: "Sürümü yükselt…"

    struct State {
        const core::SourceInfo* source = nullptr;
        const core::ImageInfo* image = nullptr; // null → empty panel
        bool mountedHere = false;               // this edition is the mounted one
        bool canMount = false;
        bool canDelete = false;
        bool canRename = false;
        int marked = 1;                         // editions marked in the table: "Sil" takes them all
        std::wstring mountTooltip;              // why a disabled button is disabled
        std::wstring deleteTooltip;
        std::wstring renameTooltip;
        bool canUpgrade = false;                // mounted here and idle
        std::wstring upgradeTooltip;
        std::wstring upgradeQueued;             // "Kuyrukta: Windows 11 Pro sürümüne yükseltme"; empty: none
    };
    void set(State state);

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
    ui::Button* m_rename = nullptr;
    int m_marked = 1;
    std::wstring m_upgradeQueued;
};

} // namespace wl::app
