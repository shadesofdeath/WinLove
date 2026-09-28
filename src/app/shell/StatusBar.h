#pragma once
// Status bar per statusbar-titlebar.md (24px): mount state segment on the left, the app-wide
// "Uygula · n" CTA (20px primary) on the right. The CTA is disabled while the queue is empty
// and then uses its own flat look (bg.raised + text.disabled), not the generic 0.45 dimming.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>

namespace wl::app {

class ApplyCta : public ui::Widget {
public:
    explicit ApplyCta(std::wstring label);
    std::function<void()> onInvoke;

    void setQueue(int count);
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool tick(double now) override;

private:
    [[nodiscard]] std::wstring text() const;
    std::wstring m_label;
    int m_queue = 0;
    ui::Tween m_hover;
};

class StatusBar : public ui::Widget {
public:
    struct Labels {
        std::wstring noMount;   // "Bağlı imaj yok"
        std::wstring mounted;   // "Mounted"
        std::wstring image;     // "İmaj"
        std::wstring apply;     // "Uygula"
    };
    explicit StatusBar(Labels labels);

    ApplyCta& cta() { return *m_cta; }
    // Mount segment: nullopt = nothing mounted. `size` is preformatted ("4,80 GB").
    void setMount(std::optional<std::wstring> mountPath, std::wstring size);
    // Background task segment right of the segments: label + 80×2 bar + percent; nullopt hides it.
    void setTask(std::optional<std::wstring> label, float fraction);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    Labels m_labels;
    std::optional<std::wstring> m_mountPath;
    std::wstring m_size;
    std::optional<std::wstring> m_task;
    float m_taskFraction = 0;
    ApplyCta* m_cta = nullptr;
};

} // namespace wl::app
