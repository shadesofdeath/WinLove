#pragma once
// Status bar per statusbar-titlebar.md (24px): mount state segment on the left, the app-wide
// "Uygula · n" CTA (20px primary) on the right. The CTA is disabled while the queue is empty
// and then uses its own flat look (bg.raised + text.disabled), not the generic 0.45 dimming.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace wl::app {

class ApplyCta : public ui::Widget {
public:
    explicit ApplyCta(std::wstring label);
    std::function<void()> onInvoke;

    void setQueue(int count);
    // Replaces "Uygula · n" (e.g. "Durdur" while applying); empty = back to the queue label.
    void setOverride(std::wstring label);
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
    std::wstring m_override;
    ui::Tween m_hover;
};

// D-091: where the work stands, left on the status bar — Kaynak › Bağla › Düzenle · n › Uygula › ISO.
// Done steps carry a check, the current one the accent; a click opens the step's page.
class WorkflowSteps : public ui::Widget {
public:
    static constexpr int kSteps = 5;
    enum class State : std::uint8_t { Pending, Current, Done };
    explicit WorkflowSteps(std::array<std::wstring, kSteps> labels);
    std::function<void(int step)> onStep;

    void setStates(std::array<State, kSteps> states, int editCount);
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;

private:
    [[nodiscard]] std::wstring label(int step) const;
    [[nodiscard]] float stepWidth(int step) const;
    std::array<std::wstring, kSteps> m_labels;
    std::array<State, kSteps> m_states{};
    int m_editCount = 0;
    int m_hover = -1;
};

class StatusBar : public ui::Widget {
public:
    struct Labels {
        std::wstring noMount;   // "Bağlı imaj yok"
        std::wstring mounted;   // "Mounted"
        std::wstring image;     // "İmaj"
        std::wstring apply;     // "Uygula"
        std::array<std::wstring, WorkflowSteps::kSteps> steps; // "Kaynak", "Bağla", "Düzenle", "Uygula", "ISO"
    };
    explicit StatusBar(Labels labels);

    ApplyCta& cta() { return *m_cta; }
    WorkflowSteps& steps() { return *m_steps; }
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
    WorkflowSteps* m_steps = nullptr;
};

} // namespace wl::app
