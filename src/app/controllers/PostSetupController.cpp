#include "app/controllers/PostSetupController.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <algorithm>
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
    if (plan.empty()) {
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

void PostSetupController::setPrograms(std::vector<core::PostSetupProgram> programs,
                                      std::vector<std::pair<std::wstring, std::wstring>> texts) {
    PostSetupPlan next = plan();
    next.programs = std::move(programs);
    next.programTexts = next.programs.empty() ? decltype(texts){} : std::move(texts);
    store(std::move(next));
}

void PostSetupController::setOfflinePrograms(bool offline) {
    PostSetupPlan next = plan();
    if (next.offlinePrograms == offline) {
        return;
    }
    next.offlinePrograms = offline;
    store(std::move(next));
}

bool PostSetupController::offlinePrograms() const {
    return plan().offlinePrograms;
}

void PostSetupController::add(PostSetupStep step) {
    PostSetupPlan next = plan();
    next.steps.push_back(std::move(step));
    store(std::move(next));
}

bool PostSetupController::hasCommand(std::size_t index) const {
    const auto& commands = readyCommands();
    return index < commands.size() && std::ranges::any_of(plan().steps, [&](const PostSetupStep& step) {
               return step.type == PostSetupStep::Type::Command && step.source == commands[index].command;
           });
}

std::size_t PostSetupController::addCommands(const std::vector<std::size_t>& indexes, Language language) {
    const auto& commands = readyCommands();
    PostSetupPlan next = plan();
    std::size_t added = 0;
    for (const std::size_t index : indexes) {
        if (index >= commands.size() || std::ranges::any_of(next.steps, [&](const PostSetupStep& step) {
                return step.type == PostSetupStep::Type::Command && step.source == commands[index].command;
            })) {
            continue;
        }
        next.steps.push_back(
            PostSetupStep{PostSetupStep::Type::Command, commands[index].name(language), commands[index].command, {}, true});
        ++added;
    }
    if (added > 0) {
        store(std::move(next));
    }
    return added;
}

const std::vector<PostSetupController::ReadyCommand>& PostSetupController::readyCommands() {
    using enum CommandCategory;
    // Power plans are copied from Windows' own definitions (hidden on some PCs, e.g. Modern
    // Standby laptops, so /setactive on the built-in GUID alone would fail there) under fixed
    // GUIDs; a second run finds the copy and just activates it. Firewall groups by their resource
    // id, never by name: "Ağ Bulma" on a Turkish Windows (checked on this PC, 2026-09-30).
    static const std::vector<ReadyCommand> commands{
        {L"Güç planı: Yüksek performans", L"Power plan: High performance",
         L"powercfg /duplicatescheme 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c 5a1e0f00-7e57-4e1d-9f00-000000000001 & "
         L"powercfg /setactive 5a1e0f00-7e57-4e1d-9f00-000000000001",
         Power},
        {L"Güç planı: Nihai performans", L"Power plan: Ultimate performance",
         L"powercfg /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61 5a1e0f00-7e57-4e1d-9f00-000000000002 & "
         L"powercfg /setactive 5a1e0f00-7e57-4e1d-9f00-000000000002",
         Power},
        {L"Uyku: fişe takılıyken hiçbir zaman", L"Sleep: never when plugged in", L"powercfg /change standby-timeout-ac 0", Power},
        {L"Ekranı kapat: fişe takılıyken 30 dk", L"Turn off the display: 30 min when plugged in",
         L"powercfg /change monitor-timeout-ac 30", Power},
        {L"Disk kapanmasın (fişe takılıyken)", L"Never turn off the disk (plugged in)", L"powercfg /change disk-timeout-ac 0",
         Power},
        {L"USB seçmeli askıya almayı kapat", L"Turn off USB selective suspend",
         L"powercfg /setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0 & "
         L"powercfg /setdcvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0 & "
         L"powercfg /setactive SCHEME_CURRENT",
         Power},
        {L"Ağ bulmayı aç", L"Turn on network discovery",
         L"netsh advfirewall firewall set rule group=\"@FirewallAPI.dll,-32752\" new enable=Yes", Network},
        {L"Dosya ve yazıcı paylaşımını aç", L"Turn on file and printer sharing",
         L"netsh advfirewall firewall set rule group=\"@FirewallAPI.dll,-28502\" new enable=Yes", Network},
        {L"Uzak Masaüstü'nü aç (Pro ve üstü)", L"Turn on Remote Desktop (Pro and above)",
         L"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server\" /v fDenyTSConnections /t REG_DWORD /d 0 /f & "
         L"netsh advfirewall firewall set rule group=\"@FirewallAPI.dll,-28752\" new enable=Yes",
         Network},
        {L"Ping'e yanıt ver (ICMPv4)", L"Answer ping (ICMPv4)",
         L"netsh advfirewall firewall add rule name=\"WinLove ICMPv4\" dir=in action=allow protocol=icmpv4:8,any", Network},
        {L"Bağlı ağları Özel yap", L"Make connected networks Private",
         L"powershell -NoProfile -Command \"Get-NetConnectionProfile | Set-NetConnectionProfile -NetworkCategory Private\"",
         Network},
    };
    return commands;
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
    using enum AppCategory;
    static const std::vector<App> apps{
        {L"Google Chrome", L"Google.Chrome", Browsers},
        {L"Firefox", L"Mozilla.Firefox", Browsers},
        {L"Brave", L"Brave.Brave", Browsers},
        {L"Opera", L"Opera.Opera", Browsers},
        {L"Vivaldi", L"Vivaldi.Vivaldi", Browsers},

        {L"7-Zip", L"7zip.7zip", Tools},
        {L"WinRAR", L"RARLab.WinRAR", Tools},
        {L"Everything", L"voidtools.Everything", Tools},
        {L"PowerToys", L"Microsoft.PowerToys", Tools},
        {L"Notepad++", L"Notepad++.Notepad++", Tools},
        {L"Windows Terminal", L"Microsoft.WindowsTerminal", Tools},
        {L"ShareX", L"ShareX.ShareX", Tools},
        {L"qBittorrent", L"qBittorrent.qBittorrent", Tools},
        {L"Rufus", L"Rufus.Rufus", Tools},
        {L"CPU-Z", L"CPUID.CPU-Z", Tools},
        {L"HWiNFO", L"REALiX.HWiNFO", Tools},
        {L"CrystalDiskInfo", L"CrystalDewWorld.CrystalDiskInfo", Tools},
        {L"AnyDesk", L"AnyDesk.AnyDesk", Tools},
        {L"TeamViewer", L"TeamViewer.TeamViewer", Tools},

        {L"VLC", L"VideoLAN.VLC", Media},
        {L"MPC-HC", L"clsid2.mpc-hc", Media},
        {L"Spotify", L"Spotify.Spotify", Media},
        {L"OBS Studio", L"OBSProject.OBSStudio", Media},
        {L"IrfanView", L"IrfanSkiljan.IrfanView", Media},
        {L"GIMP", L"GIMP.GIMP.3", Media},
        {L"Audacity", L"Audacity.Audacity", Media},
        {L"HandBrake", L"HandBrake.HandBrake", Media},

        {L"VS Code", L"Microsoft.VisualStudioCode", Development},
        {L"Git", L"Git.Git", Development},
        {L"Python 3.13", L"Python.Python.3.13", Development},
        {L"Node.js LTS", L"OpenJS.NodeJS.LTS", Development},
        {L"Docker Desktop", L"Docker.DockerDesktop", Development},
        {L"PuTTY", L"PuTTY.PuTTY", Development},
        {L"WinSCP", L"WinSCP.WinSCP", Development},

        {L"Discord", L"Discord.Discord", Communication},
        {L"Telegram", L"Telegram.TelegramDesktop", Communication},
        {L"Zoom", L"Zoom.Zoom", Communication},

        {L"Steam", L"Valve.Steam", Games},
        {L"Epic Games Launcher", L"EpicGames.EpicGamesLauncher", Games},

        {L"LibreOffice", L"TheDocumentFoundation.LibreOffice", Office},
        {L"Adobe Acrobat Reader", L"Adobe.Acrobat.Reader.64-bit", Office},
        {L"SumatraPDF", L"SumatraPDF.SumatraPDF", Office},
    };
    return apps;
}

} // namespace wl::app
