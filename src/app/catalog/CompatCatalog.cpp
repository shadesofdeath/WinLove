#include "app/catalog/CompatCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

namespace wl::app {

namespace {

std::wstring textOf(const nlohmann::json& entry, const char* key) {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? utf8::toWide(it->get<std::string>()) : std::wstring();
}

std::vector<std::wstring> listOf(const nlohmann::json& entry, const char* key) {
    std::vector<std::wstring> out;
    const auto it = entry.find(key);
    if (it == entry.end() || !it->is_array()) {
        return out;
    }
    for (const auto& item : *it) {
        if (item.is_string() && !item.get<std::string>().empty()) {
            out.push_back(utf8::toWide(item.get<std::string>()));
        }
    }
    return out;
}

} // namespace

Result<CompatCatalog> CompatCatalog::parse(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.compat") {
        return fail(ErrorCode::ParseError, L"not a compatibility catalog", L"compat.json");
    }
    CompatCatalog catalog;
    for (const auto& entry : doc.value("guards", nlohmann::json::array())) {
        if (!entry.is_object()) {
            continue;
        }
        CompatCatalogEntry guard;
        guard.rule.id = textOf(entry, "id");
        guard.rule.appx = listOf(entry, "appx");
        guard.rule.components = listOf(entry, "components");
        guard.rule.services = listOf(entry, "services");
        guard.nameTr = textOf(entry, "tr");
        guard.nameEn = textOf(entry, "en");
        guard.descriptionTr = textOf(entry, "description_tr");
        guard.descriptionEn = textOf(entry, "description_en");
        guard.defaultOn = entry.value("default", false);
        if (guard.rule.id.empty() || guard.nameTr.empty() || guard.nameEn.empty() ||
            (guard.rule.appx.empty() && guard.rule.components.empty() && guard.rule.services.empty()) ||
            catalog.find(guard.rule.id)) {
            log::warn("app", L"compat.json: a guard without an id, a name or anything to keep was skipped: " + guard.rule.id);
            continue;
        }
        catalog.m_guards.push_back(std::move(guard));
    }
    return catalog;
}

const CompatCatalogEntry* CompatCatalog::find(std::wstring_view id) const {
    for (const auto& guard : m_guards) {
        if (guard.rule.id == id) {
            return &guard;
        }
    }
    return nullptr;
}

std::vector<std::wstring> CompatCatalog::defaults() const {
    std::vector<std::wstring> out;
    for (const auto& guard : m_guards) {
        if (guard.defaultOn) {
            out.push_back(guard.rule.id);
        }
    }
    return out;
}

} // namespace wl::app
