#include "core/policy/PolicyWrites.h"

#include "base/File.h"
#include "base/Text.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>

namespace wl::core::policy {

namespace {

std::vector<std::uint8_t> wide(std::wstring_view text) {
    std::vector<std::uint8_t> out;
    out.reserve((text.size() + 1) * 2);
    for (const wchar_t c : text) {
        out.push_back(static_cast<std::uint8_t>(c & 0xFF));
        out.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    }
    out.push_back(0);
    out.push_back(0);
    return out;
}

std::vector<std::uint8_t> multi(const std::vector<std::wstring>& lines) {
    std::vector<std::uint8_t> out;
    for (const auto& line : lines) {
        const auto bytes = wide(line);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    out.push_back(0);
    out.push_back(0);
    return out;
}

std::vector<std::uint8_t> littleEndian(std::uint64_t value, int bytes) {
    std::vector<std::uint8_t> out;
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
    return out;
}

std::vector<std::wstring> linesOf(std::wstring_view text) {
    std::vector<std::wstring> lines;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find(L'\n', at), text.size());
        std::wstring line(text.substr(at, end - at));
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        at = end + 1;
    }
    return lines;
}

std::optional<std::uint64_t> parseNumber(std::wstring_view text) {
    const std::string s = utf8::fromWide(std::wstring(text::trim(text)));
    std::uint64_t out = 0;
    if (s.empty()) {
        return std::nullopt;
    }
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return out;
}

class Writer {
public:
    explicit Writer(bool user) : m_root(user ? L"HKCU\\" : L"HKLM\\") {}

    void value(const std::wstring& key, const std::wstring& name, const PolicyValue& v, std::optional<std::uint32_t> defaultDword) {
        switch (v.kind) {
        case PolicyValue::Kind::None:
            if (defaultDword) {
                dword(key, name, *defaultDword);
            }
            return;
        case PolicyValue::Kind::Decimal: dword(key, name, v.number); return;
        case PolicyValue::Kind::LongDecimal: qword(key, name, v.number); return;
        case PolicyValue::Kind::String: string(key, name, v.text, false); return;
        case PolicyValue::Kind::Delete: remove(key, name); return;
        }
    }
    void list(const std::wstring& policyKey, const PolicyValueList& list) {
        const std::wstring listKey = list.key.empty() ? policyKey : list.key;
        for (const auto& item : list.items) {
            value(item.key.empty() ? listKey : item.key, item.valueName, item.value, std::nullopt);
        }
    }
    void dword(const std::wstring& key, const std::wstring& name, std::uint64_t n) {
        set(key, name, REG_DWORD, littleEndian(n & 0xFFFFFFFFull, 4));
    }
    void qword(const std::wstring& key, const std::wstring& name, std::uint64_t n) {
        set(key, name, REG_QWORD, littleEndian(n, 8));
    }
    void string(const std::wstring& key, const std::wstring& name, std::wstring_view text, bool expandable) {
        set(key, name, expandable ? REG_EXPAND_SZ : REG_SZ, wide(text));
    }
    void multiString(const std::wstring& key, const std::wstring& name, const std::vector<std::wstring>& lines) {
        set(key, name, REG_MULTI_SZ, multi(lines));
    }
    void remove(const std::wstring& key, const std::wstring& name) {
        RegistryWrite w;
        w.kind = RegistryWrite::Kind::DeleteValue;
        w.key = m_root + key;
        w.name = name;
        put(std::move(w));
    }
    [[nodiscard]] std::vector<RegistryWrite> take() { return std::move(m_writes); }

private:
    void set(const std::wstring& key, const std::wstring& name, std::uint32_t type, std::vector<std::uint8_t> data) {
        RegistryWrite w;
        w.key = m_root + key;
        w.name = name;
        w.type = type;
        w.data = std::move(data);
        put(std::move(w));
    }
    // One write per value: a later one (an element after the policy's own value) replaces it.
    void put(RegistryWrite w) {
        std::erase_if(m_writes, [&](const RegistryWrite& o) {
            return text::iequals(o.key, w.key) && text::iequals(o.name, w.name);
        });
        m_writes.push_back(std::move(w));
    }
    std::wstring m_root;
    std::vector<RegistryWrite> m_writes;
};

const PresentationControl* controlOf(const Policy& policy, std::wstring_view id) {
    const auto it = std::ranges::find_if(policy.presentation, [&](const PresentationControl& c) { return c.refId == id; });
    return it != policy.presentation.end() ? &*it : nullptr;
}

std::wstring valueOr(const PolicyValues& values, const std::wstring& id) {
    const auto it = values.find(id);
    return it != values.end() ? it->second : std::wstring();
}

} // namespace

PolicyValues defaultValues(const Policy& policy) {
    PolicyValues values;
    for (const auto& e : policy.elements) {
        const PresentationControl* c = controlOf(policy, e.id);
        switch (e.kind) {
        case PolicyElement::Kind::Boolean: values[e.id] = c && c->defaultChecked ? L"1" : L"0"; break;
        case PolicyElement::Kind::Decimal:
        case PolicyElement::Kind::LongDecimal:
            values[e.id] = std::to_wstring(c && c->hasDefaultValue ? std::clamp(c->defaultValue, e.minValue, e.maxValue) : e.minValue);
            break;
        case PolicyElement::Kind::Enum:
            if (!e.items.empty()) {
                const int item = c && c->defaultItem >= 0 && c->defaultItem < static_cast<int>(e.items.size()) ? c->defaultItem : 0;
                values[e.id] = std::to_wstring(item);
            }
            break;
        case PolicyElement::Kind::Text: values[e.id] = c ? c->defaultText : std::wstring(); break;
        case PolicyElement::Kind::MultiText:
        case PolicyElement::Kind::List: values[e.id] = {}; break;
        }
    }
    return values;
}

std::vector<std::wstring> missingValues(const Policy& policy, const PolicyValues& values) {
    std::vector<std::wstring> missing;
    for (const auto& e : policy.elements) {
        if (!e.required) {
            continue;
        }
        const std::wstring v = valueOr(values, e.id);
        const bool empty = e.kind == PolicyElement::Kind::List || e.kind == PolicyElement::Kind::MultiText
                               ? linesOf(v).empty()
                               : text::trim(v).empty();
        if (empty && e.kind != PolicyElement::Kind::Boolean) {
            missing.push_back(e.id);
        }
    }
    return missing;
}

Result<std::vector<RegistryWrite>> policyWrites(const Policy& policy, bool user, const PolicyChoice& choice) {
    Writer w(user);
    if (choice.state == PolicyState::NotConfigured) {
        return std::vector<RegistryWrite>{};
    }
    auto keyOf = [&](const PolicyElement& e) { return e.key.empty() ? policy.key : e.key; };
    if (choice.state == PolicyState::Disabled) {
        if (!policy.valueName.empty()) {
            w.value(policy.key, policy.valueName, policy.disabledValue, 0u);
        }
        w.list(policy.key, policy.disabledList);
        for (const auto& e : policy.elements) {
            if (e.kind == PolicyElement::Kind::List) {
                continue; // its values are named by the entries: nothing known to delete
            }
            if (e.kind == PolicyElement::Kind::Boolean && e.valueName.empty()) {
                continue; // only lists
            }
            w.remove(keyOf(e), e.valueName);
        }
        return w.take();
    }
    // Enabled.
    if (!policy.valueName.empty()) {
        w.value(policy.key, policy.valueName, policy.enabledValue, 1u);
    }
    w.list(policy.key, policy.enabledList);
    for (const auto& e : policy.elements) {
        const std::wstring key = keyOf(e);
        const std::wstring raw = valueOr(choice.values, e.id);
        switch (e.kind) {
        case PolicyElement::Kind::Boolean: {
            const bool on = raw == L"1" || text::iequals(raw, L"true");
            if (!e.valueName.empty()) {
                w.value(key, e.valueName, on ? e.trueValue : e.falseValue, on ? 1u : 0u);
            }
            w.list(key, on ? e.trueList : e.falseList);
            break;
        }
        case PolicyElement::Kind::Decimal:
        case PolicyElement::Kind::LongDecimal: {
            if (text::trim(raw).empty() && !e.required) {
                break;
            }
            const auto n = parseNumber(raw);
            if (!n) {
                return fail(ErrorCode::InvalidArgument, L"not a number", e.id + L" = " + raw);
            }
            if (*n < e.minValue || *n > e.maxValue) {
                return fail(ErrorCode::InvalidArgument,
                            L"out of range " + std::to_wstring(e.minValue) + L"…" + std::to_wstring(e.maxValue), e.id + L" = " + raw);
            }
            if (e.storeAsText) {
                w.string(key, e.valueName, std::to_wstring(*n), false);
            } else if (e.kind == PolicyElement::Kind::Decimal) {
                w.dword(key, e.valueName, *n);
            } else {
                w.qword(key, e.valueName, *n);
            }
            break;
        }
        case PolicyElement::Kind::Text:
            if (e.required && text::trim(raw).empty()) {
                return fail(ErrorCode::InvalidArgument, L"a value is required", e.id);
            }
            if (raw.size() > e.maxLength) {
                return fail(ErrorCode::InvalidArgument, L"longer than " + std::to_wstring(e.maxLength), e.id);
            }
            w.string(key, e.valueName, raw, e.expandable);
            break;
        case PolicyElement::Kind::MultiText: {
            const auto lines = linesOf(raw);
            if (e.required && lines.empty()) {
                return fail(ErrorCode::InvalidArgument, L"a value is required", e.id);
            }
            w.multiString(key, e.valueName, lines);
            break;
        }
        case PolicyElement::Kind::Enum: {
            if (e.items.empty()) {
                break;
            }
            const auto n = parseNumber(raw);
            if (!n || *n >= e.items.size()) {
                if (raw.empty() && !e.required) {
                    break;
                }
                return fail(ErrorCode::InvalidArgument, L"no such choice", e.id + L" = " + raw);
            }
            const auto& item = e.items[static_cast<std::size_t>(*n)];
            if (!e.valueName.empty()) {
                w.value(key, e.valueName, item.value, std::nullopt);
            }
            w.list(key, item.valueList);
            break;
        }
        case PolicyElement::Kind::List: {
            const auto lines = linesOf(raw);
            if (e.required && lines.empty()) {
                return fail(ErrorCode::InvalidArgument, L"a value is required", e.id);
            }
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (e.explicitValue) {
                    const auto eq = lines[i].find(L'=');
                    if (eq == std::wstring::npos || eq == 0) {
                        return fail(ErrorCode::InvalidArgument, L"\"name=value\" expected", e.id + L": " + lines[i]);
                    }
                    w.string(key, std::wstring(text::trim(std::wstring_view(lines[i]).substr(0, eq))),
                             text::trim(std::wstring_view(lines[i]).substr(eq + 1)), e.expandable);
                } else if (!e.valuePrefix.empty()) {
                    w.string(key, e.valuePrefix + std::to_wstring(i + 1), lines[i], e.expandable);
                } else {
                    w.string(key, lines[i], lines[i], e.expandable);
                }
            }
            break;
        }
        }
    }
    return w.take();
}

std::wstring policyTarget(std::wstring_view id, bool user) {
    return (user ? L"user\\" : L"machine\\") + std::wstring(id);
}

bool parsePolicyTarget(std::wstring_view target, std::wstring& id, bool& user) {
    for (const auto [prefix, isUser] : {std::pair{std::wstring_view(L"machine\\"), false}, std::pair{std::wstring_view(L"user\\"), true}}) {
        if (target.size() > prefix.size() && text::istartsWith(target, prefix)) {
            id = std::wstring(target.substr(prefix.size()));
            user = isUser;
            return true;
        }
    }
    return false;
}

namespace {
const char* stateKey(PolicyState s) {
    return s == PolicyState::Enabled ? "enabled" : s == PolicyState::Disabled ? "disabled" : "notConfigured";
}
} // namespace

std::wstring policyOperationJson(const PolicyOperation& op) {
    nlohmann::json doc;
    doc["name"] = utf8::fromWide(op.name);
    doc["state"] = stateKey(op.choice.state);
    nlohmann::json values = nlohmann::json::object();
    for (const auto& [id, v] : op.choice.values) {
        values[utf8::fromWide(id)] = utf8::fromWide(v);
    }
    doc["values"] = std::move(values);
    nlohmann::json writes = nlohmann::json::array();
    for (const auto& w : op.writes) {
        writes.push_back({{"key", utf8::fromWide(w.key)}, {"name", utf8::fromWide(w.name)}, {"value", utf8::fromWide(formatRegValue(w))}});
    }
    doc["writes"] = std::move(writes);
    return utf8::toWide(doc.dump());
}

Result<PolicyOperation> parsePolicyOperation(std::wstring_view json) {
    const auto doc = nlohmann::json::parse(utf8::fromWide(std::wstring(json)), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"policy operation is not JSON", std::wstring(json.substr(0, 80)));
    }
    PolicyOperation op;
    auto str = [](const nlohmann::json& j, const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_string() ? utf8::toWide(it->get<std::string>()) : std::wstring();
    };
    op.name = str(doc, "name");
    const std::wstring state = str(doc, "state");
    op.choice.state = state == L"enabled" ? PolicyState::Enabled : state == L"disabled" ? PolicyState::Disabled : PolicyState::NotConfigured;
    if (const auto it = doc.find("values"); it != doc.end() && it->is_object()) {
        for (const auto& [k, v] : it->items()) {
            if (v.is_string()) {
                op.choice.values[utf8::toWide(k)] = utf8::toWide(v.get<std::string>());
            }
        }
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

// ---- Registry.pol ----------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kPolSignature = 0x67655250; // "PReg"
constexpr std::uint32_t kPolVersion = 1;
constexpr std::wstring_view kDelPrefix = L"**del.";

std::uint32_t readU32(std::string_view bytes, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]);
    }
    return v;
}

void putU32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void putChar(std::string& out, wchar_t c) {
    out.push_back(static_cast<char>(c & 0xFF));
    out.push_back(static_cast<char>((c >> 8) & 0xFF));
}

void putText(std::string& out, std::wstring_view text) {
    for (const wchar_t c : text) {
        putChar(out, c);
    }
    putChar(out, L'\0');
}

} // namespace

Result<std::vector<PolEntry>> parsePol(std::string_view bytes) {
    std::vector<PolEntry> entries;
    if (bytes.empty()) {
        return entries;
    }
    if (bytes.size() < 8 || readU32(bytes, 0) != kPolSignature || readU32(bytes, 4) != kPolVersion) {
        return fail(ErrorCode::ParseError, L"not a Registry.pol file (PReg v1)", L"");
    }
    std::size_t at = 8;
    auto ch = [&]() -> std::optional<wchar_t> {
        if (at + 2 > bytes.size()) {
            return std::nullopt;
        }
        const auto c = static_cast<wchar_t>(static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1]) << 8));
        at += 2;
        return c;
    };
    auto textUntilNul = [&]() -> std::optional<std::wstring> {
        std::wstring s;
        while (auto c = ch()) {
            if (*c == L'\0') {
                return s;
            }
            s.push_back(*c);
        }
        return std::nullopt;
    };
    auto expect = [&](wchar_t want) { return ch() == want; };
    while (at < bytes.size()) {
        PolEntry e;
        if (!expect(L'[')) {
            return fail(ErrorCode::ParseError, L"Registry.pol: '[' expected", std::to_wstring(at));
        }
        auto key = textUntilNul();
        if (!key || !expect(L';')) {
            return fail(ErrorCode::ParseError, L"Registry.pol: bad key", std::to_wstring(at));
        }
        auto name = textUntilNul();
        if (!name || !expect(L';') || at + 4 > bytes.size()) {
            return fail(ErrorCode::ParseError, L"Registry.pol: bad value name", std::to_wstring(at));
        }
        e.key = std::move(*key);
        e.valueName = std::move(*name);
        e.type = readU32(bytes, at);
        at += 4;
        if (!expect(L';') || at + 4 > bytes.size()) {
            return fail(ErrorCode::ParseError, L"Registry.pol: bad type", std::to_wstring(at));
        }
        const std::uint32_t size = readU32(bytes, at);
        at += 4;
        if (!expect(L';') || at + size > bytes.size()) {
            return fail(ErrorCode::ParseError, L"Registry.pol: bad size", std::to_wstring(at));
        }
        e.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
        if (!expect(L']')) {
            return fail(ErrorCode::ParseError, L"Registry.pol: ']' expected", std::to_wstring(at));
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

std::string formatPol(const std::vector<PolEntry>& entries) {
    std::string out;
    putU32(out, kPolSignature);
    putU32(out, kPolVersion);
    for (const auto& e : entries) {
        putChar(out, L'[');
        putText(out, e.key);
        putChar(out, L';');
        putText(out, e.valueName);
        putChar(out, L';');
        putU32(out, e.type);
        putChar(out, L';');
        putU32(out, static_cast<std::uint32_t>(e.data.size()));
        putChar(out, L';');
        out.append(reinterpret_cast<const char*>(e.data.data()), e.data.size());
        putChar(out, L']');
    }
    return out;
}

void mergeIntoPol(std::vector<PolEntry>& entries, const RegistryWrite& write) {
    if (write.kind != RegistryWrite::Kind::Set && write.kind != RegistryWrite::Kind::DeleteValue) {
        return;
    }
    std::wstring key = write.key;
    for (const std::wstring_view root : {L"HKLM\\", L"HKCU\\"}) {
        if (text::istartsWith(key, root)) {
            key.erase(0, root.size());
        }
    }
    const std::wstring name = write.name;
    const std::wstring deleted = std::wstring(kDelPrefix) + name;
    std::erase_if(entries, [&](const PolEntry& e) {
        return text::iequals(e.key, key) && (text::iequals(e.valueName, name) || text::iequals(e.valueName, deleted));
    });
    if (write.kind == RegistryWrite::Kind::Set) {
        entries.push_back({key, name, write.type, write.data});
    } else {
        entries.push_back({key, deleted, REG_SZ, wide(L" ")});
    }
}

Result<void> recordInGroupPolicy(const std::filesystem::path& mountDir, std::span<const RegistryWrite> writes) {
    const auto root = mountDir / L"Windows" / L"System32" / L"GroupPolicy";
    bool machine = false;
    bool user = false;
    for (const bool side : {false, true}) {
        std::vector<const RegistryWrite*> mine;
        for (const auto& w : writes) {
            if (text::istartsWith(w.key, side ? L"HKCU\\" : L"HKLM\\")) {
                mine.push_back(&w);
            }
        }
        if (mine.empty()) {
            continue;
        }
        const auto file = root / (side ? L"User" : L"Machine") / L"Registry.pol";
        std::vector<PolEntry> entries;
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            auto bytes = readFileBytes(file);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            auto parsed = parsePol(*bytes);
            if (!parsed) {
                return fail(ErrorCode::ParseError, parsed.error().message, file.wstring());
            }
            entries = std::move(*parsed);
        }
        for (const RegistryWrite* w : mine) {
            mergeIntoPol(entries, *w);
        }
        std::filesystem::create_directories(file.parent_path(), ec);
        if (auto r = writeFileAtomic(file, formatPol(entries)); !r) {
            return r;
        }
        (side ? user : machine) = true;
    }
    if (!machine && !user) {
        return {};
    }
    // gpt.ini: Version = user << 16 | machine, and the registry policies' client-side extension in each
    // side's list (kept first: the lists are sorted by GUID and this one is the lowest in practice).
    // Lines of other tools (scripts, …) are kept.
    const auto ini = root / L"gpt.ini";
    std::vector<std::string> lines;
    if (auto bytes = readFileBytes(ini)) {
        std::size_t at = 0;
        while (at < bytes->size()) {
            const std::size_t end = std::min(bytes->find('\n', at), bytes->size());
            std::string line = bytes->substr(at, end - at);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty()) {
                lines.push_back(std::move(line));
            }
            at = end + 1;
        }
    }
    auto find = [&](std::string_view name) {
        return std::ranges::find_if(lines, [&](const std::string& l) {
            return l.size() > name.size() && l.compare(0, name.size(), name) == 0 && l[name.size()] == '=';
        });
    };
    if (std::ranges::none_of(lines, [](const std::string& l) { return l == "[General]"; })) {
        lines.insert(lines.begin(), "[General]");
    }
    std::uint32_t version = 0;
    if (const auto it = find("Version"); it != lines.end()) {
        std::from_chars(it->data() + 8, it->data() + it->size(), version);
        lines.erase(it);
    }
    const std::uint32_t machineVersion = (version & 0xFFFF) + (machine ? 1 : 0);
    const std::uint32_t userVersion = (version >> 16) + (user ? 1 : 0);
    auto extension = [&](std::string_view name, std::string_view pair) {
        const auto it = find(name);
        if (it == lines.end()) {
            lines.push_back(std::string(name) + "=" + std::string(pair));
        } else if (it->find("35378EAC-683F-11D2-A89A-00C04FBBCFA2") == std::string::npos) {
            it->insert(name.size() + 1, pair);
        }
    };
    if (machine) {
        extension("gPCMachineExtensionNames", "[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}{D02B1F72-3407-48AE-BA88-E8213C6761F1}]");
    }
    if (user) {
        extension("gPCUserExtensionNames", "[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}{D02B1F73-3407-48AE-BA88-E8213C6761F1}]");
    }
    if (find("gPCFunctionalityVersion") == lines.end()) {
        lines.insert(lines.begin() + 1, "gPCFunctionalityVersion=2");
    }
    lines.push_back("Version=" + std::to_string((userVersion << 16) | (machineVersion & 0xFFFF)));
    std::string out;
    for (const auto& line : lines) {
        out += line + "\r\n";
    }
    return writeFileAtomic(ini, out);
}

} // namespace wl::core::policy
