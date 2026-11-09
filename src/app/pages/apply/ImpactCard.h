#pragma once
// D-124 (the user's choice, "what the reference tool lacks"): Uygula's impact report. Before a run, what the
// queue will break in plain words — each feature a compatibility guard that is off would keep
// working (Printing, Wi-Fi and Bluetooth, Office …) with the steps that break it, and a button that
// switches that guard on (those steps leave the queue). When no known feature breaks, a green line.
#include "app/Localization.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class ImpactCard : public ui::Widget {
public:
    struct Row {
        std::wstring guard; // compat.json id
        std::wstring name;
        std::wstring items; // the steps, joined
        bool app = false;   // an app's needs (Office, Discord …), not a Windows feature
    };
    static constexpr int kMaxRows = 6;

    // `rows`: every affected feature; the first kMaxRows are listed, the rest counted.
    ImpactCard(const Localization& strings, std::vector<Row> rows);

    std::function<void(const std::wstring& guard)> onKeep; // "Korumayı aç"
    std::function<void()> onMore;                         // the Uyumluluk dialog

    [[nodiscard]] float preferredHeight() const;
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    const Localization& m_strings;
    std::vector<Row> m_rows;
    std::vector<ui::Button*> m_keep; // one per listed row
    ui::Button* m_more = nullptr;
};

} // namespace wl::app
