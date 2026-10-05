#pragma once
// P11 "Değer ekle / düzenle": one registry value typed by the user ↔ a RegistryWrite.
// The data is typed as text, by type:
//   String / ExpandString  as it is (may be empty)
//   MultiString            parts separated by ";" (";;" is a literal ";")
//   Dword / Qword          decimal, or hexadecimal with "0x"
//   Binary                 hex bytes separated by spaces or commas ("01 0a ff")
//   DeleteValue / DeleteKey  no data; DeleteKey ignores the name
#include "base/Result.h"
#include "core/image/RegistryEdit.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace wl::core {

enum class RegValueType : std::uint8_t { String, ExpandString, MultiString, Dword, Qword, Binary, DeleteValue, DeleteKey };

// ParseError (context = what was wrong) when the key has no known root or the data does not fit
// the type. Whether an image can take the key is the caller's question (mapOfflineKey).
[[nodiscard]] Result<RegistryWrite> registryWriteFromInput(std::wstring_view key, std::wstring_view name, RegValueType type,
                                                           std::wstring_view data);

struct RegValueInput {
    RegValueType type = RegValueType::String;
    std::wstring data;
};
// The other way, for editing. A type the dialog does not offer (REG_NONE, REG_LINK…) shows as Binary.
[[nodiscard]] RegValueInput registryWriteInput(const RegistryWrite& write);

} // namespace wl::core
