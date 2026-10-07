#include "app/controllers/ProgramsController.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"
#include "core/postsetup/PostSetup.h"
#include "core/tasks/TaskRunner.h"

#include <json.hpp>

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <format>

namespace wl::app {

namespace {

std::wstring key(std::wstring_view id) {
    return text::lower(id);
}

} // namespace

ProgramsController::ProgramsController(AppState& state, PostSetupController& postSetup, ProgramCatalog catalog,
                                       const Localization& strings, Language language, Events events)
    : m_state(state), m_postSetup(postSetup), m_catalog(std::move(catalog)), m_strings(strings), m_language(language),
      m_events(std::move(events)), m_net(std::make_unique<core::TaskRunner>()) {}

ProgramsController::~ProgramsController() {
    *m_alive = false;
    m_net.reset();
}

std::filesystem::path ProgramsController::cacheFolder() const {
    return m_state.settings().workRoot / L"winget";
}

void ProgramsController::load(bool refresh) {
    if (m_status == Status::Loading || (m_status == Status::Ready && !refresh)) {
        return;
    }
    // The open index holds index.db: a refresh replaces the file.
    m_index.reset();
    m_status = Status::Loading;
    m_state.notifyPrograms();
    const auto folder = cacheFolder();
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_net->run<std::filesystem::path>(
        [folder, refresh](const core::TaskContext& task) {
            return core::refreshWingetIndex(folder, refresh ? std::chrono::hours(0) : std::chrono::hours(24), task);
        },
        [this, post, alive](Result<std::filesystem::path> result) {
            post([this, alive, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                auto index = result ? core::WingetIndex::open(*result) : Result<core::WingetIndex>(std::unexpected(result.error()));
                if (index) {
                    m_count = index->count();
                    m_index = std::move(*index);
                    m_status = Status::Ready;
                    log::info("programs", std::format(L"winget index: {} packages", m_count));
                    fetchNext();
                } else {
                    m_error = index.error();
                    m_status = Status::Failed;
                    log::warn("programs", L"winget index: " + describe(m_error));
                }
                m_state.notifyPrograms();
            });
        });
}

namespace {
constexpr std::size_t kTaggedLimit = 5000;
constexpr std::size_t kEverythingLimit = 100000;

bool listed(const std::vector<core::WingetPackage>& list, std::wstring_view id) {
    return std::ranges::any_of(list, [&](const core::WingetPackage& p) { return text::iequals(p.id, id); });
}
} // namespace

std::vector<ProgramsController::Category> ProgramsController::categories() const {
    std::vector<Category> out;
    if (!m_index) {
        return out;
    }
    for (std::size_t i = 0; i < m_catalog.categories().size(); ++i) {
        const auto& category = m_catalog.categories()[i];
        Category c{category.name(m_language), {}, 0};
        for (const auto& id : category.programs) {
            if (auto package = m_index->find(id)) {
                c.programs.push_back(std::move(*package));
            }
        }
        c.more = moreIn(i).size();
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<core::WingetPackage> ProgramsController::moreIn(std::size_t index) const {
    if (!m_index || index >= m_catalog.categories().size()) {
        return {};
    }
    const auto& category = m_catalog.categories()[index];
    auto tagged = m_index->tagged(category.tags, kTaggedLimit);
    std::erase_if(tagged, [&](const core::WingetPackage& p) {
        return std::ranges::any_of(category.programs, [&](const std::wstring& id) { return text::iequals(id, p.id); });
    });
    return tagged;
}

std::vector<core::WingetPackage> ProgramsController::everything() const {
    return m_index ? m_index->all(kEverythingLimit) : std::vector<core::WingetPackage>{};
}

std::vector<ProgramsController::Bundle> ProgramsController::bundles() const {
    std::vector<Bundle> out;
    if (!m_index) {
        return out;
    }
    for (const auto& bundle : m_catalog.bundles()) {
        Bundle b{bundle.id, bundle.name(m_language), bundle.description(m_language), {}};
        for (const auto& id : bundle.programs) {
            if (auto package = m_index->find(id); package && !listed(b.programs, package->id)) {
                b.programs.push_back(std::move(*package));
            }
        }
        if (!b.programs.empty()) {
            out.push_back(std::move(b));
        }
    }
    return out;
}

ProgramsController::BundleState ProgramsController::bundleState(const Bundle& bundle) const {
    const auto picked = std::ranges::count_if(bundle.programs, [&](const core::WingetPackage& p) { return this->picked(p.id); });
    return picked == 0 ? BundleState::None
           : static_cast<std::size_t>(picked) == bundle.programs.size() ? BundleState::All
                                                                         : BundleState::Some;
}

int ProgramsController::toggleBundle(const Bundle& bundle) {
    std::vector<core::PostSetupProgram> next = picks();
    int changed = 0;
    if (bundleState(bundle) == BundleState::All) {
        changed = static_cast<int>(std::erase_if(next, [&](const core::PostSetupProgram& pick) { return listed(bundle.programs, pick.id); }));
    } else {
        for (const auto& p : bundle.programs) {
            if (!picked(p.id)) {
                next.push_back({p.id, p.name.empty() ? p.id : p.name});
                ++changed;
            }
        }
    }
    if (changed > 0) {
        m_postSetup.setPrograms(std::move(next), windowTexts());
    }
    return changed;
}

std::vector<core::WingetPackage> ProgramsController::search(std::wstring_view text, std::size_t limit) const {
    return m_index ? m_index->search(text, limit) : std::vector<core::WingetPackage>{};
}

std::optional<core::WingetPackage> ProgramsController::package(std::wstring_view id) const {
    return m_index ? m_index->find(id) : std::nullopt;
}

bool ProgramsController::picked(std::wstring_view id) const {
    return std::ranges::any_of(picks(), [&](const core::PostSetupProgram& p) { return text::iequals(p.id, id); });
}

void ProgramsController::toggle(const core::WingetPackage& package) {
    std::vector<core::PostSetupProgram> next = picks();
    const auto at = std::ranges::find_if(next, [&](const core::PostSetupProgram& p) { return text::iequals(p.id, package.id); });
    if (at != next.end()) {
        next.erase(at);
    } else {
        next.push_back({package.id, package.name.empty() ? package.id : package.name});
    }
    m_postSetup.setPrograms(std::move(next), windowTexts());
}

void ProgramsController::clear() {
    m_postSetup.setPrograms({}, {});
}

bool ProgramsController::wingetRemoved() const {
    return std::ranges::any_of(m_state.changes().operations(), [](const core::ops::Operation& op) {
        return op.kind == core::ops::OpKind::RemoveAppx && text::istartsWith(op.target, L"Microsoft.DesktopAppInstaller");
    });
}

const core::WingetDetails* ProgramsController::details(std::wstring_view id) {
    const std::wstring k = key(id);
    if (const auto it = m_details.find(k); it != m_details.end()) {
        return &it->second;
    }
    if (!m_failed.contains(k)) {
        // Newest last: the rows on screen now go before the ones scrolled past.
        std::erase(m_wanted, k);
        m_wanted.push_back(k);
        // Rows scrolled past long ago are not fetched: a fling through the whole repository would
        // otherwise download thousands of manifests nobody looks at.
        constexpr std::size_t kWantedMax = 64;
        if (m_wanted.size() > kWantedMax) {
            m_wanted.erase(m_wanted.begin(), m_wanted.end() - static_cast<std::ptrdiff_t>(kWantedMax));
        }
        fetchNext();
    }
    return nullptr;
}

bool ProgramsController::detailsFailed(std::wstring_view id) const {
    return m_failed.contains(key(id));
}

std::filesystem::path ProgramsController::icon(std::wstring_view id) {
    if (const auto it = m_icons.find(key(id)); it != m_icons.end()) {
        return it->second;
    }
    (void)details(id);
    return {};
}

void ProgramsController::fetchNext() {
    if (m_fetching || !m_index) {
        return;
    }
    while (!m_wanted.empty()) {
        const std::wstring k = m_wanted.back();
        m_wanted.pop_back();
        if (m_details.contains(k) || m_failed.contains(k)) {
            continue;
        }
        auto found = m_index->find(k);
        const std::wstring hash = m_index->versionDataHash(k);
        if (!found) {
            m_failed.insert(k);
            continue;
        }
        m_fetching = true;
        const auto folder = cacheFolder();
        const std::wstring locale = m_language == Language::Turkish ? L"tr-TR" : L"en-US";
        struct Fetched {
            core::WingetDetails details;
            std::filesystem::path icon;
        };
        auto post = m_events.postToUi;
        std::weak_ptr<bool> alive = m_alive;
        m_net->run<Fetched>(
            [package = *found, hash, folder, locale](const core::TaskContext& task) -> Result<Fetched> {
                auto details = core::fetchWingetDetails(package, hash, folder, locale, task.cancel);
                if (!details) {
                    return std::unexpected(details.error());
                }
                Fetched out{std::move(*details), {}};
                if (!out.details.iconUrl.empty()) {
                    if (auto icon = core::fetchWingetIcon(out.details, folder, task.cancel)) {
                        out.icon = std::move(*icon);
                    }
                }
                return out;
            },
            [this, post, alive, k](Result<Fetched> result) {
                post([this, alive, k, result = std::move(result)]() mutable {
                    if (const auto a = alive.lock(); !a || !*a) {
                        return;
                    }
                    m_fetching = false;
                    if (result) {
                        m_icons[k] = std::move(result->icon);
                        m_details[k] = std::move(result->details);
                    } else {
                        m_failed.insert(k);
                        log::warn("programs", k + L": " + describe(result.error()));
                    }
                    m_state.notifyPrograms();
                    fetchNext();
                });
            });
        return;
    }
}

std::vector<std::pair<std::wstring, std::wstring>> ProgramsController::windowTexts() const {
    const std::pair<const wchar_t*, Str> keys[] = {
        {L"heading", Str::ProgramsWinHeading},
        {L"finished", Str::ProgramsWinFinished},
        {L"pending", Str::ProgramsWinPending},
        {L"installing", Str::ProgramsWinInstalling},
        {L"installingRow", Str::ProgramsWinInstallingRow},
        {L"installed", Str::ProgramsWinInstalled},
        {L"already", Str::ProgramsWinAlready},
        {L"restart", Str::ProgramsWinRestart},
        {L"failed", Str::ProgramsWinFailed},
        {L"notInstalled", Str::ProgramsWinNotInstalled},
        {L"waitingPostSetup", Str::ProgramsWinWaitingPostSetup},
        {L"waitingWinget", Str::ProgramsWinWaitingWinget},
        {L"noWinget", Str::ProgramsWinNoWinget},
        {L"waitingNetwork", Str::ProgramsWinWaitingNetwork},
        {L"waitingNetworkHint", Str::ProgramsWinWaitingNetworkHint},
        {L"updatingSources", Str::ProgramsWinUpdatingSources},
        {L"done", Str::ProgramsWinDone},
        {L"doneWithErrors", Str::ProgramsWinDoneWithErrors},
        {L"close", Str::ProgramsWinClose},
        {L"hint", Str::ProgramsWinHint},
    };
    std::vector<std::pair<std::wstring, std::wstring>> out;
    for (const auto& [name, str] : keys) {
        out.emplace_back(name, m_strings.get(str));
    }
    return out;
}

Result<void> ProgramsController::preview() const {
    core::PostSetupPlan plan;
    plan.programs = picks();
    if (plan.programs.empty()) {
        // Nothing picked yet: three well-known ones show how it goes.
        for (const wchar_t* id : {L"7zip.7zip", L"VideoLAN.VLC", L"Google.Chrome"}) {
            const auto found = package(id);
            plan.programs.push_back({id, found ? found->name : std::wstring(id)});
        }
    }
    plan.programTexts = windowTexts();
    auto json = nlohmann::json::parse(core::programsJson(plan));
    json["dryRun"] = true;
    const auto folder = std::filesystem::temp_directory_path() / L"WinLove" / L"programs-preview";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (auto r = writeFileAtomic(folder / L"programs.ps1", core::scripts::kPrograms); !r) {
        return r;
    }
    if (auto r = writeFileAtomic(folder / L"programs.json", json.dump(2)); !r) {
        return r;
    }
    const std::wstring arguments = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + (folder / L"programs.ps1").wstring() + L"\"";
    const auto started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"powershell.exe", arguments.c_str(),
                                                                 folder.c_str(), SW_HIDE));
    if (started <= 32) {
        return fail(ErrorCode::IoError, L"the preview could not start", L"powershell.exe", static_cast<std::int32_t>(started));
    }
    return {};
}

} // namespace wl::app
