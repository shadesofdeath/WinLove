#pragma once
// "Bu bilgisayar (canlı)" card (screen 01): 1px line.strong r3, padding 16. Icon + title,
// OS line (caption), key/value rows (key column 80): drive usage, administrator state,
// then the "Canlı sistemi düzenle" button.
#include "app/Localization.h"
#include "core/system/LiveSystem.h"
#include "ui/widgets/Button.h"

#include <functional>

namespace wl::app {

class LiveCard : public ui::Widget {
public:
    LiveCard(const Localization& strings, Language language, core::LiveSystemInfo info, bool elevated);
    std::function<void()> onEdit;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    const Localization& m_strings;
    Language m_language;
    core::LiveSystemInfo m_info;
    bool m_elevated;
    ui::Button* m_edit = nullptr;
};

} // namespace wl::app
