#include "core/ops/ChangeSet.h"

#include "base/Text.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <utility>

namespace wl::core::ops {

namespace {

constexpr std::array<std::pair<OpKind, const char*>, 31> kKinds = {{
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
    {OpKind::SetPostSetup, "setPostSetup"},
    {OpKind::RemoveComponent, "removeComponent"},
    {OpKind::CleanupImage, "cleanupImage"},
    {OpKind::SetEdition, "setEdition"},
    {OpKind::WriteFile, "writeFile"},
    {OpKind::CopyFile, "copyFile"},
    {OpKind::SetTaskState, "setTaskState"},
    {OpKind::SetHosts, "setHosts"},
    {OpKind::SetDns, "setDns"},
    {OpKind::CopyTree, "copyTree"},
    {OpKind::RemoveDriver, "removeDriver"},
    {OpKind::AddAppx, "addAppx"},
    {OpKind::SetDefaultApps, "setDefaultApps"},
    {OpKind::SetIntl, "setIntl"},
    {OpKind::SetPicture, "setPicture"},
    {OpKind::AddFont, "addFont"},
    {OpKind::PatchIcons, "patchIcons"},
    {OpKind::ShrinkStore, "shrinkStore"},
    {OpKind::CreateTask, "createTask"},
    {OpKind::SetPolicy, "setPolicy"},
    {OpKind::SetContextMenu, "setContextMenu"},
}};

constexpr std::array<const char*, 3> kRisks = {"low", "medium", "high"};
constexpr std::size_t kUndoDepth = 200;
// The history keeps whole earlier queues. Most are small (a 900-step preset is ~0.3 MB), but with
// the welcome queued every state carries its script (~0.5 MB as UTF-16): 200 of them were 100 MB
// and more. Past this the oldest states go first; the newest always stays.
constexpr std::size_t kUndoBytes = 64u * 1024 * 1024;

std::size_t bytesOf(const std::vector<Operation>& ops) {
    std::size_t bytes = ops.capacity() * sizeof(Operation);
    for (const auto& op : ops) {
        bytes += (op.target.capacity() + op.value.capacity()) * sizeof(wchar_t);
    }
    return bytes;
}

std::wstring slotKey(OpKind kind, std::wstring_view target) {
    std::wstring key;
    key.reserve(target.size() + 1);
    key.push_back(static_cast<wchar_t>(L'A' + static_cast<int>(kind)));
    for (const wchar_t c : target) {
        key.push_back(c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c < 128 ? c : static_cast<wchar_t>(1));
    }
    return key;
}

} // namespace

bool Operation::sameSlot(const Operation& other) const noexcept {
    return kind == other.kind && text::iequals(target, other.target);
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

void ChangeSet::endGroup() noexcept {
    if (m_group > 0 && --m_group == 0) {
        m_groupSaved = false;
    }
}

void ChangeSet::snapshot() {
    if (m_group > 0) {
        if (m_groupSaved) {
            ++m_version; // the state before the group is already in the history
            return;
        }
        m_groupSaved = true;
    }
    m_undo.push_back(m_ops);
    m_undoBytes += bytesOf(m_undo.back());
    trimHistory();
    m_redo.clear();
    ++m_version;
}

void ChangeSet::trimHistory() {
    while (m_undo.size() > kUndoDepth || (m_undoBytes > kUndoBytes && m_undo.size() > 1)) {
        m_undoBytes -= std::min(m_undoBytes, bytesOf(m_undo.front()));
        m_undo.pop_front();
    }
}

ChangeSet ChangeSet::withoutHistory() const {
    ChangeSet copy;
    copy.m_ops = m_ops;
    copy.m_index = m_index;
    copy.m_version = m_version;
    return copy;
}

void ChangeSet::reindex() {
    m_index.clear();
    m_index.reserve(m_ops.size());
    for (std::size_t i = 0; i < m_ops.size(); ++i) {
        m_index.emplace(slotKey(m_ops[i].kind, m_ops[i].target), i);
    }
}

std::size_t ChangeSet::indexOf(OpKind kind, std::wstring_view target, const std::vector<bool>* skip) const {
    std::size_t first = kNone;
    const auto [begin, end] = m_index.equal_range(slotKey(kind, target));
    for (auto it = begin; it != end; ++it) {
        const std::size_t i = it->second;
        if (i < first && !(skip && (*skip)[i]) && m_ops[i].kind == kind && text::iequals(m_ops[i].target, target)) {
            first = i;
        }
    }
    return first;
}

void ChangeSet::add(Operation op) {
    snapshot();
    addOne(std::move(op));
}

void ChangeSet::addAll(std::vector<Operation> ops) {
    if (ops.empty()) {
        return;
    }
    snapshot();
    for (auto& op : ops) {
        addOne(std::move(op));
    }
}

std::size_t ChangeSet::removeAll(const std::vector<std::pair<OpKind, std::wstring>>& slots) {
    // Marked first, taken out in one pass: one erase per slot moved the rest of the queue each time.
    std::vector<bool> gone(m_ops.size(), false);
    std::size_t removed = 0;
    for (const auto& [kind, target] : slots) {
        if (const std::size_t i = indexOf(kind, target, &gone); i != kNone) {
            gone[i] = true;
            ++removed;
        }
    }
    if (removed == 0) {
        return 0;
    }
    snapshot();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_ops.size(); ++i) {
        if (!gone[i]) {
            if (kept != i) {
                m_ops[kept] = std::move(m_ops[i]);
            }
            ++kept;
        }
    }
    m_ops.resize(kept, Operation{OpKind::RemovePackage, {}, {}});
    reindex();
    return removed;
}

void ChangeSet::addOne(Operation op) {
    // Enable/Disable of one feature are inverses: queuing both means "leave it as it is".
    if (op.kind == OpKind::DisableFeature || op.kind == OpKind::EnableFeature) {
        const OpKind other = op.kind == OpKind::DisableFeature ? OpKind::EnableFeature : OpKind::DisableFeature;
        if (const std::size_t opposite = indexOf(other, op.target); opposite != kNone) {
            m_ops.erase(m_ops.begin() + static_cast<std::ptrdiff_t>(opposite));
            reindex();
            return;
        }
    }
    if (const std::size_t same = indexOf(op.kind, op.target); same != kNone) {
        m_ops[same] = std::move(op);
    } else {
        m_index.emplace(slotKey(op.kind, op.target), m_ops.size());
        m_ops.push_back(std::move(op));
    }
}

bool ChangeSet::remove(OpKind kind, std::wstring_view target) {
    const std::size_t i = indexOf(kind, target);
    if (i == kNone) {
        return false;
    }
    snapshot();
    m_ops.erase(m_ops.begin() + static_cast<std::ptrdiff_t>(i));
    reindex();
    return true;
}

void ChangeSet::clear() {
    if (!m_ops.empty()) {
        snapshot();
        m_ops.clear();
        m_index.clear();
    }
}

const Operation* ChangeSet::find(OpKind kind, std::wstring_view target) const {
    const std::size_t i = indexOf(kind, target);
    return i == kNone ? nullptr : &m_ops[i];
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
    m_undoBytes -= std::min(m_undoBytes, bytesOf(m_ops));
    reindex();
    ++m_version;
    return true;
}

bool ChangeSet::redo() {
    if (m_redo.empty()) {
        return false;
    }
    m_undo.push_back(std::move(m_ops));
    m_undoBytes += bytesOf(m_undo.back());
    m_ops = std::move(m_redo.back());
    m_redo.pop_back();
    reindex();
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
    set.reindex();
    return set;
}

} // namespace wl::core::ops
