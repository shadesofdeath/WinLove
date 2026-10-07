#pragma once
// D-078: the right column (280) of the Programlar page for the selected program — its icon (or a
// letter mark), name, publisher and id, the short description in the app's language when the
// manifest has it, KeyValue rows (Sürüm, Lisans, Kurulum, Mimari), its tags, and at the bottom
// "Ekle / Çıkar" and the homepage. Details arrive from winget a moment after the selection.
#include "app/Localization.h"
#include "core/programs/Winget.h"
#include "ui/widgets/Button.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace wl::app {

class ProgramInspector : public ui::Widget {
public:
    ProgramInspector(const Localization& strings, Language language);

    std::function<void()> onToggle;
    std::function<void(const std::wstring& url)> onOpenUrl;

    // `details` null: still on their way, or `failed`.
    void set(std::optional<core::WingetPackage> package, const core::WingetDetails* details, std::filesystem::path icon,
             bool picked, bool failed);

    // A program's mark when it has no icon: its first letter on accent.subtle (also the table's).
    static void paintLetter(ui::Canvas& canvas, ui::RectF box, std::wstring_view name, bool large);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    const Localization& m_strings;
    Language m_language;
    std::optional<core::WingetPackage> m_package;
    std::optional<core::WingetDetails> m_details;
    std::filesystem::path m_icon;
    bool m_picked = false;
    bool m_failed = false;
    ui::Button* m_toggle = nullptr;
    ui::Button* m_homepage = nullptr;
};

} // namespace wl::app
