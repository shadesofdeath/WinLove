#include "app/catalog/AppxCatalog.h"

#include "base/Utf8.h"

#include <json.hpp>

#include <windows.h>

namespace wl::app {

namespace {
core::ops::Risk riskFrom(const std::string& text) {
    if (text == "low") {
        return core::ops::Risk::Low;
    }
    if (text == "high") {
        return core::ops::Risk::High;
    }
    return core::ops::Risk::Medium;
}
} // namespace

Result<AppxCatalog> AppxCatalog::parse(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.appx") {
        return fail(ErrorCode::ParseError, L"not an AppX catalog", L"appx.json");
    }
    AppxCatalog catalog;
    for (const auto& g : doc.value("groups", nlohmann::json::array())) {
        catalog.m_groups.push_back({g.value("id", std::string{}), utf8::toWide(g.value("tr", std::string{})),
                                    utf8::toWide(g.value("en", std::string{}))});
    }
    for (const auto& a : doc.value("apps", nlohmann::json::array())) {
        catalog.m_entries.push_back({utf8::toWide(a.value("match", std::string{})), utf8::toWide(a.value("tr", std::string{})),
                                     utf8::toWide(a.value("en", std::string{})), a.value("group", std::string{"other"}),
                                     riskFrom(a.value("risk", std::string{"medium"})),
                                     utf8::toWide(a.value("notes_tr", std::string{})),
                                     utf8::toWide(a.value("notes_en", std::string{})), a.value("lockedSince", 0)});
    }
    if (catalog.groupIndex("other") < 0 || catalog.m_groups.empty() || catalog.m_groups.back().id != "other") {
        catalog.m_groups.push_back({"other", L"Diğer Uygulamalar", L"Other apps"});
    }
    return catalog;
}

const AppxCatalogEntry* AppxCatalog::find(std::wstring_view identity) const {
    const AppxCatalogEntry* best = nullptr;
    for (const auto& e : m_entries) {
        if (e.match.size() <= identity.size() &&
            CompareStringOrdinal(identity.data(), static_cast<int>(e.match.size()), e.match.data(),
                                 static_cast<int>(e.match.size()), TRUE) == CSTR_EQUAL &&
            (!best || e.match.size() > best->match.size())) {
            best = &e;
        }
    }
    return best;
}

int AppxCatalog::groupIndex(std::string_view id) const {
    for (std::size_t i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace wl::app
