#pragma once
// UTF-8 is used only at file/JSON boundaries; everything inside WinLove is UTF-16 (std::wstring).
#include <string>
#include <string_view>

namespace wl::utf8 {

[[nodiscard]] std::wstring toWide(std::string_view utf8);
[[nodiscard]] std::string fromWide(std::wstring_view wide);

} // namespace wl::utf8
