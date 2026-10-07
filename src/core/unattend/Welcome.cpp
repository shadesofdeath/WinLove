#include "core/unattend/Welcome.h"

#include "base/Text.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"
#include "core/image/BootImage.h"
#include "core/image/wim/WimVerify.h"

#include <windows.h>

#include <bcrypt.h>
#include <json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>

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

} // namespace

std::string welcomeJson(const WelcomePlan& plan) {
    nlohmann::json pages = nlohmann::json::array();
    for (const auto& [on, id] : {std::pair{plan.computerPage, "computer"}, std::pair{plan.lookPage, "look"},
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
        {"setupAccount", utf8::fromWide(kWelcomeAccount)},
        {"allowEmptyPassword", plan.allowEmptyPassword},
        {"defaults", {{"theme", utf8::fromWide(plan.theme)}, {"accent", utf8::fromWide(plan.accent)}, {"privacy", utf8::fromWide(plan.privacy)}}},
        {"pages", pages},
        {"themes", nlohmann::json::array({{{"id", "dark"}, {"name", text(plan, "themeDark")}}, {{"id", "light"}, {"name", text(plan, "themeLight")}}})},
        {"accents", accents},
        {"privacy", nlohmann::json::array({{{"id", "strict"}, {"name", text(plan, "privacyStrict")}, {"detail", text(plan, "privacyStrictDetail")}, {"writes", strictWrites()}},
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
        plan.computerPage = shown("computer");
        plan.lookPage = shown("look");
        plan.privacyPage = shown("privacy");
        plan.allowEmptyPassword = doc.value("allowEmptyPassword", true);
        const auto defaults = doc.value("defaults", nlohmann::json::object());
        plan.theme = utf8::toWide(defaults.value("theme", std::string("dark")));
        plan.accent = utf8::toWide(defaults.value("accent", std::string("#D4905A")));
        plan.privacy = utf8::toWide(defaults.value("privacy", std::string("strict")));
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

std::wstring welcomeFirstLogonCommand() {
    // On its own (start): the setup account's sign-in goes on, and the window waits for Windows'
    // first sign-in screen to go (it is on the Winlogon desktop, above anything).
    return LR"(cmd /c start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%ProgramData%\WinLove\Oobe\oobe.ps1")";
}

std::wstring randomWelcomePassword() {
    static constexpr wchar_t kAlphabet[] = L"ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    constexpr std::size_t kCount = std::size(kAlphabet) - 1;
    for (;;) {
        std::array<unsigned char, 20> random{};
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            continue;
        }
        std::wstring out;
        for (const unsigned char byte : random) {
            out.push_back(kAlphabet[byte % kCount]);
        }
        const bool upper = std::ranges::any_of(out, [](wchar_t c) { return std::iswupper(c) != 0; });
        const bool lower = std::ranges::any_of(out, [](wchar_t c) { return std::iswlower(c) != 0; });
        const bool digit = std::ranges::any_of(out, [](wchar_t c) { return std::iswdigit(c) != 0; });
        if (upper && lower && digit) {
            return out;
        }
    }
}

} // namespace wl::core
