#pragma once
// Status bar of the WinLoveUI package (D-107, LAYOUT.md "Durum çubuğu"): 30px on the panel color,
// a 1px line on top, Caption text.secondary, 20 at the sides. Left: a 7px dot (accent = mounted)
// and "Bağlı · Windows 11 Pro · D:\WinLove\mount" or "Bağlı imaj yok"; then the running task (label,
// 80×4 bar, percent); right: the host facts ("DISM 10.0.26100.1   Boş alan D: 182 GB").
#include "ui/widget/Widget.h"

#include <optional>
#include <string>

namespace wl::app {

class StatusBar : public ui::Widget {
public:
    struct Labels {
        std::wstring noMount; // "Bağlı imaj yok"
        std::wstring mounted; // "Bağlı"
    };
    explicit StatusBar(Labels labels);
    // nullopt = nothing mounted; else the edition and the mount folder.
    void setMount(std::optional<std::wstring> edition, std::wstring mountPath);
    // Background task: label + bar + percent; nullopt hides it.
    void setTask(std::optional<std::wstring> label, float fraction);
    // Right-aligned facts about this PC.
    void setInfo(std::wstring info);
    void paint(ui::Canvas& canvas) override;

private:
    Labels m_labels;
    std::optional<std::wstring> m_edition;
    std::wstring m_mountPath;
    std::optional<std::wstring> m_task;
    float m_taskFraction = 0;
    std::wstring m_info;
};

} // namespace wl::app
