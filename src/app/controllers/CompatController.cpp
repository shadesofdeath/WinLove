#include "app/controllers/CompatController.h"

#include "app/controllers/PostSetupController.h"
#include "base/Log.h"

#include <algorithm>
#include <format>
#include <map>

namespace wl::app {

namespace {

// At most `limit` names, then "+N".
std::wstring joined(const std::vector<std::wstring>& names, std::size_t limit = 3) {
    std::wstring out;
    for (std::size_t i = 0; i < names.size() && i < limit; ++i) {
        out += (i ? L", " : L"") + names[i];
    }
    if (names.size() > limit) {
        out += std::format(L" +{}", names.size() - limit);
    }
    return out;
}

// D-120: the guards before `guardsSeen` was saved (D-082 and "Önerilen"). Settings without the
// list chose among these; a later guard takes its default.
constexpr std::wstring_view kGuardsBeforeSeen[] = {L"windows-update", L"store",    L"printing", L"wireless",       L"audio",
                                                   L"webview-apps",   L"recovery", L"security", L"xbox",           L"sharing",
                                                   L"remote-desktop", L"camera",   L"east-asian", L"recommended"};

} // namespace

CompatController::CompatController(AppState& state, const Localization& strings, Language language, CompatCatalog catalog,
                                   const AppxCatalog& appxNames, const PostSetupController& postSetup)
    : m_state(state), m_strings(strings), m_catalog(std::move(catalog)), m_appxNames(appxNames), m_postSetup(postSetup),
      m_language(language) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        // The queue changed (a removal taken back, a preset, programs picked), or the app list
        // came in with what each app needs.
        if (change == AppState::Change::Queue || change == AppState::Change::Components) {
            enforce();
        }
    });
}

CompatController::~CompatController() {
    m_state.unsubscribe(m_subscription);
}

bool CompatController::isOn(std::wstring_view id) const {
    if (const auto* guard = m_catalog.find(id); guard && guard->rule.always) {
        return true; // D-121: "core"
    }
    const auto& settings = m_state.settings();
    const auto& chosen = settings.guards;
    const bool seen = chosen && (settings.guardsSeen ? std::ranges::find(*settings.guardsSeen, id) != settings.guardsSeen->end()
                                                     : std::ranges::find(kGuardsBeforeSeen, id) != std::end(kGuardsBeforeSeen));
    if (!seen) {
        const auto* guard = m_catalog.find(id);
        return guard && guard->defaultOn;
    }
    return std::ranges::find(*chosen, id) != chosen->end();
}

std::vector<std::wstring> CompatController::catalogIds() const {
    std::vector<std::wstring> out;
    for (const auto& guard : m_catalog.guards()) {
        out.push_back(guard.rule.id);
    }
    return out;
}

std::size_t CompatController::onCount() const {
    // The user's choice: the guards that are always on are not counted.
    return static_cast<std::size_t>(std::ranges::count_if(
        m_catalog.guards(), [&](const CompatCatalogEntry& g) { return !g.rule.always && isOn(g.rule.id); }));
}

void CompatController::setOn(std::wstring_view id, bool on) {
    const auto* chosen = m_catalog.find(id);
    if (!chosen || chosen->rule.always || isOn(id) == on) {
        return;
    }
    std::vector<std::wstring> next;
    for (const auto& guard : m_catalog.guards()) {
        if (!guard.rule.always && (guard.rule.id == id ? on : isOn(guard.rule.id))) {
            next.push_back(guard.rule.id);
        }
    }
    AppSettings settings = m_state.settings();
    settings.guards = std::move(next);
    settings.guardsSeen = catalogIds();
    m_state.setSettings(std::move(settings));
    log::info("app", std::format(L"compatibility guard {} {}", id, on ? L"on" : L"off"));
    if (on) {
        enforce();
    }
}

void CompatController::setGuards(const std::vector<std::wstring>& on) {
    std::vector<std::wstring> next;
    for (const auto& guard : m_catalog.guards()) {
        if (!guard.rule.always && std::ranges::find(on, guard.rule.id) != on.end()) {
            next.push_back(guard.rule.id);
        }
    }
    if (next == onIds() && m_state.settings().guards && m_state.settings().guardsSeen) {
        return;
    }
    AppSettings settings = m_state.settings();
    settings.guards = next;
    settings.guardsSeen = catalogIds();
    m_state.setSettings(std::move(settings));
    log::info("app", std::format(L"compatibility guards: {} of {} on", next.size(), m_catalog.guards().size()));
    enforce();
}

std::vector<std::wstring> CompatController::onIds() const {
    std::vector<std::wstring> out; // the user's choice: not the guards that are always on
    for (const auto& guard : m_catalog.guards()) {
        if (!guard.rule.always && isOn(guard.rule.id)) {
            out.push_back(guard.rule.id);
        }
    }
    return out;
}

std::vector<core::ops::CompatGuard> CompatController::active() const {
    std::vector<core::ops::CompatGuard> out;
    for (const auto& guard : m_catalog.guards()) {
        if (isOn(guard.rule.id)) {
            out.push_back(guard.rule);
        }
    }
    // The Programlar page's window installs with winget: App Installer stays while it has programs.
    if (!m_postSetup.programs().empty()) {
        out.push_back({kProgramsGuard, {L"Microsoft.DesktopAppInstaller"}, {}, {}});
    }
    return out;
}

core::ops::AppxNeeds CompatController::needs() const {
    core::ops::AppxNeeds out;
    if (const auto& list = m_state.appxList(); list && list->status == AppState::AppxList::Status::Ready) {
        for (const auto& item : list->items) {
            out[item.package.displayName] = item.needs;
        }
    }
    return out;
}

core::ops::CompatBlock CompatController::block(const core::ops::Operation& op) const {
    return core::ops::compatBlock(op, active(), needs(), m_state.changes());
}

std::wstring CompatController::appName(const std::wstring& identity) const {
    if (const auto* entry = m_appxNames.find(identity)) {
        return entry->name(m_language);
    }
    return identity;
}

std::wstring CompatController::guardName(const std::wstring& id) const {
    if (id == kProgramsGuard) {
        return m_strings.get(Str::CompatProgramsGuard);
    }
    const auto* guard = m_catalog.find(id);
    return guard ? guard->name(m_language) : id;
}

std::wstring CompatController::explain(const core::ops::CompatBlock& block) const {
    std::wstring out;
    if (!block.guards.empty()) {
        std::vector<std::wstring> names;
        for (const auto& id : block.guards) {
            names.push_back(guardName(id));
        }
        out = m_strings.format(Str::CompatKeptBy, {{L"guards", joined(names)}});
    }
    if (!block.neededBy.empty()) {
        std::vector<std::wstring> names;
        for (const auto& identity : block.neededBy) {
            names.push_back(appName(identity));
        }
        out += (out.empty() ? L"" : L" · ") + m_strings.format(Str::CompatNeededBy, {{L"apps", joined(names)}});
    }
    return out;
}

std::vector<CompatController::Impact> CompatController::impact() const {
    // One pass over the queue with every guard that is off: a block lists each guard that would
    // hold the step (a recipe is parsed once per step, not once per guard).
    std::vector<core::ops::CompatGuard> off;
    for (const auto& guard : m_catalog.guards()) {
        if (!guard.rule.always && !isOn(guard.rule.id)) {
            off.push_back(guard.rule);
        }
    }
    std::map<std::wstring, std::vector<std::wstring>> items; // guard id → the steps it would keep
    for (const auto& conflict : core::ops::compatConflicts(m_state.changes(), off, needs())) {
        if (conflict.block.guards.empty()) {
            continue; // only an app that stays needs it: no guard of the report
        }
        const std::wstring name = describe ? describe(conflict.op) : conflict.op.target;
        for (const auto& id : conflict.block.guards) {
            auto& list = items[id];
            if (std::ranges::find(list, name) == list.end()) { // a 32-bit row has its component's name
                list.push_back(name);
            }
        }
    }
    std::vector<Impact> out;
    for (const bool apps : {false, true}) { // Windows' features first, then the apps
        for (const auto& guard : m_catalog.guards()) {
            const auto it = items.find(guard.rule.id);
            if (it != items.end() && (guard.section == CompatSection::Apps) == apps) {
                out.push_back({guard.rule.id, guard.name(m_language), guard.description(m_language), apps, std::move(it->second)});
            }
        }
    }
    return out;
}

void CompatController::enforce() {
    if (m_enforcing || m_state.queueLocked()) {
        return;
    }
    const auto conflicts = core::ops::compatConflicts(m_state.changes(), active(), needs());
    if (conflicts.empty()) {
        return;
    }
    m_enforcing = true;
    std::vector<std::pair<core::ops::OpKind, std::wstring>> slots;
    std::vector<std::wstring> names;
    for (const auto& conflict : conflicts) {
        slots.emplace_back(conflict.op.kind, conflict.op.target);
        names.push_back(describe ? describe(conflict.op) : conflict.op.target);
        log::info("app", std::format(L"compatibility: {} left the queue — {}", conflict.op.target, explain(conflict.block)));
    }
    m_state.unqueueMany(slots);
    m_enforcing = false;
    if (onDropped) {
        onDropped(m_strings.get(Str::CompatDroppedTitle),
                  m_strings.format(Str::CompatDroppedBody, {{L"n", std::to_wstring(names.size())}, {L"items", joined(names)}}) +
                      L" " + explain(conflicts.front().block));
    }
}

} // namespace wl::app
