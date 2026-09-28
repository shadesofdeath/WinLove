#pragma once
// Plain-text clipboard (CF_UNICODETEXT). Used by text boxes and the log console (Ctrl+C / Ctrl+V).
#include <string>
#include <string_view>

namespace wl::ui {

bool setClipboardText(std::wstring_view text);
[[nodiscard]] std::wstring clipboardText();

} // namespace wl::ui
