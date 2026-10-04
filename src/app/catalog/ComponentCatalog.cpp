#include "app/catalog/ComponentCatalog.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>

namespace wl::app {

namespace {

using Json = nlohmann::json;

core::ops::Risk riskFrom(const std::string& text) {
    return text == "low" ? core::ops::Risk::Low : text == "high" ? core::ops::Risk::High : core::ops::Risk::Medium;
}

std::wstring wide(const Json& j, const char* key) {
    return utf8::toWide(j.value(key, std::string{}));
}

// One "registry" entry: {key, delete:true} | {key, name, value} (value in .reg syntax).
Result<core::RegistryWrite> writeFrom(const Json& j) {
    if (j.value("delete", false)) {
        core::RegistryWrite write;
        write.kind = core::RegistryWrite::Kind::DeleteKey;
        write.key = core::normalizeRegistryKey(wide(j, "key"));
        if (write.key.empty()) {
            return fail(ErrorCode::ParseError, L"unknown registry root", wide(j, "key"));
        }
        return write;
    }
    return core::parseRegValue(wide(j, "key"), wide(j, "name"), wide(j, "value"));
}

} // namespace

Result<ComponentCatalog> ComponentCatalog::parse(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.components") {
        return fail(ErrorCode::ParseError, L"not a components catalog", L"components.json");
    }
    ComponentCatalog catalog;
    try {
        for (const auto& g : doc.value("groups", Json::array())) {
            catalog.m_groups.push_back({g.value("id", std::string{}), {wide(g, "tr"), wide(g, "en")}});
        }
        for (const auto& c : doc.value("components", Json::array())) {
            ComponentCatalogEntry entry;
            entry.id = c.value("id", std::string{});
            entry.group = c.value("group", std::string{});
            entry.kind = c.value("kind", std::string{"remove"}) == "cleanup" ? ComponentCatalogEntry::Kind::Cleanup
                                                                              : ComponentCatalogEntry::Kind::Remove;
            entry.name = {wide(c, "tr"), wide(c, "en")};
            entry.notes = {wide(c, "notes_tr"), wide(c, "notes_en")};
            entry.risk = riskFrom(c.value("risk", std::string{"medium"}));
            entry.resetBase = c.value("resetBase", true);
            entry.always = c.value("always", false);
            entry.deep = c.value("deep", false);
            std::wstring why;
            if (entry.id.empty() || entry.name.tr.empty() || entry.name.en.empty()) {
                why = L"needs an id and both names";
            } else if (std::ranges::none_of(catalog.m_groups, [&](const auto& g) { return g.id == entry.group; })) {
                why = L"unknown group";
            } else if (catalog.find(entry.id)) {
                why = L"duplicate id";
            }
            if (why.empty() && entry.kind == ComponentCatalogEntry::Kind::Remove) {
                for (const auto& item : c.value("packages", Json::array())) {
                    entry.recipe.packages.push_back(utf8::toWide(item.get<std::string>()));
                }
                for (const auto& item : c.value("paths", Json::array())) {
                    entry.recipe.paths.push_back(utf8::toWide(item.get<std::string>()));
                }
                for (const auto& item : c.value("driverClasses", Json::array())) {
                    entry.recipe.driverClasses.push_back(utf8::toWide(item.get<std::string>()));
                }
                for (const auto& item : c.value("appx", Json::array())) {
                    entry.recipe.appx.push_back(utf8::toWide(item.get<std::string>()));
                }
                entry.recipe.afterUpdates = c.value("afterUpdates", false);
                for (const auto& item : c.value("registry", Json::array())) {
                    auto write = writeFrom(item);
                    if (!write) {
                        why = describe(write.error());
                        break;
                    }
                    entry.recipe.registry.push_back(std::move(*write));
                }
                if (why.empty()) {
                    if (auto valid = core::validateComponentRecipe(entry.recipe); !valid) {
                        why = describe(valid.error());
                    } else if (entry.recipe.paths.empty() && entry.recipe.packages.empty() && entry.recipe.driverClasses.empty()) {
                        // Presence is read from the paths or from the component store (D-059, D-060).
                        why = L"needs a path, a package or a driver class";
                    } else if (entry.deep != !entry.recipe.driverClasses.empty()) {
                        why = L"\"deep\" goes with driverClasses (the page warns for exactly these)";
                    }
                }
            }
            if (!why.empty()) {
                log::warn("app", L"components.json: skipped \"" + utf8::toWide(entry.id) + L"\" — " + why);
                continue;
            }
            catalog.m_components.push_back(std::move(entry));
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed components catalog", utf8::toWide(e.what()));
    }
    return catalog;
}

const ComponentCatalogEntry* ComponentCatalog::find(std::string_view id) const {
    const auto it = std::ranges::find(m_components, id, &ComponentCatalogEntry::id);
    return it == m_components.end() ? nullptr : &*it;
}

} // namespace wl::app
