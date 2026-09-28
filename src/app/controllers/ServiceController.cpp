#include "app/controllers/ServiceController.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>

namespace wl::app {

using core::ServiceEntry;
using core::StartType;
using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

namespace {

std::wstring lowered(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

Risk riskFrom(const std::string& text) {
    return text == "low" ? Risk::Low : text == "high" ? Risk::High : Risk::Medium;
}

} // namespace

ServiceController::ServiceController(AppState& state, std::string_view catalogJson,
                                     std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_catalog(parseCatalog(catalogJson)), m_post(std::move(postToUi)) {}

ServiceController::~ServiceController() {
    *m_alive = false;
}

std::map<std::wstring, ServiceNote> ServiceController::parseCatalog(std::string_view json) {
    std::map<std::wstring, ServiceNote> catalog;
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "winlove.catalog.services") {
        return catalog;
    }
    for (const auto& s : doc.value("services", nlohmann::json::array())) {
        catalog[lowered(utf8::toWide(s.value("name", std::string{})))] =
            ServiceNote{riskFrom(s.value("risk", std::string{"medium"})), utf8::toWide(s.value("notes_tr", std::string{})),
                        utf8::toWide(s.value("notes_en", std::string{}))};
    }
    return catalog;
}

void ServiceController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.serviceList();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::ServiceList::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Loading, mountDir, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    // Engine thread: the hive load must not race a DISM unmount of the same folder.
    m_state.engine().run<std::vector<ServiceEntry>>(
        [mountDir](const core::TaskContext&) { return core::readServices(mountDir); },
        [this, post, alive, mountDir](Result<std::vector<ServiceEntry>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& mounted = m_state.mounted();
                if (!mounted || mounted->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setServiceList(
                        AppState::ServiceList{AppState::ServiceList::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                m_state.setServiceList(
                    AppState::ServiceList{AppState::ServiceList::Status::Ready, mountDir, std::move(*result), {}});
            });
        },
        {});
}

Operation ServiceController::operationFor(const ServiceEntry& service, StartType start, Risk risk) {
    Operation op{OpKind::SetServiceStart, service.name, core::startTypeKey(start)};
    // Turning a service off is the risky direction; enabling one back is always low.
    op.risk = start == StartType::Disabled ? risk : std::min(risk, Risk::Medium);
    if (service.start == StartType::Boot || service.start == StartType::System) {
        op.risk = Risk::High;
    }
    return op;
}

StartType ServiceController::target(const ServiceEntry& service) const {
    if (const auto* op = m_state.changes().find(OpKind::SetServiceStart, service.name)) {
        if (const auto start = core::startTypeFromKey(op->value)) {
            return *start;
        }
    }
    return service.start;
}

bool ServiceController::changed(const ServiceEntry& service) const {
    return m_state.changes().find(OpKind::SetServiceStart, service.name) != nullptr;
}

void ServiceController::set(const ServiceEntry& service, StartType start) {
    if (start == service.start) {
        m_state.unqueue(OpKind::SetServiceStart, service.name);
        return;
    }
    m_state.queue(operationFor(service, start, risk(service)));
}

void ServiceController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::SetServiceStart; });
}

std::size_t ServiceController::queuedCount() const {
    return m_state.changes().count(OpKind::SetServiceStart);
}

Risk ServiceController::risk(const ServiceEntry& service) const {
    if (service.start == StartType::Boot || service.start == StartType::System) {
        return Risk::High;
    }
    const auto it = m_catalog.find(lowered(service.name));
    return it != m_catalog.end() ? it->second.risk : Risk::Medium;
}

std::wstring ServiceController::notes(const ServiceEntry& service, Language language) const {
    const auto it = m_catalog.find(lowered(service.name));
    if (it == m_catalog.end()) {
        return {};
    }
    return language == Language::Turkish ? it->second.notesTr : it->second.notesEn;
}

std::vector<std::wstring> ServiceController::activeDependents(const ServiceEntry& service) const {
    std::vector<std::wstring> active;
    const auto& list = m_state.serviceList();
    if (!list) {
        return active;
    }
    for (const auto& name : core::dependentsOf(list->items, service.name)) {
        const auto it = std::ranges::find(list->items, name, &ServiceEntry::name);
        if (it != list->items.end() && target(*it) != StartType::Disabled) {
            active.push_back(it->displayName);
        }
    }
    return active;
}

} // namespace wl::app
