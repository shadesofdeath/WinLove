#pragma once
// InfoBar per infobar-toast-dialog.md: 32px, status.*Subtle background, 1px line.subtle above and
// below, icon 16 in the status color, bodyStrong title + body message (single line, ellipsis),
// close button on the right.
#include "ui/widgets/Button.h"

#include <functional>
#include <string>

namespace wl::ui {

enum class InfoKind : std::uint8_t { Info, Success, Warning, Error };

class InfoBar : public Widget {
public:
    InfoBar(InfoKind kind, std::wstring title, std::wstring message, std::wstring closeTooltip);
    std::function<void()> onClose;

    void set(InfoKind kind, std::wstring title, std::wstring message);
    [[nodiscard]] SizeF measure(SizeF available) override { return {available.width, 32.0f}; }
    void layout() override;
    void paint(Canvas& canvas) override;

private:
    InfoKind m_kind;
    std::wstring m_title;
    std::wstring m_message;
    Button* m_close = nullptr;
};

} // namespace wl::ui
