#include "core/unattend/Welcome.h"

#include "base/Text.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"
#include "core/image/BootImage.h"
#include "core/image/wim/WimVerify.h"

#include <windows.h>

#include <json.hpp>

#include <algorithm>

namespace wl::core {

namespace {

constexpr wchar_t kScriptFile[] = LR"(ProgramData\WinLove\Oobe\oobe.ps1)";
constexpr wchar_t kJsonFile[] = LR"(ProgramData\WinLove\Oobe\oobe.json)";

std::string text(const WelcomePlan& plan, const char* key) {
    for (const auto& [k, v] : plan.texts) {
        if (k == key) {
            return utf8::fromWide(v);
        }
    }
    return {};
}

// "Az veri": what the strict level writes (HKLM policies; the script writes DWORDs).
nlohmann::json strictWrites() {
    auto write = [](const char* key, const char* name, int value) {
        return nlohmann::json{{"key", key}, {"name", name}, {"value", value}};
    };
    return nlohmann::json::array({write(R"(HKLM:\SOFTWARE\Policies\Microsoft\Windows\DataCollection)", "AllowTelemetry", 1),
                                  write(R"(HKLM:\SOFTWARE\Policies\Microsoft\Windows\AdvertisingInfo)", "DisabledByGroupPolicy", 1),
                                  write(R"(HKLM:\SOFTWARE\Policies\Microsoft\Windows\System)", "PublishUserActivities", 0),
                                  write(R"(HKLM:\SOFTWARE\Policies\Microsoft\InputPersonalization)", "AllowInputPersonalization", 0)});
}

// The habits page (D-085): the settings catalog's keys (file-ext, hidden-files, launch-to,
// classic-context, taskbar-search, task-view, widgets). "on" / "off": what the switch's state writes;
// HKCU goes to the new account (Default profile + its first sign-in), HKLM at once.
nlohmann::json welcomePrefs(const WelcomePlan& plan) {
    constexpr char kAdvanced[] = R"(HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced)";
    auto write = [](const char* key, const char* name, int value) { return nlohmann::json{{"key", key}, {"name", name}, {"value", value}}; };
    auto pref = [&](const char* id, const char* nameKey, const char* detailKey, bool on, nlohmann::json onWrites, nlohmann::json offWrites) {
        return nlohmann::json{{"id", id}, {"name", text(plan, nameKey)}, {"detail", text(plan, detailKey)}, {"default", on},
                              {"on", std::move(onWrites)}, {"off", std::move(offWrites)}};
    };
    const auto none = nlohmann::json::array();
    auto classic = pref("classic", "prefClassic", "prefClassicDetail", false,
                        nlohmann::json::array({{{"key", R"(HKCU\Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\InprocServer32)"},
                                                {"name", ""}, {"type", "sz"}, {"value", ""}}}),
                        none);
    classic["windows11"] = true;
    auto widgets = pref("widgets", "prefWidgets", "prefWidgetsDetail", true, none,
                        nlohmann::json::array({write(R"(HKLM\SOFTWARE\Policies\Microsoft\Dsh)", "AllowNewsAndInterests", 0),
                                               write(R"(HKLM\SOFTWARE\Policies\Microsoft\Windows\Windows Feeds)", "EnableFeeds", 0)}));
    widgets["name10"] = text(plan, "prefWidgets10");
    return nlohmann::json::array({
        pref("ext", "prefExt", "prefExtDetail", true, nlohmann::json::array({write(kAdvanced, "HideFileExt", 0)}),
             nlohmann::json::array({write(kAdvanced, "HideFileExt", 1)})),
        pref("hidden", "prefHidden", "prefHiddenDetail", false, nlohmann::json::array({write(kAdvanced, "Hidden", 1)}), none),
        pref("thispc", "prefThisPc", "prefThisPcDetail", false, nlohmann::json::array({write(kAdvanced, "LaunchTo", 1)}), none),
        classic,
        pref("search", "prefSearch", "prefSearchDetail", true, none,
             nlohmann::json::array({write(R"(HKCU\Software\Microsoft\Windows\CurrentVersion\Search)", "SearchboxTaskbarMode", 0)})),
        pref("taskview", "prefTaskView", "prefTaskViewDetail", true, none, nlohmann::json::array({write(kAdvanced, "ShowTaskViewButton", 0)})),
        widgets,
    });
}

nlohmann::json splitFacts(const std::string& joined) {
    nlohmann::json facts = nlohmann::json::array();
    size_t start = 0;
    while (start <= joined.size()) {
        const size_t end = std::min(joined.find(';', start), joined.size());
        if (end > start) {
            facts.push_back(joined.substr(start, end - start));
        }
        start = end + 1;
    }
    return facts;
}

} // namespace

std::string welcomeJson(const WelcomePlan& plan) {
    nlohmann::json pages = nlohmann::json::array();
    for (const auto& [on, id] : {std::pair{plan.networkPage, "network"}, std::pair{plan.computerPage, "computer"},
                                 std::pair{plan.lookPage, "look"}, std::pair{plan.prefsPage, "prefs"},
                                 std::pair{plan.privacyPage, "privacy"}}) {
        if (on) {
            pages.push_back(id);
        }
    }
    nlohmann::json accents = nlohmann::json::array();
    for (const auto& [key, color] : kWelcomeAccents) {
        accents.push_back({{"name", text(plan, key)}, {"color", utf8::fromWide(color)}});
    }
    nlohmann::json texts = nlohmann::json::object();
    for (const auto& [key, value] : plan.texts) {
        texts[key] = utf8::fromWide(value);
    }
    nlohmann::json doc{
        {"format", "winlove.welcome"},
        {"version", 1},
        {"allowEmptyPassword", plan.allowEmptyPassword},
        {"defaults", {{"theme", utf8::fromWide(plan.theme)}, {"accent", utf8::fromWide(plan.accent)}, {"privacy", utf8::fromWide(plan.privacy)},
                      {"taskbar", "center"}, {"transparency", true}, {"timeZone", utf8::fromWide(plan.timeZone)}}},
        {"pages", pages},
        {"themes", nlohmann::json::array({{{"id", "dark"}, {"name", text(plan, "themeDark")}}, {{"id", "light"}, {"name", text(plan, "themeLight")}}})},
        {"accents", accents},
        {"prefs", welcomePrefs(plan)},
        {"privacy", nlohmann::json::array({{{"id", "strict"}, {"name", text(plan, "privacyStrict")}, {"detail", text(plan, "privacyStrictDetail")},
                                            {"facts", splitFacts(text(plan, "privacyStrictFacts"))}, {"writes", strictWrites()}},
                                           {{"id", "windows"}, {"name", text(plan, "privacyWindows")}, {"detail", text(plan, "privacyWindowsDetail")}, {"writes", nlohmann::json::array()}}})},
        {"texts", texts},
    };
    if (!plan.computerName.empty()) {
        doc["computerName"] = utf8::fromWide(plan.computerName);
    }
    return doc.dump(1);
}

std::vector<ops::Operation> welcomeOperations(const WelcomePlan& plan) {
    using ops::OpKind;
    using ops::Operation;
    return {Operation{OpKind::WriteFile, kScriptFile, utf8::toWide(scripts::kOobe)},
            Operation{OpKind::WriteFile, kJsonFile, utf8::toWide(welcomeJson(plan))}};
}

std::optional<WelcomePlan> welcomePlanFromOperations(const std::vector<ops::Operation>& ops) {
    for (const auto& op : ops) {
        if (op.kind != ops::OpKind::WriteFile || text::lower(op.target) != text::lower(std::wstring(kJsonFile))) {
            continue;
        }
        const auto doc = nlohmann::json::parse(utf8::fromWide(op.value), nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.welcome") {
            return std::nullopt;
        }
        WelcomePlan plan;
        const auto pages = doc.value("pages", nlohmann::json::array());
        auto shown = [&](const char* id) { return std::ranges::any_of(pages, [&](const auto& p) { return p.is_string() && p.template get<std::string>() == id; }); };
        plan.networkPage = shown("network");
        plan.computerPage = shown("computer");
        plan.lookPage = shown("look");
        plan.prefsPage = shown("prefs");
        plan.privacyPage = shown("privacy");
        plan.allowEmptyPassword = doc.value("allowEmptyPassword", true);
        const auto defaults = doc.value("defaults", nlohmann::json::object());
        plan.theme = utf8::toWide(defaults.value("theme", std::string("dark")));
        plan.accent = utf8::toWide(defaults.value("accent", std::string("#0078D4")));
        plan.privacy = utf8::toWide(defaults.value("privacy", std::string("strict")));
        plan.timeZone = utf8::toWide(defaults.value("timeZone", std::string{}));
        plan.computerName = utf8::toWide(doc.value("computerName", std::string{}));
        for (const auto& [key, value] : doc.value("texts", nlohmann::json::object()).items()) {
            if (value.is_string()) {
                plan.texts.emplace_back(key, utf8::toWide(value.get<std::string>()));
            }
        }
        return plan;
    }
    return std::nullopt;
}

std::vector<std::pair<ops::OpKind, std::wstring>> welcomeSlots() {
    return {{ops::OpKind::WriteFile, kScriptFile}, {ops::OpKind::WriteFile, kJsonFile}};
}

Result<std::vector<int>> editionsWithoutWelcome(const SourceInfo& source, int onlyIndex) {
    std::vector<int> missing;
    std::shared_ptr<const ByteSource> bytes;
    for (const auto& image : source.install.images) {
        if (onlyIndex > 0 && image.index != onlyIndex) {
            continue;
        }
        if (!bytes) {
            auto opened = openInstallImage(source);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            bytes = *opened;
        }
        auto found = wimFileExists(*bytes, image.index, kScriptFile);
        if (!found) {
            return std::unexpected(found.error());
        }
        if (!*found) {
            missing.push_back(image.index);
        }
    }
    return missing;
}

std::wstring welcomeSetupCommand() {
    // Synchronous: Setup waits for the pages and the account, then goes on (D-085).
    return LR"(cmd /c powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%ProgramData%\WinLove\Oobe\oobe.ps1")";
}

} // namespace wl::core
