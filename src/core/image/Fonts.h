#pragma once
// Fonts into the image: Windows\Fonts\<file> + a value under
// HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts named the way Windows names it:
//   "<full name> (TrueType)"  .ttf / .ttc / .otf with TrueType outlines
//   "<full name> (OpenType)"  .otf with PostScript (CFF) outlines
//   "<a> & <b> (TrueType)"    a collection: every font's full name
// The name comes from the font's own 'name' table (ID 4, full name; Windows / English first).
#include "base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

inline constexpr std::wstring_view kFontsKey = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

struct FontInfo {
    std::vector<std::wstring> names; // full name of each font in the file
    bool postscript = false;         // CFF outlines ("OTTO")
    [[nodiscard]] std::wstring registryName() const;
};

[[nodiscard]] bool isFontFile(const std::filesystem::path& file); // .ttf .otf .ttc by extension
[[nodiscard]] Result<FontInfo> parseFont(std::string_view bytes);
[[nodiscard]] Result<FontInfo> readFontInfo(const std::filesystem::path& file);

// A file name for Windows\Fonts: the source's own name when it is plain (letters, digits, - _ .
// space), otherwise a cleaned one; never a path.
[[nodiscard]] std::wstring fontFileName(const std::filesystem::path& source);

} // namespace wl::core
