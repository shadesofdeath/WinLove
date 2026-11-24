#pragma once
// InfoBar per infobar-toast-dialog.md: kHeight tall, status.*Subtle background, icon 16 in the
// status color, bodyStrong title + body message (single line, ellipsis), close button on the right.
#include "ui/widgets/Button.h"

#include <functional>
#include <string>

namespace wl::ui {

enum class InfoKind : std::uint8_t { Info, Success, Warning, Error };

// What a status kind looks like (InfoBar, Toast): subtle background, ink, icon.
struct InfoStyle {
    tokens::Color background;
    tokens::Color ink;
    icons::Icon icon;
};
[[nodiscard]] InfoStyle infoStyle(InfoKind kind) noexcept;

class InfoBar : public Widget {
public:
    InfoBar(InfoKind kind, std::wstring title, std::wstring message, std::wstring closeTooltip);
    std::function<void()> onClose;

    // Optional text action left of the close button ("Mount'ları temizle"); empty label hides it.
    void setAction(std::wstring label, std::function<void()> onInvoke);

    void set(InfoKind kind, std::wstring title, std::wstring message);
    void appendBody(std::wstring_view more) {
        m_message += more;
        invalidate();
    }
    // D-136: one height on every page — its close button (size::control) with a spacing step around.
    static constexpr float kHeight = tokens::size::control + 2 * tokens::spacing::s2;
    [[nodiscard]] SizeF measure(SizeF available) override { return {available.width, kHeight}; }
    void layout() override;
    void paint(Canvas& canvas) override;

private:
    InfoKind m_kind;
    std::wstring m_title;
    std::wstring m_message;
    Button* m_close = nullptr;
    Button* m_action = nullptr;
};

} // namespace wl::ui
