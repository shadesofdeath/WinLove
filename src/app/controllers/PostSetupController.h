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
    // Well-known winget packages: the "Uygulama ekle" dialog offers them one at a time, "Hazır
    // uygulamalar" as a check list. Every id was looked up in the winget source (2026-09-30).
    enum class AppCategory : std::uint8_t { Browsers, Tools, Media, Development, Communication, Games, Office };
    struct App {
        std::wstring name;
        std::wstring id;
        AppCategory category = AppCategory::Tools;
    };

    explicit PostSetupController(AppState& state);

    [[nodiscard]] const core::PostSetupPlan& plan() const; // parsed once per queue version
    [[nodiscard]] std::size_t stepCount() const { return plan().steps.size(); }

    void add(core::PostSetupStep step);
    // popularApps()[index] is a winget step of the plan already (ids compare without case).
    [[nodiscard]] bool hasApp(std::size_t index) const;
    // One queue edit for the whole pick; apps that are a step already are left out. Returns how
    // many steps were added.
    std::size_t addApps(const std::vector<std::size_t>& indexes);
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
