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

} // namespace wl::utf8
