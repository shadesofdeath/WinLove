#include "core/image/ContextMenu.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/OffReg.h"
#include "core/image/Services.h"
#include "core/system/BackupFiles.h"
#include "core/system/Privileges.h"

#include <json.hpp>
#include <pugixml.hpp>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <map>

namespace wl::core {

namespace {

constexpr wchar_t kBlocked[] = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked";
constexpr wchar_t kBlockedInHive[] = L"Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked";
constexpr wchar_t kItemPrefix[] = L"WinLove.";

std::wstring textOf(const std::vector<std::uint8_t>& bytes) {
    std::wstring s(bytes.size() / 2, L'\0');
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<wchar_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    }
    while (!s.empty() && s.back() == L'\0') {
        s.pop_back();
    }
    return s;
}

std::wstring stringOf(const OffRegKey& key, const wchar_t* name) {
    return key ? textOf(key.bytes(name)) : std::wstring();
}

bool hasValue(const OffRegKey& key, std::wstring_view name) {
    if (!key) {
        return false;
    }
    const auto values = key.values(/*withData=*/false);
    return std::ranges::any_of(values, [&](const OffRegKey::Value& v) { return text::iequals(v.name, name); });
}

std::wstring upperClsid(std::wstring s) {
    std::ranges::transform(s, s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return s;
}

bool isClsid(std::wstring_view s) {
    return s.size() == 38 && s.front() == L'{' && s.back() == L'}';
}

// "&Open with Code" → "Open with Code" (the access-key marker is not part of the name).
std::wstring withoutAmpersand(std::wstring s) {
    std::wstring out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&' && i + 1 < s.size() && s[i + 1] != L'&') {
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string_view localName(const char* name) {
    const std::string_view n(name);
    const auto colon = n.find(':');
    return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

// Windows 11's packaged menu items: <desktop4|5:FileExplorerContextMenus> anywhere in the manifest.
void readPackaged(const std::filesystem::path& mountDir, std::map<std::wstring, ContextMenuEntry>& byClsid,
                  const std::vector<std::wstring>& blocked) {
    // WindowsApps is closed to administrators: listed and read with the backup privilege (as Appx.cpp).
    (void)enablePrivilege(SE_BACKUP_NAME);
    const auto apps = mountDir / L"Program Files" / L"WindowsApps";
    for (const auto& folder : backupListFolders(apps)) {
        if (folder.find(L"_~_") != std::wstring::npos) {
            continue; // a bundle's folder: its packages carry the manifests
        }
        auto bytes = backupReadFile(apps / folder / L"AppxManifest.xml", 4u << 20);
        if (!bytes || bytes->find("FileExplorerContextMenus") == std::string::npos) {
            continue;
        }
        pugi::xml_document doc;
        if (!doc.load_buffer(bytes->data(), bytes->size())) {
            continue;
        }
        std::wstring identity;
        for (const auto& c : doc.document_element().children()) {
            if (localName(c.name()) == "Identity") {
                identity = utf8::toWide(c.attribute("Name").as_string());
            }
        }
        // Depth-first over the whole manifest: the menus sit under Applications › Extensions.
        std::vector<pugi::xml_node> stack{doc.document_element()};
        while (!stack.empty()) {
            const pugi::xml_node node = stack.back();
            stack.pop_back();
            // Children pushed last-first: they come off the stack in document order (the places' order).
            for (auto c = node.last_child(); c; c = c.previous_sibling()) {
                if (c.type() == pugi::node_element) {
                    stack.push_back(c);
                }
            }
            if (localName(node.name()) != "ItemType" || localName(node.parent().name()) != "FileExplorerContextMenus") {
                continue;
            }
            const std::wstring place = utf8::toWide(node.attribute("Type").as_string());
            for (const auto& verb : node.children()) {
                if (localName(verb.name()) != "Verb") {
                    continue;
                }
                // Manifests write the CLSID without braces ("9f156763-7844-…"); the registry's Blocked key with them.
                std::wstring clsid = upperClsid(utf8::toWide(verb.attribute("Clsid").as_string()));
                if (clsid.size() == 36 && clsid.front() != L'{') {
                    clsid = L"{" + clsid + L"}";
                }
                if (!isClsid(clsid)) {
                    continue;
                }
                auto& entry = byClsid[clsid];
                if (entry.id.empty()) {
                    entry.kind = ContextMenuEntry::Kind::Packaged;
                    entry.id = clsid;
                    entry.clsid = clsid;
                    entry.keyName = utf8::toWide(verb.attribute("Id").as_string());
                    // The menu's own words come from the app at run time: the verb's id and the package's
                    // short name ("OpenTerminalHere (WindowsTerminal)").
                    const std::wstring shortName = identity.substr(std::min(identity.rfind(L'.') + 1, identity.size()));
                    entry.name = shortName.empty() ? entry.keyName : entry.keyName + L" (" + shortName + L")";
                    entry.source = identity;
                    entry.hidden = std::ranges::any_of(blocked, [&](const std::wstring& b) { return text::iequals(b, clsid); });
                }
                if (std::ranges::none_of(entry.places, [&](const std::wstring& p) { return text::iequals(p, place); })) {
                    entry.places.push_back(place);
                }
            }
        }
    }
}

RegistryWrite stringWrite(std::wstring key, std::wstring name, std::wstring_view value, DWORD type = REG_SZ) {
    RegistryWrite w;
    w.key = std::move(key);
    w.name = std::move(name);
    w.type = type;
    for (const wchar_t c : value) {
        w.data.push_back(static_cast<std::uint8_t>(c & 0xFF));
        w.data.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    }
    w.data.push_back(0);
    w.data.push_back(0);
    return w;
}

bool knownPlace(std::wstring_view place) {
    return std::ranges::any_of(contextMenuPlaces(), [&](const std::wstring& p) { return text::iequals(p, place); });
}

bool background(std::wstring_view place) {
    return text::iequals(place, L"Directory\\Background") || text::iequals(place, L"DesktopBackground");
}

} // namespace

const std::vector<std::wstring>& contextMenuPlaces() {
    static const std::vector<std::wstring> places = {L"*",      L"AllFilesystemObjects", L"Directory", L"Folder",
                                                     L"Directory\\Background", L"Drive", L"DesktopBackground"};
    return places;
}

std::vector<ContextMenuEntry> readPackagedContextMenu(const std::filesystem::path& mountDir) {
    std::map<std::wstring, ContextMenuEntry> byClsid;
    readPackaged(mountDir, byClsid, {});
    std::vector<ContextMenuEntry> out;
    for (auto& [clsid, e] : byClsid) {
        out.push_back(std::move(e));
    }
    return out;
}

Result<std::vector<ContextMenuEntry>> readContextMenu(const std::filesystem::path& mountDir) {
    const auto guard = preferImageLanguage(mountDir); // "@shell32.dll,-8506" in the image's language
    auto hive = OffRegKey::openHive(hiveFilePath(mountDir, OfflineHiveFile::Software));
    if (!hive) {
        return std::unexpected(hive.error());
    }
    std::vector<std::wstring> blocked;
    if (const OffRegKey key = hive->open(kBlockedInHive)) {
        for (const auto& v : key.values(/*withData=*/false)) {
            blocked.push_back(v.name);
        }
    }
    auto isBlocked = [&](const std::wstring& clsid) {
        return std::ranges::any_of(blocked, [&](const std::wstring& b) { return text::iequals(b, clsid); });
    };
    std::vector<ContextMenuEntry> entries;
    std::map<std::wstring, ContextMenuEntry> byClsid; // handlers and packaged items, one per CLSID
    for (const auto& place : contextMenuPlaces()) {
        const std::wstring classes = L"Classes\\" + place;
        if (const OffRegKey shell = hive->open(classes + L"\\shell")) {
            for (const auto& verb : shell.subkeys()) {
                const OffRegKey key = shell.open(verb);
                ContextMenuEntry e;
                e.kind = ContextMenuEntry::Kind::Verb;
                e.id = place + L"\\shell\\" + verb;
                e.keyName = verb;
                e.places = {place};
                std::wstring name = stringOf(key, L"MUIVerb");
                if (name.empty()) {
                    name = stringOf(key, L"");
                }
                name = withoutAmpersand(resolveImageText(name, mountDir));
                e.name = name.empty() ? verb : name;
                e.command = stringOf(key.open(L"command"), L"");
                e.extended = hasValue(key, L"Extended");
                e.hidden = hasValue(key, L"ProgrammaticAccessOnly") || hasValue(key, L"LegacyDisable");
                entries.push_back(std::move(e));
            }
        }
        if (const OffRegKey handlers = hive->open(classes + L"\\shellex\\ContextMenuHandlers")) {
            for (const auto& name : handlers.subkeys()) {
                std::wstring clsid = stringOf(handlers.open(name), L"");
                if (!isClsid(clsid) && isClsid(name)) {
                    clsid = name;
                }
                if (!isClsid(clsid)) {
                    continue; // "-{…}" style switched-off entries and broken ones
                }
                clsid = upperClsid(clsid);
                auto& e = byClsid[clsid];
                if (e.id.empty()) {
                    e.kind = ContextMenuEntry::Kind::Handler;
                    e.id = clsid;
                    e.clsid = clsid;
                    e.keyName = name;
                    const OffRegKey cls = hive->open(L"Classes\\CLSID\\" + clsid);
                    const std::wstring display = withoutAmpersand(resolveImageText(stringOf(cls, L""), mountDir));
                    e.name = !display.empty() ? display : (isClsid(name) ? clsid : name);
                    e.source = stringOf(cls.open(L"InprocServer32"), L"");
                    e.hidden = isBlocked(clsid);
                }
                if (std::ranges::none_of(e.places, [&](const std::wstring& p) { return text::iequals(p, place); })) {
                    e.places.push_back(place);
                }
            }
        }
    }
    readPackaged(mountDir, byClsid, blocked);
    for (auto& [clsid, e] : byClsid) {
        entries.push_back(std::move(e));
    }
    std::ranges::sort(entries, [](const ContextMenuEntry& a, const ContextMenuEntry& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1,
                               nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    log::info("reg", std::format(L"{} context menu entries read from {}", entries.size(), mountDir.wstring()));
    return entries;
}

std::vector<RegistryWrite> contextMenuHideWrites(const ContextMenuEntry& entry) {
    if (entry.hidden) {
        return {};
    }
    if (entry.kind == ContextMenuEntry::Kind::Verb) {
        return {stringWrite(L"HKLM\\SOFTWARE\\Classes\\" + entry.id, L"ProgrammaticAccessOnly", L"")};
    }
    return {stringWrite(kBlocked, entry.clsid, entry.name)};
}

std::vector<RegistryWrite> contextMenuShowWrites(const ContextMenuEntry& entry) {
    if (!entry.hidden) {
        return {};
    }
    auto remove = [](std::wstring key, std::wstring name) {
        RegistryWrite w;
        w.kind = RegistryWrite::Kind::DeleteValue;
        w.key = std::move(key);
        w.name = std::move(name);
        return w;
    };
    if (entry.kind == ContextMenuEntry::Kind::Verb) {
        const std::wstring key = L"HKLM\\SOFTWARE\\Classes\\" + entry.id;
        return {remove(key, L"ProgrammaticAccessOnly"), remove(key, L"LegacyDisable")};
    }
    return {remove(kBlocked, entry.clsid)};
}

std::wstring contextMenuItemId(std::wstring_view name) {
    std::wstring id;
    for (const wchar_t c : name) {
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) {
            id.push_back(c);
        } else if (c >= L'A' && c <= L'Z') {
            id.push_back(static_cast<wchar_t>(c - L'A' + L'a'));
        } else if (!id.empty() && id.back() != L'-') {
            id.push_back(L'-');
        }
        if (id.size() >= 40) {
            break;
        }
    }
    while (!id.empty() && id.back() == L'-') {
        id.pop_back();
    }
    return id.empty() ? std::wstring(L"item") : id;
}

Result<std::vector<RegistryWrite>> contextMenuAddWrites(const ContextMenuItem& item) {
    if (text::trim(item.name).empty()) {
        return fail(ErrorCode::InvalidArgument, L"the item needs a name", item.id);
    }
    if (text::trim(item.command).empty()) {
        return fail(ErrorCode::InvalidArgument, L"the item needs a command", item.name);
    }
    if (item.places.empty()) {
        return fail(ErrorCode::InvalidArgument, L"the item needs a place", item.name);
    }
    if (item.id.empty() || item.id.size() > 48 ||
        !std::ranges::all_of(item.id, [](wchar_t c) { return std::iswalnum(c) || c == L'-' || c == L'.'; })) {
        return fail(ErrorCode::InvalidArgument, L"bad item id", item.id);
    }
    std::vector<RegistryWrite> writes;
    for (const auto& place : item.places) {
        if (!knownPlace(place)) {
            return fail(ErrorCode::InvalidArgument, L"unknown place", place);
        }
        const std::wstring key = L"HKLM\\SOFTWARE\\Classes\\" + place + L"\\shell\\" + kItemPrefix + item.id;
        writes.push_back(stringWrite(key, L"", std::wstring(text::trim(item.name))));
        if (!text::trim(item.icon).empty()) {
            writes.push_back(stringWrite(key, L"Icon", std::wstring(text::trim(item.icon))));
        }
        if (item.extended) {
            writes.push_back(stringWrite(key, L"Extended", L""));
        }
        // A background click has no item: %V is its folder; %1 would be empty there.
        std::wstring command(text::trim(item.command));
        if (background(place)) {
            for (std::size_t at = command.find(L"%1"); at != std::wstring::npos; at = command.find(L"%1", at + 2)) {
                command[at + 1] = L'V';
            }
        }
        writes.push_back(stringWrite(key + L"\\command", L"", command, REG_EXPAND_SZ));
    }
    return writes;
}

std::wstring contextMenuOperationJson(const ContextMenuOperation& op) {
    nlohmann::json doc;
    doc["name"] = utf8::fromWide(op.name);
    if (op.item) {
        nlohmann::json places = nlohmann::json::array();
        for (const auto& p : op.item->places) {
            places.push_back(utf8::fromWide(p));
        }
        doc["item"] = {{"id", utf8::fromWide(op.item->id)},     {"name", utf8::fromWide(op.item->name)},
                       {"command", utf8::fromWide(op.item->command)}, {"icon", utf8::fromWide(op.item->icon)},
                       {"places", std::move(places)},         {"extended", op.item->extended}};
    }
    nlohmann::json writes = nlohmann::json::array();
    for (const auto& w : op.writes) {
        writes.push_back({{"key", utf8::fromWide(w.key)}, {"name", utf8::fromWide(w.name)}, {"value", utf8::fromWide(formatRegValue(w))}});
    }
    doc["writes"] = std::move(writes);
    return utf8::toWide(doc.dump());
}

Result<ContextMenuOperation> parseContextMenuOperation(std::wstring_view json) {
    const auto doc = nlohmann::json::parse(utf8::fromWide(std::wstring(json)), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"context menu operation is not JSON", std::wstring(json.substr(0, 80)));
    }
    auto str = [](const nlohmann::json& j, const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_string() ? utf8::toWide(it->get<std::string>()) : std::wstring();
    };
    ContextMenuOperation op;
    op.name = str(doc, "name");
    if (const auto it = doc.find("item"); it != doc.end() && it->is_object()) {
        ContextMenuItem item;
        item.id = str(*it, "id");
        item.name = str(*it, "name");
        item.command = str(*it, "command");
        item.icon = str(*it, "icon");
        if (const auto places = it->find("places"); places != it->end() && places->is_array()) {
            for (const auto& p : *places) {
                if (p.is_string()) {
                    item.places.push_back(utf8::toWide(p.get<std::string>()));
                }
            }
        }
        if (const auto ext = it->find("extended"); ext != it->end() && ext->is_boolean()) {
            item.extended = ext->get<bool>();
        }
        op.item = std::move(item);
    }
    if (const auto it = doc.find("writes"); it != doc.end() && it->is_array()) {
        for (const auto& w : *it) {
            if (!w.is_object()) {
                continue;
            }
            auto write = parseRegValue(str(w, "key"), str(w, "name"), str(w, "value"));
            if (!write) {
                return std::unexpected(write.error());
            }
            op.writes.push_back(std::move(*write));
        }
    }
    return op;
}

} // namespace wl::core
