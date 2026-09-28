#pragma once
#include <cstdint>
#include <string_view>

namespace wl::core {

// Kinds of source the user can open (docs/ARCHITECTURE.md §2.2).
enum class ImageFormat : std::uint8_t {
    Unknown,
    Iso,
    Wim,
    Esd,
    Swm,
    Vhd,
    Vhdx,
};

// Classifies by file extension only (case-insensitive). Content sniffing is done by the backend
// when the file is actually opened.
[[nodiscard]] ImageFormat formatFromPath(std::wstring_view path) noexcept;

[[nodiscard]] const wchar_t* formatName(ImageFormat format) noexcept;

} // namespace wl::core
