#pragma once
// D-082: resources/catalog/compat.json — the compatibility guards ("Windows Update", "Printing" …):
// what each keeps out of the queue (core::ops::CompatGuard) and the texts the Uyumluluk dialog
// shows. Which are on is the user's (AppSettings::guards); `defaultOn` is for a new user.
#include "app/Localization.h"
#include "base/Result.h"
#include "core/ops/Compat.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

struct CompatCatalogEntry {
    core::ops::CompatGuard rule;
    std::wstring nameTr;
    std::wstring nameEn;
    std::wstring descriptionTr;
    std::wstring descriptionEn;
    bool defaultOn = false;

    [[nodiscard]] const std::wstring& name(Language language) const {
        return language == Language::Turkish ? nameTr : nameEn;
    }
    [[nodiscard]] const std::wstring& description(Language language) const {
        return language == Language::Turkish ? descriptionTr : descriptionEn;
    }
};

class CompatCatalog {
public:
    // A guard without an id, a name or anything to keep is skipped (and logged).
    [[nodiscard]] static Result<CompatCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<CompatCatalogEntry>& guards() const noexcept { return m_guards; }
    [[nodiscard]] const CompatCatalogEntry* find(std::wstring_view id) const;
    [[nodiscard]] std::vector<std::wstring> defaults() const; // ids that are on for a new user

private:
    std::vector<CompatCatalogEntry> m_guards;
};

} // namespace wl::app
