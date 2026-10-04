#include "app/catalog/ImageSettingsCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/ImageFiles.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <format>
#include <sstream>

namespace wl::app {

namespace {

using Json = nlohmann::json;

std::wstring wide(const Json& j, const char* key) {
    return utf8::toWide(j.value(key, std::string{}));
}

LocalizedText text(const Json& j, const char* tr, const char* en) {
    return {wide(j, tr), wide(j, en)};
}

core::ops::Risk riskFrom(const std::string& value) {
    return value == "high" ? core::ops::Risk::High : value == "medium" ? core::ops::Risk::Medium : core::ops::Risk::Low;
}

// "writes", "services" and "files" of a toggle or of one option. False when an entry is malformed.
bool readOperations(const Json& j, ImageSettingOption& option, std::wstring& why) {
    for (const auto& w : j.value("writes", Json::array())) {
        auto write = core::parseRegValue(wide(w, "key"), wide(w, "name"), wide(w, "value"));
        if (!write) {
            why = describe(write.error());
            return false;
        }
        option.writes.push_back(std::move(*write));
    }
    for (const auto& s : j.value("services", Json::array())) {
        const auto start = core::startTypeFromKey(wide(s, "start"));
        const std::wstring name = wide(s, "name");
        if (!start || name.empty()) {
            why = L"bad service entry";
            return false;
        }
        option.services.emplace_back(name, *start);
    }
    for (const auto& f : j.value("files", Json::array())) {
        const std::wstring path = wide(f, "path");
        const std::string content = f.value("content", std::string{});
        if (auto ok = core::validateImageFile(path, content.size()); !ok) {
            why = describe(ok.error());
            return false;
        }
        option.files.emplace_back(path, utf8::toWide(content));
    }
    return true;
}

// Fills `setting.options`, defaultOption and recommended. False (with `why`) when malformed.
bool readOptions(const Json& j, ImageSetting& setting, std::wstring& why) {
    if (setting.control == ImageSetting::Control::Toggle) {
        const std::string natural = j.value("default", std::string{});
        if (natural != "on" && natural != "off") {
            why = L"toggle needs \"default\": \"on\" | \"off\"";
            return false;
        }
        setting.options = {ImageSettingOption{"off", {}, {}, {}}, ImageSettingOption{"on", {}, {}, {}}};
        setting.defaultOption = natural == "on" ? 1 : 0;
        const int other = 1 - setting.defaultOption;
        if (!readOperations(j, setting.options[static_cast<std::size_t>(other)], why)) {
            return false;
        }
        if (setting.options[static_cast<std::size_t>(other)].isDefault()) {
            why = L"toggle without writes, services or files";
            return false;
        }
        const auto recommended = j.find("recommended");
        setting.recommended = recommended != j.end() && recommended->is_boolean() && recommended->get<bool>() ? other : -1;
        return true;
    }
    if (setting.control == ImageSetting::Control::Text || setting.control == ImageSetting::Control::File) {
        setting.options = {ImageSettingOption{"default", {}, {}, {}}, ImageSettingOption{"value", {}, {}, {}}};
        setting.defaultOption = 0;
        auto& value = setting.options[1];
        if (setting.control == ImageSetting::Control::Text) {
            const std::string format = j.value("format", std::string{});
            if (format == "pagefile") {
                setting.format = ImageSetting::Format::Pagefile;
            } else if (!format.empty()) {
                why = L"unknown text format";
                return false;
            }
            // Key and name only: the data is what the user types (a page file entry: REG_MULTI_SZ).
            const wchar_t* empty = setting.format == ImageSetting::Format::Pagefile ? L"hex(7):00,00" : L"\"\"";
            for (const auto& w : j.value("writes", Json::array())) {
                auto write = core::parseRegValue(wide(w, "key"), wide(w, "name"), empty);
                if (!write) {
                    why = describe(write.error());
                    return false;
                }
                value.writes.push_back(std::move(*write));
            }
            if (value.writes.empty()) {
                why = L"text setting without a value to write";
                return false;
            }
            return true;
        }
        if (!readOperations(j, value, why)) {
            return false;
        }
        value.copyTo = wide(j, "copy");
        if (auto ok = core::validateImageFile(value.copyTo, 0); !ok) {
            why = describe(ok.error());
            return false;
        }
        return true;
    }
    for (const auto& o : j.value("options", Json::array())) {
        ImageSettingOption option{o.value("id", std::string{}), text(o, "tr", "en"), {}, {}};
        if (!readOperations(o, option, why)) {
            return false;
        }
        setting.options.push_back(std::move(option));
    }
    const auto defaults = std::ranges::count_if(setting.options, &ImageSettingOption::isDefault);
    if (setting.options.size() < 2 || defaults != 1) {
        why = L"needs two or more options, exactly one without writes (the Windows default)";
        return false;
    }
    setting.defaultOption = static_cast<int>(std::ranges::find_if(setting.options, &ImageSettingOption::isDefault) -
                                             setting.options.begin());
    const auto recommended = j.find("recommended");
    if (recommended != j.end() && recommended->is_string()) {
        const auto it = std::ranges::find(setting.options, recommended->get<std::string>(), &ImageSettingOption::id);
        if (it == setting.options.end()) {
            why = L"recommended option does not exist";
            return false;
        }
        setting.recommended = static_cast<int>(it - setting.options.begin());
    }
    return true;
}

} // namespace

std::optional<std::wstring> pagefileEntry(std::wstring_view typed) {
    std::wstringstream in{std::wstring(typed)};
    std::wstring drive;
    in >> drive;
    if (drive.size() == 2 && drive[1] == L':') {
        drive.pop_back();
    }
    if (drive.size() != 1 || !std::iswalpha(drive[0])) {
        return std::nullopt;
    }
    const wchar_t letter = static_cast<wchar_t>(std::towupper(drive[0]));
    std::wstring rest;
    std::vector<std::uint64_t> sizes;
    for (std::wstring word; in >> word;) {
        if (word.empty() || word.size() > 7 || !std::ranges::all_of(word, [](wchar_t c) { return std::iswdigit(c) != 0; })) {
            return std::nullopt;
        }
        sizes.push_back(std::stoull(word));
    }
    if (sizes.empty()) {
        return std::format(L"{}:\\pagefile.sys 0 0", letter);
    }
    constexpr std::uint64_t kMinMb = 16, kMaxMb = 1024 * 1024;
    if (sizes.size() != 2 || sizes[0] < kMinMb || sizes[1] > kMaxMb || sizes[0] > sizes[1]) {
        return std::nullopt;
    }
    return std::format(L"{}:\\pagefile.sys {} {}", letter, sizes[0], sizes[1]);
}

std::wstring pagefileTyped(std::wstring_view entry) {
    // "D:\pagefile.sys 4096 8192"
    if (entry.size() < 15 || entry[1] != L':' || _wcsnicmp(entry.data() + 2, L"\\pagefile.sys", 13) != 0) {
        return {};
    }
    std::wstring sizes(entry.substr(15));
    while (!sizes.empty() && sizes.front() == L' ') {
        sizes.erase(sizes.begin());
    }
    const std::wstring drive = std::wstring(1, entry[0]) + L":";
    return sizes.empty() || sizes == L"0 0" ? drive : drive + L" " + sizes;
}

Result<ImageSettingsCatalog> ImageSettingsCatalog::parse(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.settings") {
        return fail(ErrorCode::ParseError, L"not a settings catalog", L"settings.json");
    }
    ImageSettingsCatalog catalog;
    // value() throws on a wrongly typed field; the catalog is ours, but a bad edit must not crash.
    try {
        for (const auto& t : doc.value("tabs", Json::array())) {
            catalog.m_tabs.push_back({t.value("id", std::string{}), text(t, "tr", "en")});
        }
        for (const auto& s : doc.value("sections", Json::array())) {
            catalog.m_sections.push_back({s.value("id", std::string{}), s.value("tab", std::string{}), text(s, "tr", "en")});
        }
        for (const auto& j : doc.value("settings", Json::array())) {
            ImageSetting setting;
            setting.id = j.value("id", std::string{});
            setting.section = j.value("section", std::string{});
            const std::string control = j.value("control", std::string{"toggle"});
            setting.control = control == "dropdown" ? ImageSetting::Control::Dropdown
                              : control == "radio"  ? ImageSetting::Control::Radio
                              : control == "text"   ? ImageSetting::Control::Text
                              : control == "file"   ? ImageSetting::Control::File
                                                    : ImageSetting::Control::Toggle;
            setting.label = text(j, "tr", "en");
            setting.hint = text(j, "hint_tr", "hint_en");
            setting.risk = riskFrom(j.value("risk", std::string{"low"}));
            setting.firstLogon = j.value("apply", std::string{}) == "firstLogon";
            std::wstring why;
            const bool knownSection =
                std::ranges::find(catalog.m_sections, setting.section, &ImageSettingSection::id) != catalog.m_sections.end();
            if (!knownSection) {
                why = L"unknown section";
            }
            if (!knownSection || !readOptions(j, setting, why)) {
                log::warn("app", L"setting " + utf8::toWide(setting.id) + L": " + why);
                continue;
            }
            catalog.m_settings.push_back(std::move(setting));
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed settings catalog", utf8::toWide(e.what()));
    }
    return catalog;
}

} // namespace wl::app
