#include "app/catalog/ImageSettingsCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>

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

// "writes" and "services" of a toggle or of one option. False when an entry is malformed.
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
            why = L"toggle without writes or services";
            return false;
        }
        const auto recommended = j.find("recommended");
        setting.recommended = recommended != j.end() && recommended->is_boolean() && recommended->get<bool>() ? other : -1;
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
