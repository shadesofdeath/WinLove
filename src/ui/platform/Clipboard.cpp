#include "ui/platform/Clipboard.h"

#include <windows.h>

#include <cstring>
#include <cwchar>

namespace wl::ui {

bool setClipboardText(std::wstring_view text) {
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (memory) {
        if (auto* target = static_cast<wchar_t*>(GlobalLock(memory))) {
            std::memcpy(target, text.data(), text.size() * sizeof(wchar_t));
            target[text.size()] = L'\0';
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok) {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring clipboardText() {
    std::wstring text;
    if (!OpenClipboard(nullptr)) {
        return text;
    }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* source = static_cast<const wchar_t*>(GlobalLock(data))) {
            // The clipboard is outside input: another program may have put the text there without
            // its terminator, so the block's size bounds the read (audit UI-11).
            const std::size_t capacity = GlobalSize(data) / sizeof(wchar_t);
            text.assign(source, wcsnlen(source, capacity));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

} // namespace wl::ui
