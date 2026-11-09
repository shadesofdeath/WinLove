#include "core/ops/UpdateGuard.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"
#include "core/image/ImageFiles.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/image/SystemComponents.h"
#include "core/postsetup/SetupScripts.h"

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <map>

namespace wl::core::ops {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kPathsPerComponent = 16; // enough to see it came back

std::wstring lower(std::wstring_view s) {
    std::wstring out(s);
    std::ranges::transform(out, out.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return out;
}

// Keys whose values Windows itself or setup owns, or that run something once: never put back.
bool guardedKey(std::wstring_view key) {
    const std::wstring k = lower(key);
    if (!k.starts_with(L"hklm\\")) {
        return false; // per-user values, HKCR (the user's file types), HKU
    }
    for (const std::wstring_view part : {L"\\runonce", L"\\currentversion\\setup", L"hklm\\system\\setup", L"\\oobe", L"\\winlogon"}) {
        if (k.find(part) != std::wstring::npos) {
            return false;
        }
    }
    return true;
}

std::wstring utf16Text(const std::vector<std::uint8_t>& data) {
    std::wstring text(data.size() / 2, L'\0');
    if (!text.empty()) {
        std::memcpy(text.data(), data.data(), text.size() * 2);
    }
    return text;
}

std::optional<UpdateGuardValue> guardValueOf(const RegistryWrite& write) {
    UpdateGuardValue v{write.key, write.name, {}, {}};
    if (write.kind == RegistryWrite::Kind::DeleteValue) {
        v.type = L"delete";
        return v;
    }
    if (write.kind != RegistryWrite::Kind::Set) {
        return std::nullopt; // key deletions / bare keys: nothing a value check can hold
    }
    switch (write.type) {
    case 1: // REG_SZ
    case 2: { // REG_EXPAND_SZ
        std::wstring text = utf16Text(write.data);
        while (!text.empty() && text.back() == L'\0') {
            text.pop_back();
        }
        v.type = write.type == 1 ? L"sz" : L"expandsz";
        v.data = std::move(text);
        return v;
    }
    case 7: { // REG_MULTI_SZ
        std::wstring text = utf16Text(write.data);
        while (!text.empty() && text.back() == L'\0') {
            text.pop_back();
        }
        std::ranges::replace(text, L'\0', L'\n');
        v.type = L"multisz";
        v.data = std::move(text);
        return v;
    }
    case 4: { // REG_DWORD
        if (write.data.size() < 4) {
            return std::nullopt;
        }
        std::uint32_t n = 0;
        std::memcpy(&n, write.data.data(), 4);
        v.type = L"dword";
        v.data = std::to_wstring(n);
        return v;
    }
    case 11: { // REG_QWORD
        if (write.data.size() < 8) {
            return std::nullopt;
        }
        std::uint64_t n = 0;
        std::memcpy(&n, write.data.data(), 8);
        v.type = L"qword";
        v.data = std::to_wstring(n);
        return v;
    }
    case 3: { // REG_BINARY
        v.type = L"binary";
        for (const std::uint8_t b : write.data) {
            v.data += std::format(L"{:02x}", b);
        }
        return v;
    }
    default: return std::nullopt;
    }
}

// "Microsoft.BingNews_4.55.0.0_x64__8wekyb3d8bbwe" → name + family; an identity alone → name.
UpdateGuardApp appOf(std::wstring_view target) {
    UpdateGuardApp app;
    const auto first = target.find(L'_');
    app.name = std::wstring(target.substr(0, first));
    if (first != std::wstring_view::npos) {
        const auto last = target.rfind(L'_');
        const std::wstring_view publisher = target.substr(last + 1);
        if (!publisher.empty() && last > first) {
            app.family = app.name + L"_" + std::wstring(publisher);
        }
    }
    return app;
}

template <typename T, typename Key>
void upsert(std::vector<T>& list, const T& item, Key key) {
    const auto it = std::ranges::find_if(list, [&](const T& x) { return key(x) == key(item); });
    if (it == list.end()) {
        list.push_back(item);
    } else {
        *it = item; // a later step wins
    }
}

} // namespace

UpdateGuardPlan updateGuardPlan(const std::vector<Operation>& ran) {
    UpdateGuardPlan plan;
    const auto serviceKey = [](const UpdateGuardService& s) { return lower(s.name); };
    const auto valueKey = [](const UpdateGuardValue& v) { return lower(v.key) + L"::" + lower(v.name); };
    const auto appKey = [](const UpdateGuardApp& a) { return lower(a.name); };
    for (const Operation& op : ran) {
        switch (op.kind) {
        case OpKind::SetServiceStart: {
            const auto start = startTypeFromKey(op.value);
            if (!start || *start == StartType::Boot || *start == StartType::System) {
                break; // drivers' start types are not kept
            }
            UpdateGuardService s{op.target, 4, false};
            switch (*start) {
            case StartType::Auto: s.start = 2; break;
            case StartType::AutoDelayed:
                s.start = 2;
                s.delayed = true;
                break;
            case StartType::Manual: s.start = 3; break;
            default: s.start = 4; break;
            }
            upsert(plan.services, s, serviceKey);
            break;
        }
        case OpKind::SetRegistryValue: {
            auto write = registryWriteFrom(op.target, op.value);
            if (!write || !guardedKey(write->key)) {
                break;
            }
            if (auto value = guardValueOf(*write)) {
                upsert(plan.values, *value, valueKey);
            }
            break;
        }
        case OpKind::RemoveAppx: {
            const UpdateGuardApp app = appOf(op.target);
            if (!app.name.empty()) {
                upsert(plan.apps, app, appKey);
            }
            break;
        }
        case OpKind::SetTaskState: {
            const auto it = std::ranges::find_if(plan.tasks, [&](const std::wstring& t) { return text::iequals(t, op.target); });
            if (op.value == L"enabled") {
                if (it != plan.tasks.end()) {
                    plan.tasks.erase(it); // switched back on later in the queue
                }
            } else if (it == plan.tasks.end()) {
                plan.tasks.push_back(op.target);
            }
            break;
        }
        case OpKind::RemoveComponent: {
            const auto recipe = componentRecipeFromJson(utf8::fromWide(op.value));
            if (!recipe) {
                break;
            }
            for (const auto& identity : recipe->appx) {
                upsert(plan.apps, UpdateGuardApp{identity, {}}, appKey);
            }
            if (recipe->paths.empty()) {
                break;
            }
            UpdateGuardComponent c{recipe->title.empty() ? op.target : recipe->title, {}};
            for (std::size_t i = 0; i < recipe->paths.size() && i < kPathsPerComponent; ++i) {
                c.paths.push_back(recipe->paths[i]);
            }
            plan.components.push_back(std::move(c));
            break;
        }
        default: break;
        }
    }
    return plan;
}

std::string updateGuardToJson(const UpdateGuardPlan& plan) {
    Json doc{{"format", "winlove.updateguard"}, {"version", 1}, {"mode", plan.reportOnly ? "report" : "repair"}};
    Json services = Json::array();
    for (const auto& s : plan.services) {
        services.push_back({{"name", utf8::fromWide(s.name)}, {"start", s.start}, {"delayed", s.delayed}});
    }
    Json values = Json::array();
    for (const auto& v : plan.values) {
        values.push_back({{"key", utf8::fromWide(v.key)},
                          {"name", utf8::fromWide(v.name)},
                          {"type", utf8::fromWide(v.type)},
                          {"data", utf8::fromWide(v.data)}});
    }
    Json apps = Json::array();
    for (const auto& a : plan.apps) {
        apps.push_back({{"name", utf8::fromWide(a.name)}, {"family", utf8::fromWide(a.family)}});
    }
    Json tasks = Json::array();
    for (const auto& t : plan.tasks) {
        tasks.push_back(utf8::fromWide(t));
    }
    Json components = Json::array();
    for (const auto& c : plan.components) {
        Json paths = Json::array();
        for (const auto& p : c.paths) {
            paths.push_back(utf8::fromWide(p));
        }
        components.push_back({{"title", utf8::fromWide(c.title)}, {"paths", std::move(paths)}});
    }
    doc["services"] = std::move(services);
    doc["values"] = std::move(values);
    doc["apps"] = std::move(apps);
    doc["tasks"] = std::move(tasks);
    doc["components"] = std::move(components);
    return doc.dump(1);
}

Result<UpdateGuardPlan> updateGuardFromJson(std::string_view json) {
    const Json doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || doc.value("format", std::string()) != "winlove.updateguard") {
        return fail(ErrorCode::InvalidArgument, L"not an update guard file");
    }
    const auto str = [](const Json& o, const char* key) {
        return o.contains(key) && o[key].is_string() ? utf8::toWide(o[key].get<std::string>()) : std::wstring();
    };
    const auto list = [&](const char* key) { return doc.contains(key) && doc[key].is_array() ? doc[key] : Json::array(); };
    UpdateGuardPlan plan;
    plan.reportOnly = doc.value("mode", std::string()) == "report";
    for (const auto& s : list("services")) {
        if (s.is_object()) {
            plan.services.push_back({str(s, "name"), s.value("start", 4u), s.value("delayed", false)});
        }
    }
    for (const auto& v : list("values")) {
        if (v.is_object()) {
            plan.values.push_back({str(v, "key"), str(v, "name"), str(v, "type"), str(v, "data")});
        }
    }
    for (const auto& a : list("apps")) {
        if (a.is_object()) {
            plan.apps.push_back({str(a, "name"), str(a, "family")});
        }
    }
    for (const auto& t : list("tasks")) {
        if (t.is_string()) {
            plan.tasks.push_back(utf8::toWide(t.get<std::string>()));
        }
    }
    for (const auto& c : list("components")) {
        if (!c.is_object()) {
            continue;
        }
        UpdateGuardComponent component{str(c, "title"), {}};
        if (c.contains("paths") && c["paths"].is_array()) {
            for (const auto& p : c["paths"]) {
                if (p.is_string()) {
                    component.paths.push_back(utf8::toWide(p.get<std::string>()));
                }
            }
        }
        plan.components.push_back(std::move(component));
    }
    return plan;
}

Result<void> writeUpdateGuard(const std::filesystem::path& mountDir, const UpdateGuardPlan& plan) {
    if (plan.empty()) {
        log::info("apply", L"update guard: nothing to keep");
        return {};
    }
    const std::wstring folder(kUpdateGuardFolder);
    if (auto r = writeImageFile(mountDir, folder + L"\\updateguard.ps1", scripts::kUpdateguard); !r) {
        return r;
    }
    if (auto r = writeImageFile(mountDir, folder + L"\\guard.json", updateGuardToJson(plan)); !r) {
        return r;
    }
    if (auto r = writeSetupScript(mountDir, kUpdateGuardSetupScript,
                                  L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "
                                  L"\"%ProgramData%\\WinLove\\Guard\\updateguard.ps1\" -Register\r\n",
                                  "WinLove: update guard (D-125)");
        !r) {
        return r;
    }
    log::info("apply", std::format(L"update guard written: {} service(s), {} value(s), {} app(s), {} task(s), {} component(s)",
                                   plan.services.size(), plan.values.size(), plan.apps.size(), plan.tasks.size(),
                                   plan.components.size()));
    return {};
}

} // namespace wl::core::ops
