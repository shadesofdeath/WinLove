#include "core/ops/ChangeSet.h"

#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <utility>

namespace wl::core::ops {

namespace {

constexpr std::array<std::pair<OpKind, const char*>, 10> kKinds = {{
    {OpKind::RemovePackage, "removePackage"},
    {OpKind::RemoveCapability, "removeCapability"},
    {OpKind::RemoveAppx, "removeAppx"},
    {OpKind::DisableFeature, "disableFeature"},
    {OpKind::EnableFeature, "enableFeature"},
    {OpKind::AddDriver, "addDriver"},
    {OpKind::AddPackage, "addPackage"},
    {OpKind::SetRegistryValue, "setRegistryValue"},
    {OpKind::SetServiceStart, "setServiceStart"},
    {OpKind::SetRegistryFirstLogon, "setRegistryFirstLogon"},
}};

constexpr std::array<const char*, 3> kRisks = {"low", "medium", "high"};
constexpr std::size_t kUndoDepth = 200;

// Enable/Disable of one feature are inverses: queuing both means "leave it as it is".
bool inverse(OpKind a, OpKind b) {
    return (a == OpKind::DisableFeature && b == OpKind::EnableFeature) ||
           (a == OpKind::EnableFeature && b == OpKind::DisableFeature);
}

bool sameTarget(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

} // namespace

bool Operation::sameSlot(const Operation& other) const noexcept {
    return kind == other.kind && sameTarget(target, other.target);
}

const char* opKindKey(OpKind kind) noexcept {
    for (const auto& [k, key] : kKinds) {
        if (k == kind) {
            return key;
        }
    }
    return "?";
}

Result<OpKind> opKindFromKey(std::string_view key) {
    for (const auto& [kind, name] : kKinds) {
        if (key == name) {
            return kind;
        }
    }
    return fail(ErrorCode::ParseError, L"unknown operation kind", utf8::toWide(key));
}

void ChangeSet::snapshot() {
    m_undo.push_back(m_ops);
    if (m_undo.size() > kUndoDepth) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    ++m_version;
}

void ChangeSet::add(Operation op) {
    snapshot();
    const auto opposite = std::ranges::find_if(m_ops, [&](const Operation& o) {
        return inverse(o.kind, op.kind) && sameTarget(o.target, op.target);
    });
    if (opposite != m_ops.end()) {
        m_ops.erase(opposite);
        return;
    }
    const auto same = std::ranges::find_if(m_ops, [&](const Operation& o) { return o.sameSlot(op); });
    if (same != m_ops.end()) {
        *same = std::move(op);
    } else {
        m_ops.push_back(std::move(op));
    }
}

bool ChangeSet::remove(OpKind kind, std::wstring_view target) {
    const auto it = std::ranges::find_if(m_ops, [&](const Operation& o) {
        return o.kind == kind && sameTarget(o.target, target);
    });
    if (it == m_ops.end()) {
        return false;
    }
    snapshot(); // copies m_ops into the undo stack; `it` stays valid
    m_ops.erase(it);
    return true;
}

void ChangeSet::clear() {
    if (!m_ops.empty()) {
        snapshot();
        m_ops.clear();
    }
}

const Operation* ChangeSet::find(OpKind kind, std::wstring_view target) const {
    const auto it = std::ranges::find_if(m_ops, [&](const Operation& o) {
        return o.kind == kind && sameTarget(o.target, target);
    });
    return it == m_ops.end() ? nullptr : &*it;
}

std::size_t ChangeSet::count(OpKind kind) const {
    return static_cast<std::size_t>(std::ranges::count_if(m_ops, [kind](const Operation& o) { return o.kind == kind; }));
}

std::int64_t ChangeSet::estimatedSizeDelta() const {
    std::int64_t total = 0;
    for (const auto& op : m_ops) {
        total += op.sizeDelta;
    }
    return total;
}

bool ChangeSet::undo() {
    if (m_undo.empty()) {
        return false;
    }
    m_redo.push_back(std::move(m_ops));
    m_ops = std::move(m_undo.back());
    m_undo.pop_back();
    ++m_version;
    return true;
}

bool ChangeSet::redo() {
    if (m_redo.empty()) {
        return false;
    }
    m_undo.push_back(std::move(m_ops));
    m_ops = std::move(m_redo.back());
    m_redo.pop_back();
    ++m_version;
    return true;
}

std::string ChangeSet::toJson() const {
    nlohmann::json ops = nlohmann::json::array();
    for (const auto& op : m_ops) {
        nlohmann::json entry{{"kind", opKindKey(op.kind)}, {"target", utf8::fromWide(op.target)},
                             {"risk", kRisks[static_cast<std::size_t>(op.risk)]}};
        if (!op.value.empty()) {
            entry["value"] = utf8::fromWide(op.value);
        }
        if (op.sizeDelta != 0) {
            entry["sizeDelta"] = op.sizeDelta;
        }
        ops.push_back(std::move(entry));
    }
    return nlohmann::json{{"format", "winlove.changeset"}, {"version", 1}, {"operations", ops}}.dump(2);
}

namespace {

// Throws nlohmann::json::exception on wrongly typed fields; fromJson turns that into ParseError.
Result<std::vector<Operation>> parseOperations(const nlohmann::json& doc) {
    if (doc.value("format", "") != "winlove.changeset") {
        return fail(ErrorCode::ParseError, L"not a WinLove change set");
    }
    if (doc.value("version", 0) != 1) {
        return fail(ErrorCode::Unsupported, L"unsupported change set version");
    }
    std::vector<Operation> ops;
    const auto list = doc.value("operations", nlohmann::json::array());
    if (!list.is_array()) {
        return fail(ErrorCode::ParseError, L"operations is not a list");
    }
    for (const auto& entry : list) {
        if (!entry.is_object()) {
            return fail(ErrorCode::ParseError, L"operation is not an object");
        }
        auto kind = opKindFromKey(entry.value("kind", ""));
        if (!kind) {
            return std::unexpected(kind.error());
        }
        Operation op{*kind, utf8::toWide(entry.value("target", "")), utf8::toWide(entry.value("value", ""))};
        const std::string risk = entry.value("risk", "low");
        op.risk = risk == "high" ? Risk::High : risk == "medium" ? Risk::Medium : Risk::Low;
        op.sizeDelta = entry.value("sizeDelta", std::int64_t{0});
        if (op.target.empty()) {
            return fail(ErrorCode::ParseError, L"operation without target");
        }
        ops.push_back(std::move(op));
    }
    return ops;
}

} // namespace

Result<ChangeSet> ChangeSet::fromJson(std::string_view text) {
    const auto doc = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a WinLove change set");
    }
    // value() throws type_error on a wrongly typed field ("version":"1", "target":5): a preset file
    // is user input, so any such exception is a parse error, never a crash.
    Result<std::vector<Operation>> ops = fail(ErrorCode::ParseError, L"malformed change set");
    try {
        ops = parseOperations(doc);
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed change set", utf8::toWide(e.what()));
    }
    if (!ops) {
        return std::unexpected(ops.error());
    }
    ChangeSet set;
    set.m_ops = std::move(*ops); // loaded as-is: not an undoable edit
    return set;
}

} // namespace wl::core::ops
