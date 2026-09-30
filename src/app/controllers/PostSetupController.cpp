#include "app/controllers/PostSetupController.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <utility>

namespace wl::app {

using core::PostSetupPlan;
using core::PostSetupStep;
using core::ops::OpKind;
using core::ops::Operation;

PostSetupController::PostSetupController(AppState& state) : m_state(state) {}

const PostSetupPlan& PostSetupController::plan() const {
    const std::uint64_t version = m_state.changes().version();
    if (version != m_cachedVersion) {
        m_cachedVersion = version;
        m_cached = {};
        m_cached.when = m_nextWhen;
        m_cached.continueOnError = m_nextContinue;
        if (const auto* op = m_state.changes().find(OpKind::SetPostSetup, kTarget)) {
            if (auto parsed = core::postSetupFromJson(utf8::fromWide(op->value))) {
                m_cached = std::move(*parsed);
            } else {
                log::warn("app", describe(parsed.error())); // a preset edited by hand
            }
        }
    }
    return m_cached;
}

Operation PostSetupController::operationFor(const PostSetupPlan& plan, std::uint64_t payloadBytes) {
    Operation op{OpKind::SetPostSetup, kTarget, utf8::toWide(core::postSetupToJson(plan))};
    // Commands run with administrator / SYSTEM rights on the installed system.
    op.risk = core::ops::Risk::Medium;
    op.sizeDelta = static_cast<std::int64_t>(payloadBytes);
    return op;
}

void PostSetupController::store(PostSetupPlan plan) {
    m_cachedVersion = ~0ull; // also when the queue does not change (options of an empty plan)
    if (plan.steps.empty()) {
        m_state.unqueue(OpKind::SetPostSetup, kTarget);
        return;
    }
    std::uint64_t payload = 0;
    for (const auto& step : plan.steps) {
        if (step.type != PostSetupStep::Type::Copy || step.source.empty()) {
            continue;
        }
        auto known = m_sizes.find(step.source);
        if (known == m_sizes.end()) {
            PostSetupPlan one;
            one.steps.push_back(step);
            known = m_sizes.emplace(step.source, core::postSetupPayloadBytes(one)).first;
        }
        payload += known->second;
    }
    m_state.queue(operationFor(plan, payload));
}

void PostSetupController::add(PostSetupStep step) {
    PostSetupPlan next = plan();
    next.steps.push_back(std::move(step));
    store(std::move(next));
}

void PostSetupController::replace(std::size_t index, PostSetupStep step) {
    PostSetupPlan next = plan();
    if (index < next.steps.size()) {
        m_sizes.erase(step.source); // the folder may have changed since it was added
        next.steps[index] = std::move(step);
        store(std::move(next));
    }
}

void PostSetupController::remove(std::size_t index) {
    PostSetupPlan next = plan();
    if (index < next.steps.size()) {
        next.steps.erase(next.steps.begin() + static_cast<std::ptrdiff_t>(index));
        store(std::move(next));
    }
}

bool PostSetupController::move(std::size_t index, int delta) {
    PostSetupPlan next = plan();
    const std::size_t count = next.steps.size();
    if (index >= count || (delta < 0 && index == 0) || (delta > 0 && index + 1 >= count)) {
        return false;
    }
    std::swap(next.steps[index], next.steps[delta < 0 ? index - 1 : index + 1]);
    store(std::move(next));
    return true;
}

void PostSetupController::toggleWait(std::size_t index) {
    PostSetupPlan next = plan();
    if (index < next.steps.size() && next.steps[index].type == PostSetupStep::Type::Command) {
        next.steps[index].wait = !next.steps[index].wait;
        store(std::move(next));
    }
}

void PostSetupController::setWhen(PostSetupPlan::When when) {
    PostSetupPlan next = plan();
    m_nextWhen = when;
    if (next.when != when) {
        next.when = when;
        store(std::move(next));
    }
}

void PostSetupController::setContinueOnError(bool value) {
    PostSetupPlan next = plan();
    m_nextContinue = value;
    if (next.continueOnError != value) {
        next.continueOnError = value;
        store(std::move(next));
    }
}

const std::vector<PostSetupController::App>& PostSetupController::popularApps() {
    static const std::vector<App> apps{
        {L"7-Zip", L"7zip.7zip"},
        {L"Firefox", L"Mozilla.Firefox"},
        {L"Google Chrome", L"Google.Chrome"},
        {L"Brave", L"Brave.Brave"},
        {L"VLC", L"VideoLAN.VLC"},
        {L"Notepad++", L"Notepad++.Notepad++"},
        {L"VS Code", L"Microsoft.VisualStudioCode"},
        {L"Git", L"Git.Git"},
        {L"PowerToys", L"Microsoft.PowerToys"},
        {L"Windows Terminal", L"Microsoft.WindowsTerminal"},
        {L"Everything", L"voidtools.Everything"},
        {L"qBittorrent", L"qBittorrent.qBittorrent"},
        {L"Discord", L"Discord.Discord"},
        {L"Steam", L"Valve.Steam"},
        {L"Spotify", L"Spotify.Spotify"},
        {L"OBS Studio", L"OBSProject.OBSStudio"},
    };
    return apps;
}

} // namespace wl::app
