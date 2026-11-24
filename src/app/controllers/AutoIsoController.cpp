#include "app/controllers/AutoIsoController.h"

#include "base/Log.h"
#include "base/Text.h"

#include <algorithm>
#include <format>

namespace wl::app {

using Run = AppState::ApplyRun;
using AutoIso = Run::AutoIso;

AutoIso::Skip autoIsoSkip(const Run& run) {
    if (run.error || !run.result) {
        return AutoIso::Skip::RunFailed;
    }
    const auto& r = *run.result;
    if (r.commitError) {
        return AutoIso::Skip::RunFailed;
    }
    if (!r.report.completed) {
        return AutoIso::Skip::Stopped; // a stop, or DISM's session lost (D-131)
    }
    if (r.held || r.kept || !r.committed) {
        return AutoIso::Skip::NotSaved;
    }
    if (std::ranges::any_of(run.extras, [](const Run::ExtraEdition& e) { return e.state != Run::ExtraEdition::State::Done; })) {
        return AutoIso::Skip::Edition; // D-055: the ISO would carry that edition unchanged
    }
    return AutoIso::Skip::None;
}

AutoIsoController::AutoIsoController(AppState& state, Hooks hooks) : m_state(state), m_hooks(std::move(hooks)) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Iso) {
            isoChanged();
        }
    });
}

AutoIsoController::~AutoIsoController() {
    m_state.unsubscribe(m_subscription);
}

std::optional<IsoController::Blocker> AutoIsoController::unavailable() const {
    return IsoController::sourceBlocker(m_state);
}

std::filesystem::path AutoIsoController::output() const {
    const auto& source = m_state.source();
    const bool ours = source && !m_name.empty() && text::iequals(m_nameSource.wstring(), source->path.wstring());
    return IsoController::defaultFolder(m_state) / (ours ? m_name : IsoController::defaultFileName(m_state));
}

void AutoIsoController::setOutput(const std::filesystem::path& file) {
    if (file.empty()) {
        return;
    }
    std::filesystem::path name = file.filename();
    if (!text::iendsWith(name.wstring(), L".iso")) {
        name += L".iso";
    }
    m_name = name.wstring();
    m_nameSource = m_state.source() ? m_state.source()->path : std::filesystem::path();
    m_state.setIsoFolder(file.parent_path()); // ISO Oluştur's folder as well (saved)
}

void AutoIsoController::attach() {
    auto& run = m_state.applyRunMutable();
    if (!run || run->stage == Run::Stage::Done || run->autoIso || !m_state.settings().isoAfterApply || unavailable()) {
        return;
    }
    AutoIso plan;
    plan.output = output();
    run->autoIso = plan;
    log::info("apply", L"the setup ISO follows when the image is saved: " + plan.output.wstring());
    m_state.notifyApply();
}

void AutoIsoController::runFinished() {
    auto& run = m_state.applyRunMutable();
    if (!run || run->stage != Run::Stage::Done || !run->autoIso || run->autoIso->state != AutoIso::State::Waiting) {
        return;
    }
    auto& iso = *run->autoIso;
    auto notStarted = [&](AutoIso::Skip why, int blocker = -1) {
        iso.state = AutoIso::State::NotStarted;
        iso.skip = why;
        iso.blocker = blocker;
        static constexpr const wchar_t* kWhy[] = {L"-", L"the run failed", L"the run was stopped", L"the image was not saved",
                                                  L"an edition was not saved", L"ISO Oluştur refuses the source", L"the welcome is missing"};
        log::info("apply", std::format(L"no setup ISO after the run: {}", kWhy[static_cast<std::size_t>(why)]));
        m_state.notifyApply();
    };
    if (const AutoIso::Skip skip = autoIsoSkip(*run); skip != AutoIso::Skip::None) {
        notStarted(skip);
        return;
    }
    if (const auto blocker = m_hooks.blocker ? m_hooks.blocker() : std::nullopt) {
        notStarted(AutoIso::Skip::Blocked, static_cast<int>(*blocker));
        return;
    }
    if (m_hooks.welcomeMissing && !m_hooks.welcomeMissing().empty()) {
        notStarted(AutoIso::Skip::Welcome);
        return;
    }
    iso.state = AutoIso::State::Running;
    const std::filesystem::path output = iso.output;
    m_state.notifyApply();
    log::info("apply", L"the image is saved: building the setup ISO " + output.wstring());
    if (m_hooks.start) {
        m_hooks.start(IsoController::defaultRequest(m_state, output));
    }
    // The ISO engine refused at its door (nothing started): said, not left "running".
    auto& after = m_state.applyRunMutable();
    const auto& isoRun = m_state.isoRun();
    if (after && after->autoIso && after->autoIso->state == AutoIso::State::Running &&
        !(isoRun && isoRun->afterApply && (isoRun->running || isoRun->result || isoRun->error))) {
        after->autoIso->state = AutoIso::State::Failed;
        after->autoIso->error = Error{ErrorCode::Unknown, L"the ISO build did not start", output.wstring()};
        m_state.notifyApply();
    }
}

void AutoIsoController::isoChanged() {
    auto& run = m_state.applyRunMutable();
    const auto& isoRun = m_state.isoRun();
    if (!run || !run->autoIso || run->autoIso->state != AutoIso::State::Running || !isoRun || !isoRun->afterApply ||
        isoRun->running) {
        return;
    }
    auto& iso = *run->autoIso;
    if (isoRun->result) {
        iso.state = AutoIso::State::Done;
        iso.bytes = isoRun->result->bytes;
    } else if (isoRun->error && isoRun->error->code == ErrorCode::Cancelled) {
        iso.state = AutoIso::State::Cancelled;
    } else {
        iso.state = AutoIso::State::Failed;
        iso.error = isoRun->error.value_or(Error{ErrorCode::Unknown, L"the ISO build failed", iso.output.wstring()});
    }
    m_state.notifyApply();
}

} // namespace wl::app
