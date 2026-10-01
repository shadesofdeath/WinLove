#pragma once
// UTF-8 is used only at file/JSON boundaries; everything inside WinLove is UTF-16 (std::wstring).
#include <string>
#include <string_view>

namespace wl::utf8 {

[[nodiscard]] std::wstring toWide(std::string_view utf8);
[[nodiscard]] std::string fromWide(std::wstring_view wide);
// Text file bytes → UTF-16: UTF-16 LE BOM, UTF-8 (BOM optional, strict), else the ANSI code page
// (classic .inf / REGEDIT4 files).
[[nodiscard]] std::wstring decodeText(std::string_view bytes);
// Bytes in a Windows code page (CP_ACP, CP_OEMCP, …) → UTF-16.
[[nodiscard]] std::wstring fromCodePage(std::string_view bytes, unsigned codePage);

} // namespace wl::utf8
