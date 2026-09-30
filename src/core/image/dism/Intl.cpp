#include "core/image/dism/Intl.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/dism/DismExe.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::core {

namespace {

bool asciiAlpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}
bool asciiAlnum(wchar_t c) {
    return asciiAlpha(c) || (c >= L'0' && c <= L'9');
}
bool hexDigit(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

} // namespace

std::string intlToJson(const IntlSettings& s) {
    nlohmann::json j = nlohmann::json::object();
    auto put = [&](const char* key, const std::wstring& value) {
        if (!value.empty()) {
            j[key] = utf8::fromWide(value);
        }
    };
    put("ui", s.uiLanguage);
    put("system", s.systemLocale);
    put("user", s.userLocale);
    put("input", s.inputLocale);
    put("timezone", s.timeZone);
    return j.dump();
}

Result<IntlSettings> intlFromJson(std::string_view json) {
    const auto j = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return fail(ErrorCode::ParseError, L"international settings are not a JSON object");
    }
    auto get = [&](const char* key) { return utf8::toWide(j.value(key, std::string{})); };
    IntlSettings s{get("ui"), get("system"), get("user"), get("input"), get("timezone")};
    if (auto ok = validateIntl(s); !ok) {
        return std::unexpected(ok.error());
    }
    return s;
}

bool isLanguageTag(std::wstring_view text) noexcept {
    if (text.size() < 2 || text.size() > 20) {
        return false;
    }
    std::size_t start = 0;
    int part = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(L'-', start), text.size());
        const std::wstring_view p = text.substr(start, end - start);
        if (p.empty() || p.size() > 8 || !std::ranges::all_of(p, asciiAlnum)) {
            return false;
        }
        if (part == 0 && (p.size() < 2 || p.size() > 3 || !std::ranges::all_of(p, asciiAlpha))) {
            return false;
        }
        ++part;
        start = end + 1;
    }
    return true;
}

bool isInputLocale(std::wstring_view text) noexcept {
    const auto colon = text.find(L':');
    if (colon == std::wstring_view::npos) {
        return isLanguageTag(text);
    }
    const auto a = text.substr(0, colon);
    const auto b = text.substr(colon + 1);
    return a.size() == 4 && b.size() == 8 && std::ranges::all_of(a, hexDigit) && std::ranges::all_of(b, hexDigit);
}

bool isTimeZoneId(std::wstring_view text) noexcept {
    return !text.empty() && text.size() <= 64 && std::ranges::all_of(text, [](wchar_t c) {
        return asciiAlnum(c) || c == L' ' || c == L'.' || c == L'+' || c == L'-' || c == L'(' || c == L')';
    });
}

Result<void> validateIntl(const IntlSettings& s) {
    if (!s.uiLanguage.empty() && !isLanguageTag(s.uiLanguage)) {
        return fail(ErrorCode::InvalidArgument, L"not a language tag (UI language)", s.uiLanguage);
    }
    if (!s.systemLocale.empty() && !isLanguageTag(s.systemLocale)) {
        return fail(ErrorCode::InvalidArgument, L"not a language tag (system locale)", s.systemLocale);
    }
    if (!s.userLocale.empty() && !isLanguageTag(s.userLocale)) {
        return fail(ErrorCode::InvalidArgument, L"not a language tag (user locale)", s.userLocale);
    }
    if (!s.inputLocale.empty() && !isInputLocale(s.inputLocale)) {
        return fail(ErrorCode::InvalidArgument, L"not an input locale (keyboard)", s.inputLocale);
    }
    if (!s.timeZone.empty() && !isTimeZoneId(s.timeZone)) {
        return fail(ErrorCode::InvalidArgument, L"not a time zone id", s.timeZone);
    }
    return {};
}

ImageIntl parseIntl(std::string_view output) {
    ImageIntl intl;
    auto first = [&](std::string_view label) {
        auto values = dismExeValues(output, label);
        return values.empty() ? std::wstring() : values.front();
    };
    intl.current.uiLanguage = first("Default system UI language");
    intl.current.systemLocale = first("System locale");
    intl.current.userLocale = first("User locale for default user");
    intl.current.timeZone = first("Default time zone");
    // "Active keyboard(s) : 041f:0000041f, 0409:00000409" — the first is the default.
    std::wstring keyboards = first("Active keyboard(s)");
    intl.current.inputLocale = keyboards.substr(0, keyboards.find(L','));
    for (auto& lang : dismExeValues(output, "Installed language(s)")) {
        if (isLanguageTag(lang) && std::ranges::find(intl.languages, lang) == intl.languages.end()) {
            intl.languages.push_back(lang);
        }
    }
    return intl;
}

std::wstring intlArguments(const IntlSettings& s) {
    std::wstring args;
    auto add = [&](std::wstring_view option, const std::wstring& value, bool quote) {
        if (value.empty()) {
            return;
        }
        if (!args.empty()) {
            args += L' ';
        }
        args += quote ? std::format(L"{}:\"{}\"", option, value) : std::format(L"{}:{}", option, value);
    };
    add(L"/Set-UILang", s.uiLanguage, false);
    add(L"/Set-SysLocale", s.systemLocale, false);
    add(L"/Set-UserLocale", s.userLocale, false);
    add(L"/Set-InputLocale", s.inputLocale, false);
    add(L"/Set-TimeZone", s.timeZone, true);
    return args;
}

Result<ImageIntl> readIntl(DismSession& session) {
    const auto run = runDismExe(session, L"/Get-Intl");
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismExeFailure(*run, L"could not read the international settings of the image"));
    }
    ImageIntl intl = parseIntl(run->output);
    log::info("dism", std::format(L"intl: UI {}, system {}, user {}, keyboard {}, time zone {}, {} language(s)",
                                  intl.current.uiLanguage, intl.current.systemLocale, intl.current.userLocale,
                                  intl.current.inputLocale, intl.current.timeZone, intl.languages.size()));
    return intl;
}

Result<void> setIntl(DismSession& session, const IntlSettings& settings, const TaskContext& task) {
    if (auto ok = validateIntl(settings); !ok) {
        return ok;
    }
    const std::wstring args = intlArguments(settings);
    if (args.empty()) {
        return {};
    }
    task.report(-1.0, L"intl");
    const auto run = runDismExe(session, args);
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismExeFailure(*run, L"the international settings could not be changed"));
    }
    task.report(1.0, L"intl");
    return {};
}

} // namespace wl::core
