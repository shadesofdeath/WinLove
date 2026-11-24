#include "app/controllers/HostsController.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <unordered_set>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

HostsController::HostsController(AppState& state, std::string_view catalogJson)
    : m_state(state), m_lists(parseCatalog(catalogJson)) {}

std::vector<HostsList> HostsController::parseCatalog(std::string_view json) {
    std::vector<HostsList> lists;
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.hosts") {
        return lists;
    }
    for (const auto& l : doc.value("lists", nlohmann::json::array())) {
        HostsList list;
        list.id = utf8::toWide(l.value("id", std::string{}));
        list.name = {utf8::toWide(l.value("tr", std::string{})), utf8::toWide(l.value("en", std::string{}))};
        list.description = {utf8::toWide(l.value("desc_tr", std::string{})), utf8::toWide(l.value("desc_en", std::string{}))};
        const std::string risk = l.value("risk", std::string{"medium"});
        list.risk = risk == "low" ? Risk::Low : risk == "high" ? Risk::High : Risk::Medium;
        list.recommended = l.value("recommended", false);
        std::wstring text;
        for (const auto& e : l.value("entries", nlohmann::json::array())) {
            if (e.is_string()) {
                text += L"0.0.0.0 " + utf8::toWide(e.get<std::string>()) + L"\n";
            }
        }
        list.entries = core::parseHosts(text);
        if (!core::validHostsSection(list.id) || list.id == kCustom || list.entries.empty()) {
            log::warn("app", L"hosts catalog: skipped " + list.id);
            continue;
        }
        lists.push_back(std::move(list));
    }
    return lists;
}

const Operation* HostsController::queuedSection(std::wstring_view id) const {
    return m_state.changes().find(OpKind::SetHosts, id);
}

bool HostsController::inImageNow(const HostsList& list) const {
    return m_state.imageHas(Operation{OpKind::SetHosts, list.id, list.text()});
}

bool HostsController::onNow(const HostsList& list) const {
    if (const auto* op = queuedSection(list.id)) {
        return core::formatHostEntries(core::parseHosts(op->value)) == list.text();
    }
    return inImageNow(list);
}

const HostsController::Derived& HostsController::derived() const {
    if (m_derived.revision == m_state.revision() && m_derived.queueVersion == m_state.changes().version()) {
        return m_derived;
    }
    Derived d;
    d.revision = m_state.revision();
    d.queueVersion = m_state.changes().version();
    if (const auto* op = queuedSection(kCustom)) {
        d.custom = core::parseHosts(op->value);
    } else if (const auto& values = m_state.imageValues()) {
        if (const auto it = values->hostsSections.find(kCustom); it != values->hostsSections.end()) {
            d.custom = core::parseHosts(it->second);
        }
    }
    d.total = d.custom.size();
    for (const auto& list : m_lists) {
        d.inImage.push_back(inImageNow(list));
        d.on.push_back(onNow(list));
        if (d.on.back()) {
            d.total += list.entries.size();
        }
    }
    m_derived = std::move(d);
    return m_derived;
}

bool HostsController::inImage(const HostsList& list) const {
    if (!m_lists.empty() && &list >= m_lists.data() && &list < m_lists.data() + m_lists.size()) {
        return derived().inImage[static_cast<std::size_t>(&list - m_lists.data())] != 0;
    }
    return inImageNow(list);
}

bool HostsController::on(const HostsList& list) const {
    if (!m_lists.empty() && &list >= m_lists.data() && &list < m_lists.data() + m_lists.size()) {
        return derived().on[static_cast<std::size_t>(&list - m_lists.data())] != 0;
    }
    return onNow(list);
}

void HostsController::setSection(std::wstring_view id, std::wstring entries, Risk risk) {
    Operation op{OpKind::SetHosts, std::wstring(id), std::move(entries)};
    op.risk = risk;
    // The image's own state needs nothing queued.
    const bool imageSame = m_state.imageHas(op);
    const auto& values = m_state.imageValues();
    const bool imageNone = !values || values->hostsSections.find(std::wstring(id)) == values->hostsSections.end();
    if (imageSame || (op.value.empty() && imageNone)) {
        m_state.unqueue(OpKind::SetHosts, id);
        return;
    }
    m_state.queue(std::move(op));
}

void HostsController::toggle(const HostsList& list) {
    setSection(list.id, on(list) ? std::wstring() : list.text(), list.risk);
}

int HostsController::applyRecommended() {
    int n = 0;
    for (const auto& list : m_lists) {
        if (list.recommended && !on(list)) {
            toggle(list);
            ++n;
        }
    }
    return n;
}

const std::vector<core::HostEntry>& HostsController::customEntries() const {
    return derived().custom;
}

bool HostsController::customInImage() const {
    const auto& values = m_state.imageValues();
    return values && values->hostsSections.contains(kCustom);
}

std::size_t HostsController::importText(std::wstring_view text) {
    auto entries = customEntries(); // a copy: it grows
    // By name in a set: comparing each new line with every line so far made a block list of
    // 100,000 lines some 5 billion comparisons, on the UI thread.
    std::unordered_set<std::wstring> names;
    names.reserve(entries.size());
    for (const auto& e : entries) {
        names.insert(e.name);
    }
    std::size_t added = 0;
    for (auto& e : core::parseHosts(text)) {
        if (names.insert(e.name).second) {
            entries.push_back(std::move(e));
            ++added;
        }
    }
    if (added > 0) {
        setSection(kCustom, core::formatHostEntries(entries), Risk::Medium);
    }
    return added;
}

void HostsController::clearCustom() {
    setSection(kCustom, std::wstring(), Risk::Low);
}

int HostsController::changedCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::SetHosts));
}

std::size_t HostsController::totalEntries() const {
    return derived().total;
}

} // namespace wl::app
