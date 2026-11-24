#pragma once
// D-149: a file out of a cabinet (.cab) held in memory, with Windows' own FDI (cabinet.dll) — no
// expand.exe, no temporary file. What the official ESD catalog needs: products.cab → products.xml
// (one file of a few MB).
#include "base/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// The names in the cabinet, as stored.
[[nodiscard]] Result<std::vector<std::string>> cabinetNames(std::span<const std::uint8_t> cab);
// The bytes of the file `name` (case-insensitive; "products.xml"). NotFound when it is not in it;
// `maxBytes` caps what is kept (a larger file is an error).
[[nodiscard]] Result<std::string> cabinetFile(std::span<const std::uint8_t> cab, std::string_view name,
                                              std::size_t maxBytes = 64u << 20);

} // namespace wl::core
