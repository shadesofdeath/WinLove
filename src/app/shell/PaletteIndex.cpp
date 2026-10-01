#include "app/shell/PaletteIndex.h"

#include "app/Format.h"
#include "app/pages/FeaturesPage.h"
#include "app/pages/PageBits.h"
#include "app/pages/ServicesPage.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace wl::app {

namespace {

using ui::icons::Icon;

constexpr std::wstring_view kSeparator = L" · ";

std::wstring_view trimmed(std::wstring_view text) {
    while (!text.empty() && text.front() == L' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == L' ') {
        text.remove_suffix(1);
    }
    return text;
}

bool isWordChar(wchar_t c) {
    return std::iswalnum(c) != 0;
}

// 0 at the start, 1 at the start of a word, 2 inside one.
int positionRank(std::wstring_view text, std::size_t at) {
    return at == 0 ? 0 : !isWordChar(text[at - 1]) ? 1 : 2;
}

std::wstring widen(std::string_view text) {
    return {text.begin(), text.end()}; // catalog ids and page keys are ASCII
}

} // namespace

std::wstring foldForSearch(std::wstring_view text) {
    std::wstring out(text);
    if (out.empty()) {
        return out;
    }
    // Before the lower-casing: the invariant mapping would turn "I" into "i" but leave "ı" alone.
    for (auto& c : out) {
        if (c == L'I' || c == L'İ' || c == L'ı') {
            c = L'i';
        }
    }
    std::wstring lower(out.size(), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, out.data(), static_cast<int>(out.size()),
                                      lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0);
    if (written == static_cast<int>(out.size())) { // marks in the original text rely on equal lengths
        out = std::move(lower);
    }
    for (auto& c : out) {
        switch (c) {
        case L'ç': c = L'c'; break; // ç
        case L'ğ': c = L'g'; break; // ğ
        case L'ö': c = L'o'; break; // ö
        case L'ş': c = L's'; break; // ş
        case L'ü': c = L'u'; break; // ü
        default: break;
        }
    }
    return out;
}

PaletteMatch matchPalette(std::wstring_view name, std::wstring_view extra, std::wstring_view query) {
    const std::wstring q = foldForSearch(trimmed(query));
    if (q.empty()) {
        return {};
    }
    const std::wstring n = foldForSearch(name);
    // The whole query in the name: the best placed occurrence ("WinDefend — Microsoft Defender…"
    // matches "def" at the word, not inside "WinDefend").
    PaletteMatch best;
    for (std::size_t at = n.find(q); at != std::wstring::npos; at = n.find(q, at + 1)) {
        const int rank = positionRank(n, at);
        if (!best || rank < best.rank) {
            best = {rank, at, q.size()};
        }
        if (rank == 0) {
            break;
        }
    }
    if (best) {
        return best;
    }
    // Word by word: each one somewhere in the name, or at least in the extra text.
    const std::wstring e = foldForSearch(extra);
    bool allInName = true;
    PaletteMatch mark{4, 0, 0};
    std::size_t from = 0;
    while (from < q.size()) {
        std::size_t to = q.find(L' ', from);
        if (to == std::wstring::npos) {
            to = q.size();
        }
        if (to > from) {
            const std::wstring_view word = std::wstring_view(q).substr(from, to - from);
            const std::size_t at = n.find(word);
            if (at == std::wstring::npos) {
                allInName = false;
                if (e.find(word) == std::wstring::npos) {
                    return {};
                }
            } else if (mark.length == 0) {
                mark.at = at;
                mark.length = word.size();
            }
        }
        from = to + 1;
    }
    mark.rank = allInName ? 3 : 4;
    return mark;
}

PaletteItem PaletteIndex::commandItem(PaletteCommand command) const {
    auto s = [&](Str key) { return m.strings.get(key); };
    auto keys = [&](std::initializer_list<Str> parts) {
        std::wstring joined;
        for (const Str part : parts) {
            if (!joined.empty()) {
                joined += L' ';
            }
            joined += s(part);
        }
        return std::vector<std::wstring>{std::move(joined)};
    };
    PaletteItem item;
    item.kind = PaletteItem::Kind::Command;
    item.command = command;
    switch (command) {
    case PaletteCommand::ApplyQueue:
        item.icon = Icon::ApplyPlay;
        item.name = s(Str::PaletteCmdApplyQueue);
        item.keys = keys({Str::KbdCtrl, Str::KbdEnterKey});
        break;
    case PaletteCommand::SavePreset:
        item.icon = Icon::Save;
        item.name = s(Str::PaletteCmdSavePreset);
        item.keys = {s(Str::KbdCtrl) + L" S"};
        break;
    case PaletteCommand::LoadPreset:
        item.icon = Icon::PresetBookmark;
        item.name = s(Str::PaletteCmdLoadPreset);
        item.keys = {s(Str::KbdCtrl) + L" O"};
        break;
    case PaletteCommand::OpenSource:
        item.icon = Icon::OpenFolder;
        item.name = s(Str::PaletteCmdOpenSource);
        break;
    case PaletteCommand::Unmount:
        item.icon = Icon::Unmount;
        item.name = s(Str::PaletteCmdUnmount);
        break;
    case PaletteCommand::ToggleTheme:
        item.icon = Icon::Eye;
        item.name = s(Str::PaletteCmdToggleTheme);
        item.keys = {s(Str::KbdCtrl) + L" " + s(Str::KbdShift) + L" T"};
        break;
    case PaletteCommand::ToggleNav:
        item.icon = Icon::SidebarToggle;
        item.name = s(m.navCollapsed && m.navCollapsed() ? Str::PaletteCmdExpandNav : Str::PaletteCmdCollapseNav);
        item.keys = {s(Str::KbdCtrl) + L" B"};
        break;
    case PaletteCommand::OpenLogFolder:
        item.icon = Icon::LogTerminal;
        item.name = s(Str::AboutOpenLogFolder);
        break;
    }
    return item;
}

PaletteResults PaletteIndex::search(std::wstring_view query) const {
    auto s = [&](Str key) { return m.strings.get(key); };
    struct Scored {
        PaletteItem item;
        int rank;
    };
    auto take = [](std::vector<Scored>& found, std::size_t limit) {
        std::ranges::stable_sort(found, [](const Scored& a, const Scored& b) {
            return a.rank != b.rank ? a.rank < b.rank : a.item.kind < b.item.kind;
        });
        std::vector<PaletteItem> out;
        for (auto& scored : found) {
            if (out.size() == limit) {
                break;
            }
            out.push_back(std::move(scored.item));
        }
        return out;
    };
    const bool searching = !trimmed(query).empty();
    PaletteResults out;

    // ---- commands ----
    {
        std::vector<Scored> found;
        for (const auto command : {PaletteCommand::ApplyQueue, PaletteCommand::SavePreset, PaletteCommand::LoadPreset,
                                   PaletteCommand::OpenSource, PaletteCommand::Unmount, PaletteCommand::ToggleTheme,
                                   PaletteCommand::ToggleNav, PaletteCommand::OpenLogFolder}) {
            if (m.available && !m.available(command)) {
                continue;
            }
            PaletteItem item = commandItem(command);
            if (!searching) {
                found.push_back({std::move(item), 0});
            } else if (const PaletteMatch match = matchPalette(item.name, {}, query)) {
                item.markAt = match.at;
                item.markLength = match.length;
                found.push_back({std::move(item), match.rank});
            }
        }
        out.commands = take(found, searching ? kMaxCommands : kMaxIdleCommands);
    }
    if (!searching) {
        return out;
    }

    // ---- results ----
    std::vector<Scored> found;
    auto consider = [&](PaletteItem item, std::wstring_view extra) {
        if (const PaletteMatch match = matchPalette(item.name, extra, query)) {
            item.markAt = match.at;
            item.markLength = match.length;
            found.push_back({std::move(item), match.rank});
        }
    };

    for (const auto& page : allPages()) {
        if (page.id == PageId::Gallery) {
            continue; // developer page
        }
        PaletteItem item;
        item.kind = PaletteItem::Kind::Page;
        item.icon = page.icon;
        item.page = page.id;
        item.name = s(page.navLabel);
        item.detail = s(Str::PalettePages) + std::wstring(kSeparator) + s(Str::PaletteGo);
        std::wstring extra = widen(page.key) + L' ' + s(page.title);
        if (page.description) {
            extra += L' ' + s(*page.description);
        }
        consider(std::move(item), extra);
    }

    // The mounted image's own things; the P12 form is for a mounted image too.
    if (m.state.mounted()) {
        const auto& catalog = m.settings.catalog();
        for (const auto& setting : catalog.settings()) {
            const auto section = std::ranges::find(catalog.sections(), setting.section, &ImageSettingSection::id);
            if (section == catalog.sections().end()) {
                continue;
            }
            const auto tab = std::ranges::find(catalog.tabs(), section->tab, &ImageSettingTab::id);
            PaletteItem item;
            item.kind = PaletteItem::Kind::Setting;
            item.icon = Icon::TweaksSliders;
            item.page = PageId::Tweaks;
            item.id = widen(setting.id);
            item.name = setting.label.get(m.language);
            item.detail = s(Str::NavTweaks);
            std::wstring extra = section->title.get(m.language);
            if (tab != catalog.tabs().end()) {
                item.detail += std::wstring(kSeparator) + tab->title.get(m.language);
                extra += L' ' + tab->title.get(m.language);
            }
            if (m.settings.current(setting) != setting.defaultOption) {
                item.detail += std::wstring(kSeparator) + s(Str::ComponentsQueued);
            }
            consider(std::move(item), extra);
        }

        for (const auto& group : m.components.groups()) {
            for (const auto& component : group.items) {
                PaletteItem item;
                item.kind = PaletteItem::Kind::Component;
                item.icon = Icon::AppxPackage;
                item.page = PageId::Components;
                item.id = component.packageName;
                item.name = component.name;
                item.detail = s(Str::NavComponents) + std::wstring(kSeparator) +
                              (m.components.queued(component)
                                   ? s(Str::PaletteQueuedForRemoval)
                                   : m.strings.format(Str::PaletteRiskOf, {{L"risk", s(riskText(component.risk))}}));
                if (component.size > 0) {
                    item.detail += std::wstring(kSeparator) + formatBytes(component.size, m.language);
                }
                consider(std::move(item), component.identity + L' ' + group.name);
            }
        }

        if (const auto& features = m.state.optionalFeatures();
            features && features->status == AppState::OptionalFeatures::Status::Ready) {
            for (const auto& feature : features->items) {
                PaletteItem item;
                item.kind = PaletteItem::Kind::Feature;
                item.icon = Icon::PuzzleFeatures;
                item.page = PageId::Features;
                item.id = feature.name;
                item.name = feature.displayName.empty() ? feature.name : feature.displayName;
                item.detail = s(Str::NavFeatures) + std::wstring(kSeparator) +
                              s(FeaturesPage::statusName(m.features.status(feature)));
                consider(std::move(item), feature.name);
            }
        }

        if (const auto& services = m.state.serviceList();
            services && services->status == AppState::ServiceList::Status::Ready) {
            for (const auto& service : services->items) {
                PaletteItem item;
                item.kind = PaletteItem::Kind::Service;
                item.icon = Icon::ServicesGear;
                item.page = PageId::Services;
                item.id = service.name;
                item.name = service.displayName.empty() || service.displayName == service.name
                                ? service.name
                                : service.name + L" — " + service.displayName;
                item.detail = s(Str::NavServices) + std::wstring(kSeparator) +
                              s(ServicesPage::startName(m.services.target(service)));
                if (m.services.changed(service)) {
                    item.detail += std::wstring(kSeparator) + s(Str::ComponentsQueued);
                }
                consider(std::move(item), {});
            }
        }
    }

    out.total = found.size();
    out.results = take(found, kMaxResults);
    return out;
}

} // namespace wl::app
