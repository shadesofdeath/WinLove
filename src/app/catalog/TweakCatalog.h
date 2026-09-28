#pragma once
// resources/catalog/tweaks.json: the P11 registry tweak categories. Each tweak is a list of
// registry writes (.reg value syntax, validated with core::parseRegValue at load).
#include "app/Localization.h"
#include "base/Result.h"
#include "core/image/RegistryEdit.h"
#include "core/ops/ChangeSet.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct TweakCategory {
    std::string id;
    std::wstring descTr;
    std::wstring descEn;
    [[nodiscard]] const std::wstring& desc(Language language) const {
        return language == Language::Turkish ? descTr : descEn;
    }
};

struct Tweak {
    std::string id;
    std::string category;
    std::wstring nameTr;
    std::wstring nameEn;
    core::ops::Risk risk = core::ops::Risk::Low;
    bool recommended = false;
    bool firstLogon = false; // "apply": "firstLogon" → SetRegistryFirstLogon
    std::vector<core::RegistryWrite> writes;
    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
};

class TweakCatalog {
public:
    // Entries with an invalid write are skipped (logged), not fatal.
    [[nodiscard]] static Result<TweakCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<TweakCategory>& categories() const noexcept { return m_categories; }
    [[nodiscard]] const std::vector<Tweak>& tweaks() const noexcept { return m_tweaks; }

private:
    std::vector<TweakCategory> m_categories;
    std::vector<Tweak> m_tweaks;
};

} // namespace wl::app
