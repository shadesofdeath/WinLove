#include "core/image/StartMenu.h"

#include "base/Text.h"
#include "base/Utf8.h"
#include "core/system/Com.h"

#include <windows.h>

#include <shlobj.h>
#include <wrl/client.h>

#include <json.hpp>
#include <pugixml.hpp>

#include <algorithm>
#include <cwctype>
#include <format>
#include <map>
#include <sstream>
#include <tuple>

namespace wl::core {

namespace {

constexpr wchar_t kAllUsersPrograms[] = L"ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
constexpr wchar_t kDefaultUserPrograms[] = L"Users\\Default\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs";

std::vector<std::wstring> split(std::wstring_view text, wchar_t by) {
    std::vector<std::wstring> out(1);
    for (const wchar_t c : text) {
        if (c == by) {
            out.emplace_back();
        } else {
            out.back().push_back(c);
        }
    }
    return out;
}

std::vector<int> versionOf(std::wstring_view text) {
    std::vector<int> out;
    for (const auto& part : split(text, L'.')) {
        out.push_back(part.empty() ? 0 : static_cast<int>(std::wcstol(part.c_str(), nullptr, 10)));
    }
    return out;
}

pugi::xml_node childEndingWith(const pugi::xml_node& node, std::string_view suffix) {
    for (const auto& child : node.children()) {
        const std::string_view name = child.name();
        if (name == suffix || (name.size() > suffix.size() && name.ends_with(suffix) && name[name.size() - suffix.size() - 1] == ':')) {
            return child;
        }
    }
    return {};
}

// A logo of the package for the preview: "Assets\Square44x44Logo.png" is stored scale-qualified.
std::wstring logoFile(const std::filesystem::path& packageDir, const std::filesystem::path& mountDir, const std::wstring& logo) {
    if (logo.empty()) {
        return {};
    }
    const std::filesystem::path relative(logo);
    const auto dir = packageDir / relative.parent_path();
    const std::wstring stem = text::lower(relative.stem().wstring());
    std::error_code ec;
    std::filesystem::path best;
    int bestScore = -1;
    for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        const std::wstring name = text::lower(entry.path().filename().wstring());
        if (!name.starts_with(stem) || !name.ends_with(L".png")) {
            continue;
        }
        // Prefer a plain 48 px target, then scale-200, then anything.
        int score = 1;
        if (name.find(L"targetsize-48.png") != std::wstring::npos || name.find(L"targetsize-48_altform-unplated") != std::wstring::npos) {
            score = 4;
        } else if (name.find(L"scale-200") != std::wstring::npos) {
            score = 3;
        } else if (name.find(L"scale-100") != std::wstring::npos) {
            score = 2;
        }
        if (score > bestScore) {
            bestScore = score;
            best = entry.path();
        }
    }
    if (best.empty()) {
        return {};
    }
    return std::filesystem::relative(best, mountDir, ec).wstring();
}

struct Manifested {
    std::vector<int> version;
    std::vector<StartApp> apps;
};

// The apps of one AppxManifest.xml; `family` comes from the folder name.
std::vector<StartApp> appsOf(const std::filesystem::path& manifest, const std::wstring& family, const std::filesystem::path& mountDir) {
    std::vector<StartApp> out;
    pugi::xml_document doc;
    if (!doc.load_file(manifest.c_str())) {
        return out;
    }
    const auto package = doc.child("Package");
    const auto properties = package.child("Properties");
    if (std::string_view(properties.child_value("Framework")) == "true" ||
        std::string_view(properties.child_value("ResourcePackage")) == "true") {
        return out;
    }
    const std::wstring identity = utf8::toWide(package.child("Identity").attribute("Name").value());
    std::wstring packageName = utf8::toWide(properties.child_value("DisplayName"));
    if (packageName.empty() || packageName.starts_with(L"ms-resource:")) {
        packageName = readableAppName(identity);
    }
    for (const auto& app : package.child("Applications").children()) {
        if (std::string_view(app.name()).find("Application") == std::string_view::npos) {
            continue;
        }
        const auto visual = childEndingWith(app, "VisualElements");
        if (!visual || std::string_view(visual.attribute("AppListEntry").value()) == "none") {
            continue;
        }
        std::wstring name = utf8::toWide(visual.attribute("DisplayName").value());
        if (name.empty() || name.starts_with(L"ms-resource:")) {
            name = packageName;
        }
        const std::wstring id = utf8::toWide(app.attribute("Id").value());
        if (id.empty()) {
            continue;
        }
        StartApp entry;
        entry.kind = StartApp::Kind::Packaged;
        entry.id = family + L"!" + id;
        entry.name = name;
        entry.icon = logoFile(manifest.parent_path(), mountDir, utf8::toWide(visual.attribute("Square44x44Logo").value()));
        out.push_back(std::move(entry));
    }
    return out;
}

// What a shortcut's icon is in the image: its icon location, else its target, with the
// installed system's folders mapped into the image ("%windir%\explorer.exe" → "Windows\explorer.exe").
std::pair<std::wstring, int> linkIcon(const std::filesystem::path& mountDir, const std::filesystem::path& link) {
    Microsoft::WRL::ComPtr<IShellLinkW> shell;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell))) ||
        FAILED(shell.As(&file)) || FAILED(file->Load(link.c_str(), STGM_READ))) {
        return {};
    }
    wchar_t buffer[MAX_PATH * 2] = {};
    int index = 0;
    std::wstring location;
    if (SUCCEEDED(shell->GetIconLocation(buffer, static_cast<int>(std::size(buffer)), &index)) && buffer[0]) {
        location = buffer;
    } else if (SUCCEEDED(shell->GetPath(buffer, static_cast<int>(std::size(buffer)), nullptr, SLGP_RAWPATH)) && buffer[0]) {
        location = buffer;
        index = 0;
    }
    if (location.empty()) {
        return {};
    }
    static const std::pair<const wchar_t*, const wchar_t*> kRoots[] = {
        {L"%windir%\\", L"Windows\\"},          {L"%systemroot%\\", L"Windows\\"},
        {L"%programfiles%\\", L"Program Files\\"}, {L"%programfiles(x86)%\\", L"Program Files (x86)\\"},
        {L"%programw6432%\\", L"Program Files\\"}, {L"%programdata%\\", L"ProgramData\\"},
        {L"%systemdrive%\\", L""},             {L"c:\\", L""}};
    const std::wstring lower = text::lower(location);
    for (const auto& [from, to] : kRoots) {
        if (lower.starts_with(from)) {
            const std::wstring relative = std::wstring(to) + location.substr(std::wcslen(from));
            std::error_code ec;
            if (std::filesystem::is_regular_file(mountDir / relative, ec)) {
                return {relative, index};
            }
            return {};
        }
    }
    return {};
}

void addLinks(const std::filesystem::path& mountDir, const wchar_t* root, const wchar_t* variable, std::vector<StartApp>& out) {
    const auto base = mountDir / root;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || text::lower(it->path().extension().wstring()) != L".lnk") {
            continue;
        }
        const auto rel = std::filesystem::relative(it->path(), base, ec);
        if (ec) {
            continue;
        }
        StartApp entry;
        entry.kind = StartApp::Kind::DesktopLink;
        entry.id = std::wstring(variable) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\" + rel.wstring();
        entry.name = it->path().stem().wstring();
        std::tie(entry.icon, entry.iconIndex) = linkIcon(mountDir, it->path());
        out.push_back(std::move(entry));
    }
}

} // namespace

std::wstring familyFromFullName(std::wstring_view fullName) {
    const auto parts = split(fullName, L'_');
    if (parts.size() != 5 || parts[0].empty() || parts[4].empty() || !parts[3].empty()) {
        return {};
    }
    return parts[0] + L"_" + parts[4];
}

std::wstring readableAppName(std::wstring_view identity) {
    // Microsoft's apps whose manifests only name a resource: the names Start shows (English).
    static const std::map<std::wstring, std::wstring> known{
        {L"microsoft.windowsstore", L"Microsoft Store"}, {L"microsoft.zunemusic", L"Media Player"},
        {L"microsoft.yourphone", L"Phone Link"}, {L"microsoft.windowsalarms", L"Clock"},
        {L"microsoft.windows.photos", L"Photos"}, {L"microsoft.paint", L"Paint"}, {L"microsoft.screensketch", L"Snipping Tool"},
        {L"microsoft.windowscamera", L"Camera"}, {L"microsoft.windowscalculator", L"Calculator"},
        {L"microsoft.windowsnotepad", L"Notepad"}, {L"microsoft.windowsterminal", L"Terminal"},
        {L"windows.immersivecontrolpanel", L"Settings"}, {L"microsoft.gethelp", L"Get Help"}, {L"microsoft.getstarted", L"Tips"},
        {L"microsoft.bingnews", L"News"}, {L"microsoft.bingweather", L"Weather"}, {L"microsoft.todos", L"Microsoft To Do"},
        {L"microsoft.outlookforwindows", L"Outlook"}, {L"microsoft.microsoftsolitairecollection", L"Solitaire & Casual Games"},
        {L"microsoft.gamingapp", L"Xbox"}, {L"microsoft.copilot", L"Copilot"}, {L"microsoftcorporationii.quickassist", L"Quick Assist"},
        {L"microsoft.windowssoundrecorder", L"Sound Recorder"}, {L"microsoft.microsoftstickynotes", L"Sticky Notes"},
        {L"clipchamp.clipchamp", L"Clipchamp"}, {L"microsoft.powerautomatedesktop", L"Power Automate"}, {L"msteams", L"Microsoft Teams"},
        {L"microsoft.microsoftofficehub", L"Microsoft 365 Copilot"}, {L"microsoft.windows.devhome", L"Dev Home"},
        {L"microsoft.xboxgamingoverlay", L"Game Bar"}, {L"microsoft.windowsfeedbackhub", L"Feedback Hub"},
        {L"microsoft.bingsearch", L"Bing"}, {L"microsoft.windows.secureassessmentbrowser", L"Take a Test"},
        {L"microsoftwindows.client.cbs", L"Windows"}, {L"microsoft.windows.shellexperiencehost", L"Windows Shell"},
        {L"microsoftwindows.crossdevice", L"Cross Device"}, {L"microsoft.applicationcompatibilityenhancements", L"App Compatibility"},
    };
    if (const auto it = known.find(text::lower(std::wstring(identity))); it != known.end()) {
        return it->second;
    }
    std::wstring last(identity);
    if (const auto dot = last.rfind(L'.'); dot != std::wstring::npos) {
        last = last.substr(dot + 1);
    }
    // "WindowsCalculator" → "Windows Calculator"
    std::wstring out;
    for (std::size_t i = 0; i < last.size(); ++i) {
        if (i > 0 && std::iswupper(last[i]) && std::iswlower(last[i - 1])) {
            out.push_back(L' ');
        }
        out.push_back(last[i]);
    }
    return out;
}

std::vector<StartApp> listStartApps(const std::filesystem::path& mountDir) {
    ComScope com;
    std::vector<StartApp> out;
    std::error_code ec;
    // Packaged: the newest version of every family.
    std::map<std::wstring, Manifested> families;
    const auto windowsApps = mountDir / L"Program Files\\WindowsApps";
    for (const auto& dir : std::filesystem::directory_iterator(windowsApps, std::filesystem::directory_options::skip_permission_denied, ec)) {
        const std::wstring full = dir.path().filename().wstring();
        const std::wstring family = familyFromFullName(full);
        const auto manifest = dir.path() / L"AppxManifest.xml";
        if (family.empty() || !std::filesystem::is_regular_file(manifest, ec)) {
            continue;
        }
        const auto version = versionOf(split(full, L'_')[1]);
        auto& slot = families[family];
        if (!slot.version.empty() && slot.version >= version) {
            continue;
        }
        slot.version = version;
        slot.apps = appsOf(manifest, family, mountDir);
    }
    ec.clear();
    for (const auto& dir : std::filesystem::directory_iterator(mountDir / L"Windows\\SystemApps", std::filesystem::directory_options::skip_permission_denied, ec)) {
        const std::wstring family = dir.path().filename().wstring();
        const auto manifest = dir.path() / L"AppxManifest.xml";
        if (family.find(L'_') == std::wstring::npos || families.contains(family) || !std::filesystem::is_regular_file(manifest, ec)) {
            continue;
        }
        families[family] = Manifested{{0}, appsOf(manifest, family, mountDir)};
    }
    // Settings lives in its own folder (no family suffix to read it from).
    if (const auto settings = mountDir / LR"(Windows\ImmersiveControlPanel\AppxManifest.xml)"; std::filesystem::is_regular_file(settings, ec)) {
        const std::wstring family = L"windows.immersivecontrolpanel_cw5n1h2txyewy";
        if (!families.contains(family)) {
            families[family] = Manifested{{0}, appsOf(settings, family, mountDir)};
        }
    }
    for (auto& [family, entry] : families) {
        for (auto& app : entry.apps) {
            out.push_back(std::move(app));
        }
    }
    // Edge is a desktop app the pin list names by id.
    if (std::filesystem::exists(mountDir / L"Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", ec) ||
        std::filesystem::exists(mountDir / L"Program Files\\Microsoft\\Edge\\Application\\msedge.exe", ec) ||
        std::filesystem::exists(mountDir / L"ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Microsoft Edge.lnk", ec)) {
        std::wstring edge = LR"(Program Files (x86)\Microsoft\Edge\Application\msedge.exe)";
        if (!std::filesystem::exists(mountDir / edge, ec)) {
            edge = LR"(Program Files\Microsoft\Edge\Application\msedge.exe)";
        }
        out.push_back(StartApp{StartApp::Kind::DesktopId, L"MSEdge", L"Microsoft Edge", edge});
    }
    addLinks(mountDir, kAllUsersPrograms, L"%ALLUSERSPROFILE%", out);
    addLinks(mountDir, kDefaultUserPrograms, L"%APPDATA%", out);
    // The Edge shortcut is the same app as MSEdge.
    std::erase_if(out, [](const StartApp& a) { return a.kind == StartApp::Kind::DesktopLink && text::lower(a.name) == L"microsoft edge"; });
    std::ranges::stable_sort(out, [](const StartApp& a, const StartApp& b) { return text::lower(a.name) < text::lower(b.name); });
    return out;
}

std::string startPinsJson(const std::vector<StartApp>& pins) {
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (const auto& pin : pins) {
        const char* key = pin.kind == StartApp::Kind::Packaged      ? "packagedAppId"
                          : pin.kind == StartApp::Kind::DesktopLink ? "desktopAppLink"
                                                                    : "desktopAppId";
        list.push_back({{key, utf8::fromWide(pin.id)}});
    }
    nlohmann::ordered_json doc;
    doc["pinnedList"] = std::move(list);
    return doc.dump();
}

Result<std::vector<StartApp>> startPinsFromJson(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("pinnedList") || !doc["pinnedList"].is_array()) {
        return fail(ErrorCode::ParseError, L"not a Start pin list", utf8::toWide(std::string(json.substr(0, 80))));
    }
    std::vector<StartApp> out;
    for (const auto& item : doc["pinnedList"]) {
        if (!item.is_object() || item.size() != 1) {
            continue;
        }
        const auto entry = item.items().begin();
        const std::string key = entry.key();
        const auto& value = entry.value();
        if (!value.is_string()) {
            continue;
        }
        StartApp app;
        app.id = utf8::toWide(value.get<std::string>());
        app.name = app.id;
        if (key == "packagedAppId") {
            app.kind = StartApp::Kind::Packaged;
        } else if (key == "desktopAppLink") {
            app.kind = StartApp::Kind::DesktopLink;
            app.name = std::filesystem::path(app.id).stem().wstring();
        } else if (key == "desktopAppId") {
            app.kind = StartApp::Kind::DesktopId;
        } else {
            continue;
        }
        out.push_back(std::move(app));
    }
    return out;
}

namespace {

constexpr wchar_t kPolicyManagerKey[] = LR"(HKLM\SOFTWARE\Microsoft\PolicyManager\current\device\Start)";
constexpr wchar_t kExplorerPolicyKey[] = LR"(HKLM\SOFTWARE\Policies\Microsoft\Windows\Explorer)";
constexpr wchar_t kJsonPathValue[] = LR"(%ProgramData%\WinLove\StartPins.json)";
constexpr wchar_t kLayoutXml[] = LR"(Users\Default\AppData\Local\Microsoft\Windows\Shell\LayoutModification.xml)";
// Windows 10: a Start layout without tiles (the same file the old "Pinned apps and tiles" toggle wrote).
constexpr char kEmptyTiles[] =
    "<LayoutModificationTemplate xmlns=\"http://schemas.microsoft.com/Start/2014/LayoutModification\" "
    "xmlns:defaultlayout=\"http://schemas.microsoft.com/Start/2014/FullDefaultLayout\" "
    "xmlns:start=\"http://schemas.microsoft.com/Start/2014/StartLayout\" Version=\"1\">\r\n"
    "  <LayoutOptions StartTileGroupCellWidth=\"6\" />\r\n"
    "  <DefaultLayoutOverride>\r\n"
    "    <StartLayoutCollection>\r\n"
    "      <defaultlayout:StartLayout GroupCellWidth=\"6\" />\r\n"
    "    </StartLayoutCollection>\r\n"
    "  </DefaultLayoutOverride>\r\n"
    "</LayoutModificationTemplate>\r\n";

constexpr wchar_t kStartState[] =
    LR"(Users\Default\AppData\Local\Packages\Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy\LocalState\start2.bin)";
// A Start state with no pins (972 bytes), from Win11Debloat (MIT, (c) Raphire — third_party/win11debloat):
// copied into the default profile, every new account starts with it. Measured (D-069): Home and Pro
// 25H2 open Start with an empty Pinned section — the only way Home takes, its edition ignores the policy.
constexpr char kEmptyStartState[] =
    "4nrhSwH8TRucAIEL3m5RhU5aX0cAW7FJilySr5CE+V6aoBj7A+HZAaADAABc9u55LN8F4borYyXEGl8Q5+RZ+qERszeqUhhZXDvc"
    "jTF6rgdprauITLqPgMVMbSZbRsLN/O5uMjSLEr6nWYIwsMJkZMnZyZrhR3PugUhUKOYDqwySCY6/CPkL/Ooz/5j2R2hwWRGqc7Zs"
    "JxDFM1DWofjUiGjDUny+Y8UjowknQVaPYao0PC4bygKEbeZqCqRvSgPalSc53OFqCh2FHydzl09fChaos385QvF40EDEgSO8U9/d"
    "ntAeNULwuuZBi7BkWSIOmWN1l4e+TZbtSJXwn+EINAJhRHyCSNeku21dsw+cMoLorMKnRmhJMLvE+CCdgNKIaPo/Krizva1+bMsI"
    "8bSkV/CxaCTLXodb/NuBYCsIHY1sTvbwSBRNMPvccw43RJCUKZRkBLkCVfW24ANbLfHXofHDMLxxFNUpBPSgzGHnueHknECcf6J4"
    "HCFBqzvSH1TjQ3S6J8tq2yaQ+jFNkxGRMushdXNNiTNjDFYMJNvgRL2lu63NPE+Cxy+IKC1NdKLweFdOGZr2mvKAw7t/fxmCTieU"
    "gLkegDomZbHL6anjy4SkjSCnfTBUNtxc0X3VJiha4wq/ArRrTtVnzcUcX+CI4BNTicx+X2eXugI+EHKjgaQS7fXHqQGEUMUeHMCX"
    "lgWUZ5kE3LFTjVifyVIGqYNDuqt7T9l7DWByiuRariySa7tiN1gA2ALKYlRsjsQL7xpxHnT1hi/9b+UuyC46cYQaDUcKDc4BGReJ"
    "P2gDIyZfudLpgUPc7YfH9doiMcWimSylbKFtsI3Mfo0HONxet5XjzjDoziduYk2dFoFfz19uaRcOHtASKzaGdtk6RC+Tm4BbU/7P"
    "lbvHEKJZ720AxOQkzU9U8RWAHHsPUVfWzYoQc2dN8OQ/JlUAqe8+PI05ST4m3LoUpBKB+oU0H84aet5etGpIi4CthvazGencFObW"
    "JWNRzxk9BXIX2YoAdXB8b7JFwlxVdhgzZK0zkkrzSSmX9iJcNoi6Tp+RtnljzLTAv6xh8gwytIW5F2e5sVh7aiqo4sji0aE+Toqy"
    "NPV7eE9Idi2ZNeEbnJ9LX127uOl5jB280hs0caXLUrYiR15+Y31wtlD8JVeTDxDDac6v+e3C4VX+28mg9bYQ7NGYXZc7yZANC/nW"
    "Tn+/hkTZUvR0gi+PUz4o/DSdKzbvVCAlqdjArcKkWW4r/WKUSLskoOKRPxdNLPVBl2S6blje4LvBzulpeHWubXWfCW4ILuOI";

std::wstring regString(std::wstring_view text) {
    std::wstring out = L"\"";
    for (const wchar_t c : text) {
        if (c == L'\\' || c == L'"') {
            out.push_back(L'\\');
        }
        out.push_back(c);
    }
    return out + L"\"";
}

std::wstring regExpand(std::wstring_view text) {
    std::wstring out = L"hex(2):";
    std::wstring data(text);
    data.push_back(L'\0');
    bool first = true;
    for (const wchar_t c : data) {
        for (const unsigned value : {static_cast<unsigned>(c) & 0xFFu, (static_cast<unsigned>(c) >> 8) & 0xFFu}) {
            out += std::format(L"{}{:02x}", first ? L"" : L",", value);
            first = false;
        }
    }
    return out;
}

} // namespace

std::string startPinsJson(const StartPinsPlan& plan) {
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    if (plan.custom) {
        for (const auto& pin : plan.pins) {
            const char* key = pin.kind == StartApp::Kind::Packaged      ? "packagedAppId"
                              : pin.kind == StartApp::Kind::DesktopLink ? "desktopAppLink"
                                                                        : "desktopAppId";
            list.push_back({{key, utf8::fromWide(pin.id)}});
        }
    }
    nlohmann::ordered_json doc;
    if (plan.applyOnce) {
        doc["applyOnce"] = true;
    }
    doc["pinnedList"] = std::move(list);
    return doc.dump();
}

std::vector<ops::Operation> startPinsOperations(const StartPinsPlan& plan) {
    using ops::OpKind;
    using ops::Operation;
    const std::string json = startPinsJson(plan);
    const std::wstring wide = utf8::toWide(json);
    std::vector<Operation> out;
    // The MDM policy (every 24H2 / 25H2 build reads it) and the Group Policy form newer builds want.
    out.push_back(Operation{OpKind::SetRegistryValue, std::wstring(kPolicyManagerKey) + L"::ConfigureStartPins", regString(wide)});
    out.push_back(Operation{OpKind::SetRegistryValue, std::wstring(kExplorerPolicyKey) + L"::ConfigureStartPins", L"dword:00000001"});
    out.push_back(Operation{OpKind::SetRegistryValue, std::wstring(kExplorerPolicyKey) + L"::ConfigureStartPinsJSON", regExpand(kJsonPathValue)});
    out.push_back(Operation{OpKind::WriteFile, kStartPinsFile, wide});
    // Every edition starts from an empty Start state (no Microsoft pins, no promoted placeholders);
    // the policy then puts the list on top where the edition / build reads it (D-069, measured).
    out.push_back(Operation{OpKind::WriteFile, kStartState, L"base64:" + utf8::toWide(kEmptyStartState)});
    if (!plan.custom) {
        out.push_back(Operation{OpKind::WriteFile, kLayoutXml, utf8::toWide(kEmptyTiles)});
    }
    return out;
}

std::optional<StartPinsPlan> startPinsPlanFromOperations(const std::vector<ops::Operation>& ops) {
    for (const auto& op : ops) {
        if (op.kind != ops::OpKind::WriteFile || text::lower(op.target) != text::lower(std::wstring(kStartPinsFile))) {
            continue;
        }
        const auto doc = nlohmann::json::parse(utf8::fromWide(op.value), nullptr, false);
        auto pins = startPinsFromJson(utf8::fromWide(op.value));
        if (doc.is_discarded() || !pins) {
            return std::nullopt;
        }
        StartPinsPlan plan;
        plan.applyOnce = doc.value("applyOnce", false);
        plan.pins = std::move(*pins);
        plan.custom = !plan.pins.empty();
        return plan;
    }
    return std::nullopt;
}

std::vector<std::pair<ops::OpKind, std::wstring>> startPinsSlots() {
    using ops::OpKind;
    return {{OpKind::SetRegistryValue, std::wstring(kPolicyManagerKey) + L"::ConfigureStartPins"},
            {OpKind::SetRegistryValue, std::wstring(kExplorerPolicyKey) + L"::ConfigureStartPins"},
            {OpKind::SetRegistryValue, std::wstring(kExplorerPolicyKey) + L"::ConfigureStartPinsJSON"},
            {OpKind::WriteFile, kStartPinsFile},
            {OpKind::WriteFile, kLayoutXml},
            {OpKind::WriteFile, kStartState}};
}

} // namespace wl::core
