#pragma once
// D-053: the international settings of an offline image — default UI language, system locale,
// user locale of the default profile, keyboard, time zone — and the languages it has.
// The DISM API has no call for these; Windows' own dism.exe does them offline:
//   dism.exe /Image:<mount> /Get-Intl
//   dism.exe /Image:<mount> /Set-UILang:tr-TR /Set-SysLocale:tr-TR /Set-UserLocale:tr-TR
//            /Set-InputLocale:041f:0000041f /Set-TimeZone:"Turkey Standard Time"
// A UI language must be installed in the image first (language pack, LanguagePacks.h): packages
// are added in the Updates phase, the settings in the Settings phase, so one run can do both.
// In the queue: one SetIntl operation, target "intl", value = JSON (empty fields = unchanged).
#include "core/image/dism/Dism.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct IntlSettings {
    std::wstring uiLanguage;   // "tr-TR"
    std::wstring systemLocale; // "tr-TR"
    std::wstring userLocale;   // "tr-TR"
    std::wstring inputLocale;  // "041f:0000041f" (or a language tag DISM maps to its default keyboard)
    std::wstring timeZone;     // "Turkey Standard Time"

    [[nodiscard]] bool empty() const noexcept {
        return uiLanguage.empty() && systemLocale.empty() && userLocale.empty() && inputLocale.empty() && timeZone.empty();
    }
    [[nodiscard]] bool operator==(const IntlSettings&) const = default;
};

struct ImageIntl {
    IntlSettings current;
    std::vector<std::wstring> languages; // installed, "tr-TR"
};

[[nodiscard]] std::string intlToJson(const IntlSettings& settings);
[[nodiscard]] Result<IntlSettings> intlFromJson(std::string_view json);

// "tr-TR", "sr-Latn-RS", "zh-Hans-CN": what /Set-UILang and the locales take.
[[nodiscard]] bool isLanguageTag(std::wstring_view text) noexcept;
// "041f:0000041f" (language:keyboard, hex) or a language tag.
[[nodiscard]] bool isInputLocale(std::wstring_view text) noexcept;
// A Windows time zone id: letters, digits, spaces and . + - ( ).
[[nodiscard]] bool isTimeZoneId(std::wstring_view text) noexcept;
// Every field valid (or empty); the error names the field.
[[nodiscard]] Result<void> validateIntl(const IntlSettings& settings);

// From /English /Get-Intl output.
[[nodiscard]] ImageIntl parseIntl(std::string_view output);
// The dism.exe arguments of `settings` ("/Set-UILang:tr-TR /Set-TimeZone:\"…\""); empty when nothing is set.
[[nodiscard]] std::wstring intlArguments(const IntlSettings& settings);

[[nodiscard]] Result<ImageIntl> readIntl(DismSession& session);
[[nodiscard]] Result<void> setIntl(DismSession& session, const IntlSettings& settings, const TaskContext& task);

} // namespace wl::core
