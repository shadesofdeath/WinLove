#pragma once
// P18 Komut Paleti (docs/pages/18-command-palette.md): what Ctrl+K can find. Pages, the P12
// settings, and — once the mounted image's lists are read — its components, features and
// services; plus the commands that can run right now. Nothing is cached: every search reads the
// state again, so a list that finishes loading while the palette is open simply shows up.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/FeatureController.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/ServiceController.h"
#include "app/pages/PageInfo.h"
#include "app/state/AppState.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

enum class PaletteCommand : std::uint8_t {
    ApplyQueue,
    SavePreset,
    LoadPreset,
    OpenSource,
    Unmount,
    ToggleTheme,
    ToggleNav,
    OpenLogFolder,
};

struct PaletteItem {
    // Also the order of equally good matches.
    enum class Kind : std::uint8_t { Page, Setting, Component, Feature, Service, Command };
    Kind kind = Kind::Page;
    ui::icons::Icon icon = ui::icons::Icon::Search;
    std::wstring name;
    std::wstring detail;            // caption under the name (results)
    std::vector<std::wstring> keys; // shortcut keycap (commands)
    PageId page = PageId::Source;   // where the item lives
    std::wstring id;                // setting id / package name / feature name / service name
    PaletteCommand command = PaletteCommand::ApplyQueue;
    // The part of `name` the query matched (the mark); length 0: matched elsewhere.
    std::size_t markAt = 0;
    std::size_t markLength = 0;
};

struct PaletteResults {
    std::vector<PaletteItem> results;  // best first, at most PaletteIndex::kMaxResults
    std::size_t total = 0;             // how many matched before the cut
    std::vector<PaletteItem> commands;
};

// Search folding, one character per character: lower case, Turkish I / İ / ı → "i", and the
// Turkish diacritics dropped — "WINDOWS", "wındows" and "guncelleme" all find what they mean.
[[nodiscard]] std::wstring foldForSearch(std::wstring_view text);

struct PaletteMatch {
    int rank = -1; // < 0: no match. 0 name starts with the query · 1 a word of the name does ·
                   // 2 inside a word · 3 every query word somewhere in the name · 4 only with `extra`
    std::size_t at = 0;
    std::size_t length = 0;
    explicit operator bool() const noexcept { return rank >= 0; }
};
// `extra`: searchable text that is not shown as the name (package identity, page description).
[[nodiscard]] PaletteMatch matchPalette(std::wstring_view name, std::wstring_view extra, std::wstring_view query);

class PaletteIndex {
public:
    static constexpr std::size_t kMaxResults = 7;
    static constexpr std::size_t kMaxCommands = 5;    // while searching
    static constexpr std::size_t kMaxIdleCommands = 8; // empty query: the command list

    struct Sources {
        AppState& state;
        const Localization& strings;
        Language language;
        const ImageSettingsController& settings;
        const ComponentController& components;
        const FeatureController& features;
        const ServiceController& services;
        std::function<bool(PaletteCommand)> available; // can the command run now?
        std::function<bool()> navCollapsed;            // "collapse" or "expand" wording
    };
    explicit PaletteIndex(Sources sources) : m(std::move(sources)) {}

    // Empty query: no results, every available command.
    [[nodiscard]] PaletteResults search(std::wstring_view query) const;

private:
    [[nodiscard]] PaletteItem commandItem(PaletteCommand command) const;

    Sources m;
};

} // namespace wl::app
