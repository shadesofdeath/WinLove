#pragma once
// Keyboard shortcut keycaps ("Ctrl" "K"): mono 10/14, 1px line.strong, r1, padding 0 3, gap 2.
#include "ui/widget/Widget.h"

#include <string>
#include <vector>

namespace wl::ui {

class Kbd : public Widget {
public:
    explicit Kbd(std::vector<std::wstring> keys);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;

    // Shared by widgets that draw keycaps inline (title bar trigger, nav footer).
    static float paintKeys(Canvas& canvas, const std::vector<std::wstring>& keys, float right, float centerY);
    static float keysWidth(const TextStyles& text, const std::vector<std::wstring>& keys);

private:
    std::vector<std::wstring> m_keys;
};

} // namespace wl::ui
