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
    // D-078: the programs of the Programlar page — in the same plan, so in the same queue operation
    // (a preset carries them, Uygula writes them with the steps).
    [[nodiscard]] const std::vector<core::PostSetupProgram>& programs() const { return plan().programs; }
    // `texts`: the install window's texts in the app's language; they travel with the plan.
    void setPrograms(std::vector<core::PostSetupProgram> programs, std::vector<std::pair<std::wstring, std::wstring>> texts);
    // D-104: install the programs offline (winget download at Apply, embed, local install at logon).
    void setOfflinePrograms(bool offline);
    [[nodiscard]] bool offlinePrograms() const;

    void add(core::PostSetupStep step);
    // Ready command steps ("Hazır komutlar"): power plan and network settings that are commands,
    // not registry values. They run where the plan runs (SetupComplete or the first logon).
    enum class CommandCategory : std::uint8_t { Power, Network };
    struct ReadyCommand {
        std::wstring nameTr;
        std::wstring nameEn;
        std::wstring command;
        CommandCategory category = CommandCategory::Power;
        [[nodiscard]] const std::wstring& name(Language language) const {
            return language == Language::Turkish ? nameTr : nameEn;
        }
    };
    [[nodiscard]] static const std::vector<ReadyCommand>& readyCommands();
    [[nodiscard]] bool hasCommand(std::size_t index) const; // the same command line is a step already
    std::size_t addCommands(const std::vector<std::size_t>& indexes, Language language);

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
