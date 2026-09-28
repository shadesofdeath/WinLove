#include "app/catalog/TweakCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

namespace wl::app {

namespace {
core::ops::Risk riskFrom(const std::string& text) {
    return text == "high" ? core::ops::Risk::High : text == "medium" ? core::ops::Risk::Medium : core::ops::Risk::Low;
}
std::wstring wide(const nlohmann::json& j, const char* key) {
    return utf8::toWide(j.value(key, std::string{}));
}
} // namespace

Result<TweakCatalog> TweakCatalog::parse(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.tweaks") {
        return fail(ErrorCode::ParseError, L"not a tweak catalog", L"tweaks.json");
    }
    TweakCatalog catalog;
    for (const auto& c : doc.value("categories", nlohmann::json::array())) {
        catalog.m_categories.push_back({c.value("id", std::string{}), wide(c, "desc_tr"), wide(c, "desc_en")});
    }
    for (const auto& t : doc.value("tweaks", nlohmann::json::array())) {
        Tweak tweak{t.value("id", std::string{}), t.value("category", std::string{}), wide(t, "tr"), wide(t, "en"),
                    riskFrom(t.value("risk", std::string{"low"})), t.value("recommended", false), {}};
        tweak.firstLogon = t.value("apply", std::string{}) == "firstLogon";
        bool valid = true;
        for (const auto& w : t.value("writes", nlohmann::json::array())) {
            auto write = core::parseRegValue(wide(w, "key"), wide(w, "name"), wide(w, "value"));
            if (!write) {
                log::warn("app", L"tweak " + utf8::toWide(tweak.id) + L": " + describe(write.error()));
                valid = false;
                break;
            }
            tweak.writes.push_back(std::move(*write));
        }
        if (valid && !tweak.writes.empty()) {
            catalog.m_tweaks.push_back(std::move(tweak));
        }
    }
    return catalog;
}

} // namespace wl::app
