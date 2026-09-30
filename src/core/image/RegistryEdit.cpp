#include "core/image/RegistryEdit.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <optional>
#include <format>
#include <fstream>
#include <sstream>

namespace wl::core {

namespace {

bool iequals(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && (a.empty() || CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                                                     static_cast<int>(b.size()), TRUE) == CSTR_EQUAL);
}

bool istartsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

// "Software\Classes\X" with prefix "Software\Classes" → "X" (only on a whole path segment).
std::optional<std::wstring> stripSegmentPrefix(std::wstring_view path, std::wstring_view prefix) {
    if (iequals(path, prefix)) {
        return std::wstring();
    }
    if (path.size() > prefix.size() && istartsWith(path, prefix) && path[prefix.size()] == L'\\') {
        return std::wstring(path.substr(prefix.size() + 1));
    }
    return std::nullopt;
}

std::wstring trim(std::wstring_view text) {
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && std::iswspace(text[a])) {
        ++a;
    }
    while (b > a && std::iswspace(text[b - 1])) {
        --b;
    }
    return std::wstring(text.substr(a, b - a));
}

Error lineError(std::size_t line, std::wstring what) {
    return Error{ErrorCode::ParseError, std::move(what), std::format(L".reg line {}", line), 0};
}

std::vector<std::uint8_t> wideBytes(std::wstring_view text, bool terminator) {
    std::vector<std::uint8_t> data((text.size() + (terminator ? 1 : 0)) * sizeof(wchar_t), 0);
    std::memcpy(data.data(), text.data(), text.size() * sizeof(wchar_t));
    return data;
}

// Parses a quoted .reg string starting at text[pos] == '"'. Returns the unescaped text and moves
// pos past the closing quote.
std::optional<std::wstring> quoted(std::wstring_view text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != L'"') {
        return std::nullopt;
    }
    std::wstring out;
    for (++pos; pos < text.size(); ++pos) {
        const wchar_t c = text[pos];
        if (c == L'\\' && pos + 1 < text.size()) {
            out.push_back(text[++pos]);
        } else if (c == L'"') {
            ++pos;
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> hexBytes(std::wstring_view text) {
    std::vector<std::uint8_t> bytes;
    std::wstring token;
    auto flush = [&]() -> bool {
        const std::wstring t = trim(token);
        token.clear();
        if (t.empty()) {
            return true;
        }
        if (t.size() > 2) {
            return false;
        }
        wchar_t* end = nullptr;
        const unsigned long v = std::wcstoul(t.c_str(), &end, 16);
        if (*end != L'\0') {
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>(v));
        return true;
    };
    for (const wchar_t c : text) {
        if (c == L',') {
            if (!flush()) {
                return std::nullopt;
            }
        } else if (c != L'\\' && c != L'\r' && c != L'\n') {
            token.push_back(c);
        }
    }
    if (!flush()) {
        return std::nullopt;
    }
    return bytes;
}

// REGEDIT4 hex(1/2/7) data: ANSI bytes (NUL-separated for multi-strings) → UTF-16 bytes.
std::vector<std::uint8_t> ansiToUtf16Bytes(const std::vector<std::uint8_t>& ansi) {
    if (ansi.empty()) {
        return {};
    }
    const auto* src = reinterpret_cast<const char*>(ansi.data());
    const int n = MultiByteToWideChar(CP_ACP, 0, src, static_cast<int>(ansi.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_ACP, 0, src, static_cast<int>(ansi.size()), wide.data(), n);
    std::vector<std::uint8_t> out(wide.size() * sizeof(wchar_t));
    std::memcpy(out.data(), wide.data(), out.size());
    return out;
}

std::wstring escape(std::wstring_view text) {
    std::wstring out;
    for (const wchar_t c : text) {
        if (c == L'\\' || c == L'"') {
            out.push_back(L'\\');
        }
        out.push_back(c);
    }
    return out;
}

} // namespace

std::wstring normalizeRegistryKey(std::wstring_view key) {
    std::wstring k = trim(key);
    while (!k.empty() && k.back() == L'\\') {
        k.pop_back();
    }
    const std::pair<std::wstring_view, std::wstring_view> roots[] = {
        {L"HKEY_LOCAL_MACHINE", L"HKLM"}, {L"HKEY_CURRENT_USER", L"HKCU"}, {L"HKEY_USERS", L"HKU"},
        {L"HKEY_CLASSES_ROOT", L"HKCR"},  {L"HKEY_CURRENT_CONFIG", L"HKCC"}, {L"HKLM", L"HKLM"},
        {L"HKCU", L"HKCU"},               {L"HKU", L"HKU"},                  {L"HKCR", L"HKCR"},
        {L"HKCC", L"HKCC"},
    };
    for (const auto& [longName, shortName] : roots) {
        if (auto rest = stripSegmentPrefix(k, longName)) {
            return rest->empty() ? std::wstring(shortName) : std::wstring(shortName) + L"\\" + *rest;
        }
    }
    return {};
}

bool isUserKey(std::wstring_view key) {
    return istartsWith(normalizeRegistryKey(key), L"HKCU");
}

bool isPostSetupOnlyKey(std::wstring_view rawKey) {
    const std::wstring key = normalizeRegistryKey(rawKey);
    // LocalService / NetworkService profiles and the current hardware profile.
    return stripSegmentPrefix(key, L"HKCC") || stripSegmentPrefix(key, L"HKU\\S-1-5-19") ||
           stripSegmentPrefix(key, L"HKU\\S-1-5-20");
}

Result<OfflineKey> mapOfflineKey(std::wstring_view rawKey) {
    const std::wstring key = normalizeRegistryKey(rawKey);
    if (auto rest = stripSegmentPrefix(key, L"HKLM\\SOFTWARE")) {
        return OfflineKey{OfflineHiveFile::Software, *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKLM\\SYSTEM")) {
        return OfflineKey{OfflineHiveFile::System, *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKCR")) {
        return OfflineKey{OfflineHiveFile::Software, rest->empty() ? L"Classes" : L"Classes\\" + *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKCU\\Software\\Classes")) {
        return OfflineKey{OfflineHiveFile::DefaultUserClasses, *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKCU")) {
        return OfflineKey{OfflineHiveFile::DefaultUser, *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKU\\.DEFAULT")) {
        return OfflineKey{OfflineHiveFile::DotDefault, *rest};
    }
    if (auto rest = stripSegmentPrefix(key, L"HKU\\S-1-5-18")) { // LocalSystem: the same hive
        return OfflineKey{OfflineHiveFile::DotDefault, *rest};
    }
    return fail(ErrorCode::Unsupported, L"registry root not available in an offline image", std::wstring(rawKey));
}

std::filesystem::path hiveFilePath(const std::filesystem::path& mountDir, OfflineHiveFile hive) {
    const auto config = mountDir / L"Windows" / L"System32" / L"config";
    switch (hive) {
    case OfflineHiveFile::Software: return config / L"SOFTWARE";
    case OfflineHiveFile::System: return config / L"SYSTEM";
    case OfflineHiveFile::DotDefault: return config / L"DEFAULT";
    case OfflineHiveFile::DefaultUser: return mountDir / L"Users" / L"Default" / L"NTUSER.DAT";
    case OfflineHiveFile::DefaultUserClasses:
        return mountDir / L"Users" / L"Default" / L"AppData" / L"Local" / L"Microsoft" / L"Windows" / L"UsrClass.dat";
    }
    return config / L"SOFTWARE";
}

Result<RegistryWrite> parseRegValue(std::wstring key, std::wstring name, std::wstring_view rawValue) {
    RegistryWrite w;
    w.key = normalizeRegistryKey(key);
    if (w.key.empty()) {
        return fail(ErrorCode::ParseError, L"unknown registry root", key);
    }
    w.name = std::move(name);
    const std::wstring value = trim(rawValue);
    if (value == L"[-]") {
        w.kind = RegistryWrite::Kind::DeleteKey;
        w.name.clear();
        return w;
    }
    if (value == L"[+]") {
        w.kind = RegistryWrite::Kind::CreateKey;
        w.name.clear();
        return w;
    }
    if (value == L"-") {
        w.kind = RegistryWrite::Kind::DeleteValue;
        return w;
    }
    if (!value.empty() && value.front() == L'"') {
        std::size_t pos = 0;
        auto text = quoted(value, pos);
        if (!text || !trim(std::wstring_view(value).substr(pos)).empty()) {
            return fail(ErrorCode::ParseError, L"bad string value", value);
        }
        w.type = REG_SZ;
        w.data = wideBytes(*text, true);
        return w;
    }
    if (istartsWith(value, L"dword:")) {
        const std::wstring digits = trim(std::wstring_view(value).substr(6));
        wchar_t* end = nullptr;
        const unsigned long long v = std::wcstoull(digits.c_str(), &end, 16);
        if (digits.empty() || digits.size() > 8 || *end != L'\0' || !std::iswxdigit(digits.front())) {
            return fail(ErrorCode::ParseError, L"bad dword value", value);
        }
        const auto d = static_cast<std::uint32_t>(v);
        w.type = REG_DWORD;
        w.data.resize(4);
        std::memcpy(w.data.data(), &d, 4);
        return w;
    }
    if (istartsWith(value, L"hex")) {
        std::size_t colon = value.find(L':');
        if (colon == std::wstring::npos) {
            return fail(ErrorCode::ParseError, L"bad hex value", value);
        }
        std::uint32_t type = REG_BINARY;
        const std::wstring spec = value.substr(3, colon - 3); // "" or "(2)"
        if (!spec.empty()) {
            if (spec.size() < 3 || spec.front() != L'(' || spec.back() != L')') {
                return fail(ErrorCode::ParseError, L"bad hex type", value);
            }
            wchar_t* end = nullptr;
            const std::wstring digits = spec.substr(1, spec.size() - 2);
            type = static_cast<std::uint32_t>(std::wcstoul(digits.c_str(), &end, 16));
            if (*end != L'\0') {
                return fail(ErrorCode::ParseError, L"bad hex type", value);
            }
        }
        auto bytes = hexBytes(std::wstring_view(value).substr(colon + 1));
        if (!bytes) {
            return fail(ErrorCode::ParseError, L"bad hex bytes", value);
        }
        w.type = type;
        w.data = std::move(*bytes);
        return w;
    }
    return fail(ErrorCode::ParseError, L"unknown value syntax", value);
}

std::wstring formatRegValue(const RegistryWrite& w) {
    switch (w.kind) {
    case RegistryWrite::Kind::DeleteKey: return L"[-]";
    case RegistryWrite::Kind::CreateKey: return L"[+]";
    case RegistryWrite::Kind::DeleteValue: return L"-";
    case RegistryWrite::Kind::Set: break;
    }
    if (w.type == REG_SZ && w.data.size() % 2 == 0) {
        std::wstring text(w.data.size() / 2, L'\0');
        std::memcpy(text.data(), w.data.data(), text.size() * sizeof(wchar_t));
        while (!text.empty() && text.back() == L'\0') {
            text.pop_back();
        }
        if (text.find(L'\0') == std::wstring::npos) {
            return L"\"" + escape(text) + L"\"";
        }
    }
    if (w.type == REG_DWORD && w.data.size() == 4) {
        std::uint32_t d = 0;
        std::memcpy(&d, w.data.data(), 4);
        return std::format(L"dword:{:08x}", d);
    }
    std::wstring out = w.type == REG_BINARY ? L"hex:" : std::format(L"hex({:x}):", w.type);
    for (std::size_t i = 0; i < w.data.size(); ++i) {
        out += std::format(L"{}{:02x}", i ? L"," : L"", w.data[i]);
    }
    return out;
}

std::wstring registryTarget(const RegistryWrite& w) {
    // Key operations get slots of their own, apart from the key's default value ("<key>::"): a file
    // that deletes a key and then sets its default value needs both. No key name ends in '\', so
    // these forms cannot clash with a value; the slashes are trimmed again when the target is parsed.
    switch (w.kind) {
    case RegistryWrite::Kind::CreateKey: return w.key + L"\\::";
    case RegistryWrite::Kind::DeleteKey: return w.key + L"\\\\::";
    default: break;
    }
    return w.key + L"::" + w.name;
}

Result<RegistryWrite> registryWriteFrom(std::wstring_view target, std::wstring_view value) {
    const auto sep = target.find(L"::");
    if (sep == std::wstring_view::npos) {
        return fail(ErrorCode::ParseError, L"registry target needs <key>::<name>", std::wstring(target));
    }
    return parseRegValue(std::wstring(target.substr(0, sep)), std::wstring(target.substr(sep + 2)), value);
}

Result<std::vector<RegistryWrite>> parseRegText(std::wstring_view text) {
    std::vector<std::wstring> lines;
    {
        std::wstring line;
        for (const wchar_t c : text) {
            if (c == L'\n') {
                lines.push_back(std::move(line));
                line.clear();
            } else if (c != L'\r') {
                line.push_back(c);
            }
        }
        lines.push_back(std::move(line));
    }
    std::vector<RegistryWrite> writes;
    std::wstring key;
    bool header = false;
    bool regedit4 = false;     // REGEDIT4: hex(1/2/7) payloads are ANSI bytes, not UTF-16
    bool skipValues = false;   // after [-key], until the next [key] (as regedit does)
    bool emptySection = false; // "[key]" seen, no value yet: importing it still creates the key
    auto closeSection = [&] {
        if (emptySection) {
            RegistryWrite w;
            w.kind = RegistryWrite::Kind::CreateKey;
            w.key = key;
            writes.push_back(std::move(w));
        }
        emptySection = false;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t lineNo = i + 1;
        std::wstring line = trim(lines[i]);
        if (!line.empty() && line.front() == L';') {
            continue; // a comment ending in '\\' does not swallow the next line
        }
        // Continuation: a hex list ending with '\\' goes on with the next line.
        while (!line.empty() && line.back() == L'\\' && i + 1 < lines.size()) {
            line.pop_back();
            line += trim(lines[++i]);
        }
        if (line.empty() || line.front() == L';') {
            continue;
        }
        if (!header) {
            if (line == L"Windows Registry Editor Version 5.00" || line == L"REGEDIT4") {
                header = true;
                regedit4 = line == L"REGEDIT4";
                continue;
            }
            return std::unexpected(lineError(lineNo, L"not a .reg file (missing header)"));
        }
        if (line.front() == L'[') {
            if (line.back() != L']') {
                return std::unexpected(lineError(lineNo, L"unterminated key"));
            }
            closeSection();
            const bool remove = line.size() > 2 && line[1] == L'-';
            std::wstring raw = line.substr(remove ? 2 : 1, line.size() - (remove ? 3 : 2));
            key = normalizeRegistryKey(raw);
            if (key.empty()) {
                return std::unexpected(lineError(lineNo, L"unknown registry root: " + raw));
            }
            skipValues = remove;
            emptySection = !remove;
            if (remove) {
                RegistryWrite w;
                w.kind = RegistryWrite::Kind::DeleteKey;
                w.key = key;
                writes.push_back(std::move(w));
            }
            continue;
        }
        if (skipValues) {
            continue; // values under a deleted key are ignored until the next [key]
        }
        if (key.empty()) {
            return std::unexpected(lineError(lineNo, L"value outside of a key"));
        }
        std::wstring name;
        std::size_t pos = 0;
        if (line.front() == L'@') {
            pos = 1;
        } else {
            auto n = quoted(line, pos);
            if (!n) {
                return std::unexpected(lineError(lineNo, L"bad value name"));
            }
            name = std::move(*n);
        }
        while (pos < line.size() && std::iswspace(line[pos])) {
            ++pos;
        }
        if (pos >= line.size() || line[pos] != L'=') {
            return std::unexpected(lineError(lineNo, L"expected '='"));
        }
        auto write = parseRegValue(key, name, std::wstring_view(line).substr(pos + 1));
        if (!write) {
            return std::unexpected(lineError(lineNo, write.error().message + L": " + write.error().context));
        }
        if (regedit4 && write->kind == RegistryWrite::Kind::Set &&
            (write->type == REG_SZ || write->type == REG_EXPAND_SZ || write->type == REG_MULTI_SZ) &&
            !std::wstring_view(line).substr(pos + 1).starts_with(L"\"")) {
            write->data = ansiToUtf16Bytes(write->data);
        }
        emptySection = false;
        writes.push_back(std::move(*write));
    }
    if (!header) {
        return std::unexpected(lineError(1, L"not a .reg file (missing header)"));
    }
    closeSection();
    return writes;
}

Result<std::vector<RegistryWrite>> readRegFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, L"could not open .reg file", file.wstring());
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto writes = parseRegText(utf8::decodeText(buffer.str()));
    if (!writes) {
        auto e = writes.error();
        e.context = file.filename().wstring() + L" · " + e.context;
        return std::unexpected(std::move(e));
    }
    return writes;
}

namespace {

const wchar_t* const kRegHeader = L"Windows Registry Editor Version 5.00\r\n";

// The [key] groups of a .reg file, in order (no header): what follows kRegHeader.
std::wstring formatRegBody(const std::vector<RegistryWrite>& writes) {
    auto longKey = [](const std::wstring& key) {
        const std::pair<std::wstring_view, std::wstring_view> roots[] = {
            {L"HKLM", L"HKEY_LOCAL_MACHINE"}, {L"HKCU", L"HKEY_CURRENT_USER"}, {L"HKU", L"HKEY_USERS"},
            {L"HKCR", L"HKEY_CLASSES_ROOT"},  {L"HKCC", L"HKEY_CURRENT_CONFIG"}};
        for (const auto& [shortName, longName] : roots) {
            if (auto rest = stripSegmentPrefix(key, shortName)) {
                return std::wstring(longName) + (rest->empty() ? L"" : L"\\" + *rest);
            }
        }
        return key;
    };
    std::wstring out;
    std::wstring current;
    bool open = false; // a [key] group is open: more values of `current` go under it
    for (const auto& w : writes) {
        if (w.kind == RegistryWrite::Kind::DeleteKey) {
            out += L"\r\n[-" + longKey(w.key) + L"]\r\n";
            open = false;
            continue;
        }
        // A created key always gets its own group: two in a row must stay two.
        if (!open || w.key != current || w.kind == RegistryWrite::Kind::CreateKey) {
            out += L"\r\n[" + longKey(w.key) + L"]\r\n";
            current = w.key;
            open = true;
        }
        if (w.kind == RegistryWrite::Kind::CreateKey) {
            open = false; // the next value opens the group again, so the key stays "created empty"
            continue;
        }
        out += (w.name.empty() ? std::wstring(L"@") : L"\"" + escape(w.name) + L"\"") + L"=" + formatRegValue(w) + L"\r\n";
    }
    return out;
}

} // namespace

std::wstring formatRegText(const std::vector<RegistryWrite>& writes) {
    return kRegHeader + formatRegBody(writes);
}

const wchar_t* const kSetupCompleteImportLine =
    L"%SystemRoot%\\System32\\reg.exe import \"%SystemRoot%\\Setup\\Scripts\\WinLove\\setupcomplete.reg\"";

DeferredScope deferredScope(const RegistryWrite& write) {
    return isUserKey(write.key) ? DeferredScope::User : DeferredScope::Machine;
}

std::filesystem::path deferredRegFile(const std::filesystem::path& mountDir, DeferredScope scope) {
    return mountDir / L"Windows" / L"Setup" / L"Scripts" / L"WinLove" /
           (scope == DeferredScope::User ? L"firstlogon-user.reg" : L"setupcomplete.reg");
}

namespace {

// Puts `write` at the end of `writes`, dropping what it supersedes: the same slot; for a new [-K]
// everything recorded for K. An earlier [-K] stays before later values, so the re-import
// reproduces "delete, then set" like the offline hive. Returns true when something was dropped.
bool replaceSlot(std::vector<RegistryWrite>& writes, const RegistryWrite& write) {
    const bool creates = write.kind == RegistryWrite::Kind::CreateKey;
    const auto dropped = std::erase_if(writes, [&](const RegistryWrite& w) {
        if (!iequals(w.key, write.key)) {
            return false;
        }
        if (write.kind == RegistryWrite::Kind::DeleteKey) {
            return true;
        }
        // A created key and the key's default value both have an empty name: different slots.
        return w.kind != RegistryWrite::Kind::DeleteKey && (w.kind == RegistryWrite::Kind::CreateKey) == creates &&
               iequals(w.name, write.name);
    });
    writes.push_back(write);
    return dropped > 0;
}

// UTF-16 LE with BOM, what reg.exe import and regedit read. `append` adds to an existing file.
Result<void> writeRegFile(const std::filesystem::path& file, std::wstring_view text, bool append) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) {
        return fail(ErrorCode::IoError, L"could not write deferred .reg file", file.wstring());
    }
    if (!append) {
        out.write("\xFF\xFE", 2);
    }
    out.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
    out.flush();
    return out ? Result<void>{} : fail(ErrorCode::IoError, L"could not write deferred .reg file", file.wstring());
}

} // namespace

DeferredRegistry::DeferredRegistry(std::filesystem::path mountDir) : m_mountDir(std::move(mountDir)) {
    m_machine.path = deferredRegFile(m_mountDir, DeferredScope::Machine);
    m_user.path = deferredRegFile(m_mountDir, DeferredScope::User);
}

DeferredRegistry::~DeferredRegistry() {
    close();
}

Result<void> DeferredRegistry::record(const RegistryWrite& write) {
    const bool user = deferredScope(write) == DeferredScope::User;
    File& file = user ? m_user : m_machine;
    std::error_code ec;
    const auto size = std::filesystem::file_size(file.path, ec);
    const bool exists = !ec && size > 0;
    if (!file.loaded) {
        // What earlier runs recorded (the same image can be serviced more than once).
        if (exists) {
            auto existing = readRegFile(file.path);
            if (!existing) {
                return std::unexpected(existing.error());
            }
            file.writes = std::move(*existing);
        }
        file.loaded = true;
    }
    const std::wstring entry = formatRegBody({write});
    if (auto r = writeRegFile(file.path, exists ? entry : kRegHeader + entry, /*append=*/exists); !r) {
        return r;
    }
    file.superseded = replaceSlot(file.writes, write) || file.superseded;
    if (user) {
        m_userHookPending = !m_userHooked;
        return {};
    }
    if (!m_machineHooked) {
        if (auto r = ensureSetupCompleteImport(m_mountDir / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd"); !r) {
            return r;
        }
        m_machineHooked = true;
    }
    return {};
}

bool DeferredRegistry::takeUserHook() noexcept {
    const bool pending = m_userHookPending;
    m_userHookPending = false;
    m_userHooked = m_userHooked || pending;
    return pending;
}

void DeferredRegistry::close() {
    for (File* file : {&m_machine, &m_user}) {
        if (!file->superseded) {
            continue;
        }
        // Only tidying: the appended file already imports to the same values.
        if (auto r = writeRegFile(file->path, formatRegText(file->writes), /*append=*/false); !r) {
            log::warn("reg", describe(r.error()));
        }
        file->superseded = false;
    }
}

Result<void> ensureSetupCompleteImport(const std::filesystem::path& setupComplete) {
    return ensureSetupCompleteLine(setupComplete, utf8::fromWide(kSetupCompleteImportLine),
                                   "WinLove: settings Windows resets during OOBE");
}

Result<void> ensureSetupCompleteLine(const std::filesystem::path& setupComplete, std::string_view wanted,
                                     std::string_view comment) {
    std::string existing;
    std::error_code ec;
    if (std::filesystem::exists(setupComplete, ec)) {
        std::ifstream in(setupComplete, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        existing = buffer.str();
    }
    const std::string line(wanted);
    if (existing.find(line) != std::string::npos) {
        return {};
    }
    // Insert near the top: an existing script may end with exit / shutdown / del %0.
    const std::string block = "rem " + std::string(comment) + "\r\n" + line + "\r\n";
    std::string text;
    if (existing.empty()) {
        text = "@echo off\r\n" + block;
    } else {
        std::size_t at = 0;
        const std::size_t firstEnd = existing.find('\n');
        std::string first = existing.substr(0, firstEnd == std::string::npos ? existing.size() : firstEnd);
        for (auto& c : first) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (first.find("@echo off") != std::string::npos) {
            at = firstEnd == std::string::npos ? existing.size() : firstEnd + 1;
        }
        text = existing.substr(0, at);
        if (!text.empty() && text.back() != '\n') {
            text += "\r\n";
        }
        text += block + existing.substr(at);
    }
    std::filesystem::create_directories(setupComplete.parent_path(), ec);
    std::ofstream out(setupComplete, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out ? Result<void>{} : fail(ErrorCode::IoError, L"could not write SetupComplete.cmd", setupComplete.wstring());
}

Result<void> deferRegistryWrite(OfflineRegistry& registry, DeferredRegistry& deferred, const RegistryWrite& write) {
    // HKCC and the service accounts exist only on the installed system: recorded, not written here.
    if (!isPostSetupOnlyKey(write.key)) {
        if (auto r = registry.apply(write); !r) {
            return r;
        }
    }
    if (auto r = deferred.record(write); !r) {
        return r;
    }
    if (!deferred.takeUserHook()) {
        return {};
    }
    // Default profile RunOnce: copied into every new account, run once at its first logon.
    RegistryWrite hook;
    hook.key = L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    hook.name = L"WinLoveFirstLogon";
    hook.type = REG_EXPAND_SZ;
    hook.data = wideBytes(L"%SystemRoot%\\System32\\reg.exe import \"%SystemRoot%\\Setup\\Scripts\\WinLove\\firstlogon-user.reg\"", true);
    return registry.apply(hook);
}

// ---- OfflineRegistry -----------------------------------------------------------------------

OfflineRegistry::OfflineRegistry(std::filesystem::path mountDir) : m_mountDir(std::move(mountDir)) {}

OfflineRegistry::~OfflineRegistry() {
    close();
}

void OfflineRegistry::close() {
    m_hives.clear();
}

Result<std::wstring> OfflineRegistry::resolve(const OfflineKey& key) {
    auto& hive = m_hives[key.hive];
    if (!hive) {
        auto loaded = OfflineHive::load(hiveFilePath(m_mountDir, key.hive));
        if (!loaded) {
            m_hives.erase(key.hive);
            return std::unexpected(loaded.error());
        }
        hive = std::make_unique<OfflineHive>(std::move(*loaded));
    }
    std::wstring path = key.path;
    if (key.hive == OfflineHiveFile::System) {
        if (auto rest = stripSegmentPrefix(path, L"CurrentControlSet")) {
            std::uint32_t current = 1;
            if (auto select = RegKey::open(hive->root(), L"Select")) {
                current = select->dword(L"Current").value_or(1);
            }
            path = std::format(L"ControlSet{:03}", current) + (rest->empty() ? L"" : L"\\" + *rest);
        }
    }
    return hive->keyName() + (path.empty() ? L"" : L"\\" + path);
}

Result<void> OfflineRegistry::apply(const RegistryWrite& write) {
    auto mapped = mapOfflineKey(write.key);
    if (!mapped) {
        return std::unexpected(mapped.error());
    }
    auto full = resolve(*mapped);
    if (!full) {
        return std::unexpected(full.error());
    }
    const std::wstring context = registryTarget(write);
    auto win32 = [&](LSTATUS status, const wchar_t* what) -> Result<void> {
        if (status == ERROR_SUCCESS) {
            return {};
        }
        return fail(status == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::IoError, what, context,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
    };
    if (write.kind == RegistryWrite::Kind::CreateKey) {
        HKEY created = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                         KEY_QUERY_VALUE, nullptr, &created, nullptr);
        if (status == ERROR_ACCESS_DENIED) { // TrustedInstaller-owned parent: see the value write below
            status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE,
                                     KEY_QUERY_VALUE, nullptr, &created, nullptr);
        }
        if (status == ERROR_SUCCESS) {
            RegCloseKey(created);
        }
        return win32(status, L"could not create registry key");
    }
    if (write.kind == RegistryWrite::Kind::DeleteKey) {
        if (mapped->path.empty()) {
            return fail(ErrorCode::InvalidArgument, L"refusing to delete a hive root", context);
        }
        LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, full->c_str());
        if (status == ERROR_ACCESS_DENIED) {
            // TrustedInstaller-owned: open with backup/restore semantics (SeRestore), clear it, then
            // delete the (now empty) key through its parent.
            HKEY backup = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE, 0, nullptr,
                                &backup, nullptr) == ERROR_SUCCESS) {
                RegKey owned(backup);
                status = RegDeleteTreeW(owned.get(), nullptr);
                if (status == ERROR_SUCCESS) {
                    status = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, 0);
                }
            }
        }
        return win32(status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status, L"could not delete registry key");
    }
    HKEY raw = nullptr;
    if (write.kind == RegistryWrite::Kind::DeleteValue) {
        LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, KEY_SET_VALUE, &raw);
        if (status == ERROR_FILE_NOT_FOUND) {
            return {};
        }
        if (status == ERROR_ACCESS_DENIED) {
            status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE,
                                     KEY_SET_VALUE, nullptr, &raw, nullptr);
        }
        if (auto r = win32(status, L"could not open registry key"); !r) {
            return r;
        }
        RegKey key(raw);
        status = RegDeleteValueW(key.get(), write.name.c_str());
        return win32(status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status, L"could not delete registry value");
    }
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                     KEY_SET_VALUE, nullptr, &raw, nullptr);
    if (status == ERROR_ACCESS_DENIED) {
        // TrustedInstaller-owned keys: SeRestore lets a backup/restore open write regardless of the ACL.
        status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, full->c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE,
                                 KEY_SET_VALUE, nullptr, &raw, nullptr);
    }
    if (auto r = win32(status, L"could not create registry key"); !r) {
        return r;
    }
    RegKey key(raw);
    status = RegSetValueExW(key.get(), write.name.empty() ? nullptr : write.name.c_str(), 0, write.type,
                            write.data.empty() ? nullptr : write.data.data(), static_cast<DWORD>(write.data.size()));
    if (auto r = win32(status, L"could not write registry value"); !r) {
        return r;
    }
    log::info("reg", L"set " + context + L" = " + formatRegValue(write));
    return {};
}

} // namespace wl::core
