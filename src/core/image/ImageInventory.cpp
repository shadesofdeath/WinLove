#include "core/image/ImageInventory.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/DeepRemoval.h"
#include "core/image/DriverInf.h"
#include "core/image/OffReg.h"
#include "core/system/Files.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <set>

namespace wl::core {

namespace {

namespace fs = std::filesystem;

std::wstring regString(const std::vector<std::uint8_t>& data) {
    std::wstring s(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    while (!s.empty() && s.back() == L'\0') {
        s.pop_back();
    }
    return s;
}

std::wstring valueString(const OffRegKey& key, std::wstring_view name) {
    for (const auto& value : key.values()) {
        if (_wcsicmp(value.name.c_str(), std::wstring(name).c_str()) == 0 &&
            (value.type == REG_SZ || value.type == REG_EXPAND_SZ)) {
            return regString(value.data);
        }
    }
    return {};
}

// "@%SystemRoot%\system32\input.dll,-5000" → the text, from this PC's files (its UI language);
// a plain string as it is; empty when it cannot be resolved.
std::wstring indirect(const std::wstring& text) {
    if (text.empty() || text.front() != L'@') {
        return text;
    }
    // "@c_display.inf,%ClassDesc%;Display adapters": an INF-indirect string with its fallback after
    // the ';' — what the reference tool shows (README §6.6: the English class names).
    if (text.find(L".inf,") != std::wstring::npos || text.find(L".INF,") != std::wstring::npos) {
        const auto semicolon = text.find(L';');
        return semicolon == std::wstring::npos ? std::wstring() : text.substr(semicolon + 1);
    }
    wchar_t buffer[512] = {};
    if (SUCCEEDED(SHLoadIndirectString(text.c_str(), buffer, static_cast<UINT>(std::size(buffer)), nullptr))) {
        return buffer;
    }
    if (const auto semicolon = text.find(L';'); semicolon != std::wstring::npos) {
        return text.substr(semicolon + 1);
    }
    return {};
}

std::wstring controlSet(const OffRegKey& system) {
    const auto select = system.open(L"Select");
    const DWORD current = select ? select.dword(L"Current").value_or(1) : 1;
    wchar_t name[32];
    swprintf_s(name, L"ControlSet%03lu", current);
    return name;
}

std::wstring upper(std::wstring s) {
    std::ranges::transform(s, s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return s;
}

bool present(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

// ---- drivers -----------------------------------------------------------------------------------

// A class INF's own description of its class: the default value its ClassInstall32 AddReg writes
// ("HKR,,,,%ClassDesc%"), resolved from [Strings] — the reference tool's English class names ("Display adapters").
std::wstring classInfDescription(const fs::path& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return {};
    }
    const std::wstring text = utf8::decodeText(*bytes);
    std::map<std::wstring, std::wstring> strings;
    std::wstring section;
    std::wstring found;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line(text::trim(std::wstring_view(text).substr(start, end - start)));
        start = end + 1;
        if (const auto semicolon = line.find(L';'); semicolon != std::wstring::npos && line.find(L'"') == std::wstring::npos) {
            line = std::wstring(text::trim(std::wstring_view(line).substr(0, semicolon)));
        }
        if (line.empty()) {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            section = text::lower(line.substr(1, line.size() - 2));
            continue;
        }
        if (section == L"strings") {
            if (const auto eq = line.find(L'='); eq != std::wstring::npos) {
                std::wstring value(text::trim(std::wstring_view(line).substr(eq + 1)));
                if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
                    value = value.substr(1, value.size() - 2);
                }
                strings[text::lower(std::wstring(text::trim(std::wstring_view(line).substr(0, eq))))] = value;
            }
            continue;
        }
        if (!found.empty() || section.find(L"class") == std::wstring::npos) {
            continue; // the class's own AddReg sections only ([ClassInstall32.AddReg], [DisplayClass.AddReg] …)
        }
        // HKR,,,,<value> / HKR,,,0,<value>
        std::vector<std::wstring> fields;
        std::wstring field;
        for (const wchar_t c : line) {
            if (c == L',') {
                fields.push_back(std::wstring(text::trim(field)));
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(std::wstring(text::trim(field)));
        if (fields.size() >= 5 && _wcsicmp(fields[0].c_str(), L"HKR") == 0 && fields[1].empty() && fields[2].empty() &&
            (fields[3].empty() || fields[3] == L"0")) {
            found = fields[4];
        }
    }
    if (found.size() > 2 && found.front() == L'%' && found.back() == L'%') {
        const auto it = strings.find(text::lower(found.substr(1, found.size() - 2)));
        found = it != strings.end() ? it->second : std::wstring();
    }
    if (found.size() >= 2 && found.front() == L'"' && found.back() == L'"') {
        found = found.substr(1, found.size() - 2);
    }
    return found.starts_with(L"@") ? std::wstring() : found;
}

std::wstring archOfPackage(std::wstring_view package) {
    const std::wstring lowered = text::lower(std::wstring(package));
    for (const wchar_t* arch : {L"amd64", L"arm64", L"x86"}) {
        if (lowered.find(std::wstring(L"_") + arch + L"_") != std::wstring::npos) {
            return arch;
        }
    }
    return L"amd64";
}

void readDrivers(const fs::path& mountDir, ImageInventory& out) {
    const auto config = mountDir / L"Windows" / L"System32" / L"config";
    std::map<std::wstring, std::wstring> classNames; // GUID → the image's description
    if (auto system = OffRegKey::openHive(config / L"SYSTEM")) {
        const auto classes = system->open(controlSet(*system) + L"\\Control\\Class");
        for (const auto& guid : classes.subkeys()) {
            const auto key = classes.open(guid);
            std::wstring name = indirect(valueString(key, L"ClassDesc"));
            if (name.empty()) {
                name = indirect(valueString(key, L""));
            }
            if (name.empty()) {
                name = valueString(key, L"Class");
            }
            classNames[upper(guid)] = name;
        }
    }
    std::set<std::wstring> seen;
    std::map<std::wstring, std::wstring> classInfNames; // GUID → the class INF's description
    const auto repository = mountDir / L"Windows" / L"System32" / L"DriverStore" / L"FileRepository";
    // Two driver databases: DRIVERS holds most packages, SYSTEM the boot-capable ones.
    for (const wchar_t* file : {L"DRIVERS", L"SYSTEM"}) {
        auto hive = OffRegKey::openHive(config / file);
        if (!hive) {
            log::warn("inventory", std::wstring(file) + L": " + hive.error().message);
            continue;
        }
        const auto packages = hive->open(L"DriverDatabase\\DriverPackages");
        for (const auto& package : packages.subkeys()) {
            if (!seen.insert(text::lower(package)).second) {
                continue;
            }
            InventoryDriver driver;
            driver.package = package;
            for (const auto& value : packages.open(package).values()) {
                if (value.name == L"Version") {
                    driver.classGuid = driverPackageClass(value.data);
                } else if (value.name.empty() && value.type == REG_SZ) {
                    driver.inf = regString(value.data);
                }
            }
            if (driver.inf.empty()) {
                continue;
            }
            driver.classInf = text::lower(driver.inf).starts_with(L"c_");
            const auto folder = repository / package;
            const DriverInf inf = parseInf(folder / driver.inf);
            driver.provider = inf.provider;
            driver.version = inf.version;
            driver.date = inf.date;
            driver.devices = inf.devicesFor(archOfPackage(package));
            if (driver.classInf) {
                if (std::wstring desc = classInfDescription(folder / driver.inf); !desc.empty()) {
                    classInfNames[normalizeClassGuid(inf.classGuid.empty() ? driver.classGuid : inf.classGuid)] = std::move(desc);
                }
            }
            if (driver.classGuid.empty()) {
                driver.classGuid = normalizeClassGuid(inf.classGuid);
            }
            const auto named = classNames.find(driver.classGuid);
            driver.className = named != classNames.end() && !named->second.empty() ? named->second : std::wstring();
            if (driver.className.empty()) {
                driver.className = inf.className; // until the class INFs are read (below)
            }
            driver.size = treeBytes(folder);
            out.drivers.push_back(std::move(driver));
        }
    }
    // Every class definition in Windows\INF ([ClassInstall32]: display.inf, disk.inf, c_*.inf …).
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mountDir / L"Windows" / L"INF", fs::directory_options::skip_permission_denied, ec)) {
        if (text::lower(entry.path().extension().wstring()) != L".inf") {
            continue;
        }
        const auto bytes = readFileBytes(entry.path());
        if (!bytes) {
            continue;
        }
        const std::wstring content = utf8::decodeText(*bytes);
        if (text::lower(content).find(L"[classinstall32") == std::wstring::npos) {
            continue;
        }
        const DriverInf inf = parseInfText(content, entry.path());
        const std::wstring guid = normalizeClassGuid(inf.classGuid);
        if (guid.empty() || classInfNames.contains(guid)) {
            continue;
        }
        if (std::wstring desc = classInfDescription(entry.path()); !desc.empty()) {
            classInfNames[guid] = std::move(desc);
        }
    }
    // Classes this image has no Control\Class key for yet: the class INF names them.
    for (auto& driver : out.drivers) {
        const auto named = classNames.find(driver.classGuid);
        if (named != classNames.end() && !named->second.empty()) {
            continue;
        }
        if (const auto own = classInfNames.find(driver.classGuid); own != classInfNames.end()) {
            driver.className = own->second;
        }
    }
    std::ranges::sort(out.drivers, [](const InventoryDriver& a, const InventoryDriver& b) {
        const int c = _wcsicmp(a.className.c_str(), b.className.c_str());
        return c != 0 ? c < 0 : _wcsicmp(a.inf.c_str(), b.inf.c_str()) < 0;
    });
}

// ---- languages ---------------------------------------------------------------------------------

// The reference tool's grouping (measured, README §6.6): one row per language — sr-Cyrl-BA, sr-Cyrl-RS and
// sr-Latn-RS are "Sırpça", es-ES and es-MX "İspanyolca", nb-NO and nn-NO "Norveççe", qps-ploc and
// qps-plocm one pseudo locale — except English, French, Portuguese and Chinese, whose variants
// are rows of their own.
std::wstring languageGroup(const std::wstring& tag) {
    const std::wstring lowered = text::lower(tag);
    const std::wstring primary = lowered.substr(0, lowered.find(L'-'));
    if (primary == L"en" || primary == L"fr" || primary == L"pt" || primary == L"zh") {
        return lowered;
    }
    return primary == L"nn" || primary == L"nb" ? L"no" : primary;
}

std::wstring localeText(const std::wstring& tag, LCTYPE type) {
    wchar_t text[LOCALE_NAME_MAX_LENGTH * 4] = {};
    return GetLocaleInfoEx(tag.c_str(), type, text, static_cast<int>(std::size(text))) > 0 ? std::wstring(text) : std::wstring();
}

void readLanguages(const fs::path& mountDir, ImageInventory& out) {
    const auto system32 = mountDir / L"Windows" / L"System32";
    std::error_code ec;
    std::map<std::wstring, std::vector<std::wstring>> groups; // group → tags
    for (const auto& entry : fs::directory_iterator(system32, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec) && isLocaleTag(entry.path().filename().wstring())) {
            const std::wstring tag = entry.path().filename().wstring();
            groups[languageGroup(tag)].push_back(tag);
        }
    }
    for (const auto& [group, tags] : groups) {
        InventoryLanguage language;
        language.tag = tags.front();
        for (std::size_t i = 1; i < tags.size(); ++i) {
            language.tag += L"," + tags[i];
        }
        // One tag: its full name when the variants are rows of their own, else the language's name.
        const bool variant = group.find(L'-') != std::wstring::npos;
        language.name = localeText(tags.front(), variant ? LOCALE_SLOCALIZEDDISPLAYNAME : LOCALE_SLOCALIZEDLANGUAGENAME);
        if (language.name.empty()) {
            language.name = tags.front();
        }
        for (const auto& tag : tags) {
            for (const std::wstring& relative : {L"Windows\\System32\\" + tag, L"Windows\\SysWOW64\\" + tag,
                                                 L"Program Files\\Common Files\\microsoft shared\\ink\\" + tag}) {
                const auto path = mountDir / relative;
                if (present(path)) {
                    language.paths.push_back(relative);
                    language.size += treeBytes(path);
                }
            }
        }
        out.languages.push_back(std::move(language));
    }
    std::ranges::sort(out.languages, [](const InventoryLanguage& a, const InventoryLanguage& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

// ---- keyboards ---------------------------------------------------------------------------------

void readKeyboards(const fs::path& mountDir, ImageInventory& out) {
    auto system = OffRegKey::openHive(mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM");
    if (!system) {
        log::warn("inventory", L"SYSTEM: " + system.error().message);
        return;
    }
    const auto layouts = system->open(controlSet(*system) + L"\\Control\\Keyboard Layouts");
    std::map<std::wstring, int> fileUse;
    for (const auto& klid : layouts.subkeys()) {
        const auto key = layouts.open(klid);
        InventoryKeyboard keyboard;
        keyboard.klid = upper(klid);
        keyboard.file = valueString(key, L"Layout File");
        keyboard.name = indirect(valueString(key, L"Layout Display Name"));
        if (keyboard.name.empty()) {
            keyboard.name = valueString(key, L"Layout Text");
        }
        if (keyboard.name.empty()) {
            keyboard.name = keyboard.klid;
        }
        ++fileUse[text::lower(keyboard.file)];
        out.keyboards.push_back(std::move(keyboard));
    }
    for (auto& keyboard : out.keyboards) {
        keyboard.sharedFile = fileUse[text::lower(keyboard.file)] > 1;
    }
    std::ranges::sort(out.keyboards, [](const InventoryKeyboard& a, const InventoryKeyboard& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

// ---- fonts -------------------------------------------------------------------------------------

void readFonts(const fs::path& mountDir, ImageInventory& out) {
    auto software = OffRegKey::openHive(mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE");
    if (!software) {
        log::warn("inventory", L"SOFTWARE: " + software.error().message);
        return;
    }
    const auto fontsKey = software->open(L"Microsoft\\Windows NT\\CurrentVersion\\Fonts");
    std::map<std::wstring, InventoryFont> families; // folded family → font
    std::map<std::wstring, std::set<std::wstring>> fileFamilies; // file → families using it
    for (const auto& value : fontsKey.values()) {
        if (value.type != REG_SZ && value.type != REG_EXPAND_SZ) {
            continue;
        }
        const std::wstring family = fontFamily(value.name);
        if (family.empty()) {
            continue;
        }
        std::wstring file = regString(value.data);
        out.fontValues.emplace_back(value.name, file);
        // A full path points outside Windows\Fonts (a per-app font): not the system's.
        if (file.find(L'\\') != std::wstring::npos || file.find(L':') != std::wstring::npos) {
            continue;
        }
        auto& font = families[text::lower(family)];
        if (font.family.empty()) {
            font.family = family;
        }
        font.values.push_back(value.name);
        if (std::ranges::find(font.files, file) == font.files.end()) {
            font.files.push_back(file);
        }
        fileFamilies[text::lower(file)].insert(text::lower(family));
    }
    const auto fonts = mountDir / L"Windows" / L"Fonts";
    for (auto& [key, font] : families) {
        std::erase_if(font.files, [&](const std::wstring& file) { return fileFamilies[text::lower(file)].size() > 1; });
        for (const auto& file : font.files) {
            font.size += treeBytes(fonts / file);
        }
        out.fonts.push_back(std::move(font));
    }
    std::ranges::sort(out.fonts, [](const InventoryFont& a, const InventoryFont& b) {
        return _wcsicmp(a.family.c_str(), b.family.c_str()) < 0;
    });
}

// ---- system apps -------------------------------------------------------------------------------

std::wstring manifestDisplayName(const fs::path& manifest) {
    const auto bytes = readFileBytes(manifest);
    if (!bytes) {
        return {};
    }
    const std::wstring text = utf8::decodeText(*bytes);
    const auto properties = text.find(L"<Properties>");
    const auto open = text.find(L"<DisplayName>", properties == std::wstring::npos ? 0 : properties);
    if (open == std::wstring::npos) {
        return {};
    }
    const auto start = open + std::wstring_view(L"<DisplayName>").size();
    const auto close = text.find(L"</DisplayName>", start);
    if (close == std::wstring::npos) {
        return {};
    }
    std::wstring name(text::trim(std::wstring_view(text).substr(start, close - start)));
    return name.starts_with(L"ms-resource:") ? std::wstring() : name;
}

void readSystemApps(const fs::path& mountDir, ImageInventory& out) {
    auto software = OffRegKey::openHive(mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE");
    if (!software) {
        return;
    }
    const auto inbox = software->open(L"Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore\\InboxApplications");
    for (const auto& fullName : inbox.subkeys()) {
        InventorySystemApp app;
        app.fullName = fullName;
        // Name_Version_Arch_ResourceId_PublisherId
        std::vector<std::wstring> parts;
        std::wstring part;
        for (const wchar_t c : fullName) {
            if (c == L'_') {
                parts.push_back(part);
                part.clear();
            } else {
                part += c;
            }
        }
        parts.push_back(part);
        if (parts.size() < 5) {
            continue;
        }
        app.name = parts.front();
        app.family = parts.front() + L"_" + parts.back();
        const std::wstring path = valueString(inbox.open(fullName), L"Path");
        // The reference tool lists every inbox app: in SystemApps, and the ones in WindowsApps (codecs …).
        const std::wstring lowered = text::lower(path);
        std::wstring_view root;
        std::wstring base;
        if (const auto at = lowered.find(L"\\systemapps\\"); at != std::wstring::npos) {
            root = std::wstring_view(path).substr(at + std::wstring_view(L"\\systemapps\\").size());
            base = L"Windows\\SystemApps\\";
        } else if (const auto at2 = lowered.find(L"\\windowsapps\\"); at2 != std::wstring::npos) {
            root = std::wstring_view(path).substr(at2 + std::wstring_view(L"\\windowsapps\\").size());
            base = L"Program Files\\WindowsApps\\";
        } else {
            continue;
        }
        const std::wstring folderName(root.substr(0, root.find(L'\\')));
        app.folder = base + folderName;
        const auto folder = mountDir / app.folder;
        if (!present(folder)) {
            continue;
        }
        app.displayName = manifestDisplayName(folder / L"AppxManifest.xml");
        app.size = treeBytes(folder);
        out.systemApps.push_back(std::move(app));
    }
    std::ranges::sort(out.systemApps, [](const InventorySystemApp& a, const InventorySystemApp& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

RegistryWrite writeOf(RegistryWrite::Kind kind, std::wstring key, std::wstring name = {}) {
    RegistryWrite write;
    write.kind = kind;
    write.key = std::move(key);
    write.name = std::move(name);
    return write;
}

} // namespace

bool isLocaleTag(std::wstring_view name) {
    if (name.size() < 4 || name.size() > 15 || name.find(L'-') == std::wstring_view::npos) {
        return false;
    }
    if (!std::ranges::all_of(name, [](wchar_t c) { return c < 128 && (std::iswalpha(c) != 0 || c == L'-'); })) {
        return false;
    }
    const std::wstring tag(name);
    return LocaleNameToLCID(tag.c_str(), 0) != 0 || IsValidLocaleName(tag.c_str()) != FALSE;
}

std::wstring fontFamily(std::wstring_view valueName) {
    std::wstring name(text::trim(valueName));
    // "(TrueType)", "(OpenType)", "(VGA res)": what Windows adds after the name.
    if (const auto paren = name.find(L" ("); paren != std::wstring::npos) {
        name.resize(paren);
    }
    if (const auto amp = name.find(L" & "); amp != std::wstring::npos) {
        name.resize(amp);
    }
    // Bitmap fonts: "Courier 10,12,15".
    if (const auto comma = name.find(L','); comma != std::wstring::npos) {
        name.resize(comma);
        while (!name.empty() && (std::iswdigit(name.back()) != 0 || name.back() == L' ')) {
            name.pop_back();
        }
    }
    static constexpr std::array<std::wstring_view, 22> kStyles = {
        L"Bold",      L"Italic",   L"Oblique",  L"Light",    L"Semilight", L"SemiLight", L"Semibold", L"SemiBold",
        L"Black",     L"Regular",  L"Condensed", L"Narrow",  L"Demibold",  L"Heavy",     L"Medium",   L"Thin",
        L"ExtraLight", L"Extralight", L"ExtraBold", L"Extrabold", L"Ultralight", L"Book"};
    bool trimmed = true;
    while (trimmed) {
        trimmed = false;
        for (const auto style : kStyles) {
            const std::wstring suffix = L" " + std::wstring(style);
            if (name.size() > suffix.size() && name.ends_with(suffix)) {
                name.resize(name.size() - suffix.size());
                trimmed = true;
            }
        }
    }
    return std::wstring(text::trim(name));
}

Result<ImageInventory> readImageInventory(const std::filesystem::path& mountDir) {
    if (!present(mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE")) {
        return fail(ErrorCode::NotFound, L"no Windows image in this folder", mountDir.wstring());
    }
    ImageInventory inventory;
    readDrivers(mountDir, inventory);
    readLanguages(mountDir, inventory);
    readKeyboards(mountDir, inventory);
    readFonts(mountDir, inventory);
    readSystemApps(mountDir, inventory);
    return inventory;
}

ComponentRecipe driverRecipe(const InventoryDriver& driver) {
    ComponentRecipe recipe;
    recipe.title = (driver.provider.empty() ? std::wstring() : driver.provider + L"  |  ") + driver.inf;
    recipe.driverPackages = {driver.package};
    recipe.afterUpdates = true; // a later cumulative update may need it (D-063)
    return recipe;
}

ComponentRecipe languageRecipe(const InventoryLanguage& language) {
    ComponentRecipe recipe;
    recipe.title = language.name;
    recipe.paths = language.paths;
    return recipe;
}

ComponentRecipe keyboardRecipe(const InventoryKeyboard& keyboard) {
    ComponentRecipe recipe;
    recipe.title = keyboard.name;
    if (!keyboard.sharedFile && !keyboard.file.empty() && keyboard.file.find(L'\\') == std::wstring::npos) {
        recipe.paths = {L"Windows\\System32\\" + keyboard.file, L"Windows\\SysWOW64\\" + keyboard.file};
    }
    recipe.registry = {writeOf(RegistryWrite::Kind::DeleteKey,
                               L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\" + keyboard.klid)};
    return recipe;
}

bool isCoreFontFamily(std::wstring_view family) {
    // VM test 2026-11-08: these 34 files (with Segoe UI, kept already) are enough for Setup's screens;
    // every other family of the preset stayed removed.
    static constexpr std::array<std::wstring_view, 24> kCore = {
        L"Segoe UI",        L"Segoe UI Variable", L"Segoe UI Symbol",   L"Segoe UI Emoji", L"Segoe Fluent Icons",
        L"Segoe MDL2 Assets", L"Marlett",         L"Arial",             L"Times New Roman", L"Courier New",
        L"Courier",         L"Tahoma",            L"Microsoft Sans Serif", L"MS Sans Serif", L"MS Serif",
        L"Small Fonts",     L"Consolas",          L"Lucida Console",    L"Symbol",          L"Wingdings",
        L"Webdings",        L"Modern",            L"Roman",             L"Script"};
    const std::wstring name(text::trim(family));
    return std::ranges::any_of(kCore, [&](std::wstring_view core) { return _wcsicmp(name.c_str(), std::wstring(core).c_str()) == 0; });
}

ComponentRecipe fontRecipe(const InventoryFont& font) {
    ComponentRecipe recipe;
    recipe.title = font.family;
    for (const auto& file : font.files) {
        recipe.paths.push_back(L"Windows\\Fonts\\" + file);
    }
    for (const auto& value : font.values) {
        recipe.registry.push_back(
            writeOf(RegistryWrite::Kind::DeleteValue, L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", value));
    }
    return recipe;
}

ComponentRecipe systemAppRecipe(const InventorySystemApp& app) {
    // The reference tool: the SystemApps folder goes, the family is marked deprovisioned (so no new user gets
    // it back), its inbox registration goes.
    ComponentRecipe recipe;
    recipe.title = app.displayName.empty() ? app.name : app.displayName;
    recipe.paths = {app.folder};
    const std::wstring store = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore\\";
    recipe.registry = {writeOf(RegistryWrite::Kind::CreateKey, store + L"Deprovisioned\\" + app.family),
                       writeOf(RegistryWrite::Kind::DeleteKey, store + L"InboxApplications\\" + app.fullName)};
    return recipe;
}

} // namespace wl::core
