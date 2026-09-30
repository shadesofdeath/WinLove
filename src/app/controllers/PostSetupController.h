#pragma once
// P14 logic (docs/pages/14-post-setup.md): the post-setup plan lives in the queue as ONE
// operation (SetPostSetup, target "post-setup", value = the plan as JSON) — order and options
// travel with it, a preset carries the whole plan, Uygula writes it in one step. Nothing else is
// stored: plan() reads the operation, every edit replaces it, an empty plan removes it.
#include "app/state/AppState.h"
#include "core/postsetup/PostSetup.h"

#include <map>
#include <string>
#include <vector>

namespace wl::app {

class PostSetupController {
public:
    static constexpr const wchar_t* kTarget = L"post-setup";
    // Well-known winget packages offered by the "Uygulama ekle" dialog.
    struct App {
        std::wstring name;
        std::wstring id;
    };

    explicit PostSetupController(AppState& state);

    [[nodiscard]] const core::PostSetupPlan& plan() const; // parsed once per queue version
    [[nodiscard]] std::size_t stepCount() const { return plan().steps.size(); }

    void add(core::PostSetupStep step);
    void replace(std::size_t index, core::PostSetupStep step);
    void remove(std::size_t index);
    // Moves a step up (-1) or down (+1); false at the ends.
    bool move(std::size_t index, int delta);
    void toggleWait(std::size_t index); // command steps only
    void setWhen(core::PostSetupPlan::When when);
    void setContinueOnError(bool value);

    [[nodiscard]] static const std::vector<App>& popularApps();
    // Pure mapping, unit-tested. `payloadBytes`: what the copy steps add to the image.
    [[nodiscard]] static core::ops::Operation operationFor(const core::PostSetupPlan& plan, std::uint64_t payloadBytes);

private:
    void store(core::PostSetupPlan plan);

    AppState& m_state;
    // "Çalıştırma" / "Hata olursa devam et" chosen while there is no step yet (nothing is queued
    // for an empty plan): they go into the plan with the first step.
    core::PostSetupPlan::When m_nextWhen = core::PostSetupPlan::When::FirstLogon;
    bool m_nextContinue = true;
    mutable core::PostSetupPlan m_cached;
    mutable std::uint64_t m_cachedVersion = ~0ull;
    std::map<std::wstring, std::uint64_t> m_sizes; // copy source → bytes (walking a folder is slow)
};

} // namespace wl::app
