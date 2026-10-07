#pragma once
// D-078: resources/catalog/programs.json — what the Programlar page offers before a search: its
// categories (their own programs, then every package winget tags with one of the category's tags)
// and its bundles (a few programs picked in one click). Only winget ids and tags: names and
// versions come from winget's index when the page shows them (tools/check_programs.py checks them).
#include "app/Localization.h"
#include "base/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct ProgramCategory {
    std::string id;
    std::wstring nameTr;
    std::wstring nameEn;
    std::vector<std::wstring> programs; // winget ids, in the order shown
    std::vector<std::wstring> tags;     // winget tags (lower case) of the rest of the category

    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
};

struct ProgramBundle {
    std::string id;
    std::wstring nameTr;
    std::wstring nameEn;
    std::wstring descriptionTr;
    std::wstring descriptionEn;
    std::vector<std::wstring> programs; // winget ids

    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
    [[nodiscard]] const std::wstring& description(Language language) const {
        return language == Language::Turkish ? descriptionTr : descriptionEn;
    }
};

class ProgramCatalog {
public:
    // A category without an id or a name is skipped; an id that cannot go on a command line too.
    [[nodiscard]] static Result<ProgramCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<ProgramCategory>& categories() const noexcept { return m_categories; }
    [[nodiscard]] const std::vector<ProgramBundle>& bundles() const noexcept { return m_bundles; }
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<ProgramCategory> m_categories;
    std::vector<ProgramBundle> m_bundles;
};

} // namespace wl::app
