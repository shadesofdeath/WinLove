#pragma once
// resources/catalog/appx.json: friendly names, groups, risk and notes for provisioned apps.
// Lookup is by package identity prefix (longest match wins): "Microsoft.VCLibs" matches
// "Microsoft.VCLibs.140.00.UWPDesktop". Unknown apps land in the "other" group, medium risk.
#include "app/Localization.h"
#include "base/Result.h"
#include "core/ops/ChangeSet.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct AppxGroup {
    std::string id;
    std::wstring nameTr;
    std::wstring nameEn;
    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
};

struct AppxCatalogEntry {
    std::wstring match;
    std::wstring nameTr;
    std::wstring nameEn;
    std::string group;
    core::ops::Risk risk = core::ops::Risk::Medium;
    std::wstring notesTr;
    std::wstring notesEn;
    // Image build from which DISM refuses to deprovision the app (0x80073CFA); 0 = never.
    int lockedSince = 0;
    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
    [[nodiscard]] const std::wstring& notes(Language language) const {
        return language == Language::Turkish ? notesTr : notesEn;
    }
};

class AppxCatalog {
public:
    [[nodiscard]] static Result<AppxCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<AppxGroup>& groups() const noexcept { return m_groups; }
    // nullptr when no entry matches.
    [[nodiscard]] const AppxCatalogEntry* find(std::wstring_view identity) const;
    [[nodiscard]] int groupIndex(std::string_view id) const; // "other" (last) when unknown

private:
    std::vector<AppxGroup> m_groups;
    std::vector<AppxCatalogEntry> m_entries;
};

} // namespace wl::app
