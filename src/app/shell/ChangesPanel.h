#pragma once
// "Değişiklikler", the right panel of the WinLoveUI package's editing screens (D-107, LAYOUT.md
// "Sağ panel 320 — Değişiklikler"): the queue's size, what each page queues (a click opens the
// page), the mounted folder and the full-width Uygula (44). While Uygula runs the bottom shows the
// stage, the percentage, a 4px bar and Durdur. It takes the place of the old status bar's
// "Uygula · n" button.
#include "app/pages/PageInfo.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

class ChangesPanel : public ui::Widget {
public:
    struct Entry {
        PageId page;
        std::wstring label;
        int count = 0;
    };
    struct Labels {
        std::wstring title;     // "Değişiklikler"
        std::wstring empty;     // "Henüz değişiklik yok"
        std::wstring emptyHint; // what the panel collects
        std::wstring mounted;   // "Bağlı"
        std::wstring noMount;   // "Bağlı imaj yok"
        std::wstring apply;     // "Uygula"
        std::wstring stop;      // "Durdur"
    };
    struct Run {
        std::wstring stage;
        float fraction = 0;
    };

    explicit ChangesPanel(Labels labels);
    std::function<void(PageId)> onPage;
    std::function<void()> onApply; // Uygula, or Durdur while it runs
    // `operations`: "23 işlem" (empty: none queued).
    void set(std::vector<Entry> entries, std::wstring operations, std::optional<std::wstring> mountPath,
             std::optional<Run> run, PageId current, bool canApply);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    [[nodiscard]] ui::Cursor cursor() const override;

private:
    [[nodiscard]] float footerTop() const;
    [[nodiscard]] ui::RectF entryRect(std::size_t index) const;

    Labels m_labels;
    std::vector<Entry> m_entries;
    std::wstring m_operations;
    std::optional<std::wstring> m_mountPath;
    std::optional<Run> m_run;
    PageId m_current = PageId::Source;
    int m_hover = -1;
    ui::Button* m_button = nullptr;
};

} // namespace wl::app
