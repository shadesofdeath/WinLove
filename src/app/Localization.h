#pragma once
// UI text lookup. All visible text comes from resources/strings/<lang>.json through a Str key;
// the key enum is generated (tools/gen_strings.py), so a typo is a compile error.
#include "app/generated/StringKeys.g.h"
#include "base/Result.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace wl::app {

enum class Language : std::uint8_t { Turkish, English };

class Localization {
public:
    using Arg = std::pair<std::wstring_view, std::wstring_view>;

    // Parses one language file. Fails if any generated key is missing, or the file has keys the
    // generated enum does not know (i.e. ./build.ps1 -Gen was not run).
    [[nodiscard]] static Result<Localization> fromJson(std::string_view utf8Json);

    [[nodiscard]] const std::wstring& get(Str key) const noexcept;

    // Replaces {name} placeholders: format(Str::CommonItems, {{L"n", L"12"}}) -> "12 öğe".
    // Unknown placeholders are left as-is so they are visible during testing.
    [[nodiscard]] std::wstring format(Str key, std::initializer_list<Arg> args) const;

private:
    std::array<std::wstring, kStrCount> m_values;
};

} // namespace wl::app
