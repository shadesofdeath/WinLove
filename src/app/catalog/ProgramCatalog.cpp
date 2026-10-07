#include "app/catalog/ProgramCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/programs/Winget.h"

#include <json.hpp>

namespace wl::app {

namespace {

// The winget ids of a "programs" list; what could not go on a command line is logged and skipped.
std::vector<std::wstring> idsOf(const nlohmann::json& entry) {
    std::vector<std::wstring> out;
    for (const auto& id : entry.value("programs", nlohmann::json::array())) {
        const std::wstring text = id.is_string() ? utf8::toWide(id.get<std::string>()) : std::wstring();
        if (core::validWingetId(text)) {
            out.push_back(text);
        } else {
            log::warn("app", L"programs.json: not a winget id: " + text);
        }
    }
    return out;
}

std::wstring textOf(const nlohmann::json& entry, const char* key) {
    return utf8::toWide(entry.value(key, std::string{}));
}

} // namespace

Result<ProgramCatalog> ProgramCatalog::parse(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.programs") {
        return fail(ErrorCode::ParseError, L"not a programs catalog", L"programs.json");
    }
    ProgramCatalog catalog;
    for (const auto& entry : doc.value("categories", nlohmann::json::array())) {
        if (!entry.is_object()) {
            continue;
        }
        ProgramCategory category;
        category.id = entry.value("id", std::string{});
        category.nameTr = utf8::toWide(entry.value("tr", std::string{}));
        category.nameEn = utf8::toWide(entry.value("en", std::string{}));
        if (category.id.empty() || category.nameTr.empty() || category.nameEn.empty()) {
            log::warn("app", L"programs.json: a category without an id or a name was skipped");
            continue;
        }
        category.programs = idsOf(entry);
        for (const auto& tag : entry.value("tags", nlohmann::json::array())) {
            if (tag.is_string() && !tag.get<std::string>().empty()) {
                category.tags.push_back(utf8::toWide(tag.get<std::string>()));
            }
        }
        catalog.m_categories.push_back(std::move(category));
    }
    for (const auto& entry : doc.value("bundles", nlohmann::json::array())) {
        if (!entry.is_object()) {
            continue;
        }
        ProgramBundle bundle{entry.value("id", std::string{}), textOf(entry, "tr"), textOf(entry, "en"), textOf(entry, "desc_tr"),
                             textOf(entry, "desc_en"), idsOf(entry)};
        if (bundle.id.empty() || bundle.nameTr.empty() || bundle.nameEn.empty() || bundle.programs.empty()) {
            log::warn("app", L"programs.json: a bundle without an id, a name or programs was skipped");
            continue;
        }
        catalog.m_bundles.push_back(std::move(bundle));
    }
    return catalog;
}

std::size_t ProgramCatalog::size() const noexcept {
    std::size_t n = 0;
    for (const auto& category : m_categories) {
        n += category.programs.size();
    }
    return n;
}

} // namespace wl::app
