#include "app/controllers/RemovalGaps.h"

#include "app/controllers/ImageSettingsController.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"

#include <algorithm>

namespace wl::app {

using core::AppAssociation;
using core::ops::ChangeSet;
using core::ops::OpKind;
using core::ops::Operation;

namespace {

constexpr wchar_t kAssociations[] = L"associations"; // AppsController's SetDefaultApps slot

bool sameId(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

// The operations of a catalog setting's option; empty (and logged) when the catalog lacks it.
std::vector<Operation> settingOps(const ImageSettingsCatalog& catalog, std::string_view id, std::string_view option) {
    for (const auto& setting : catalog.settings()) {
        if (setting.id != id) {
            continue;
        }
        for (std::size_t i = 0; i < setting.options.size(); ++i) {
            if (setting.options[i].id == option) {
                return ImageSettingsController::operationsFor(setting, static_cast<int>(i));
            }
        }
    }
    log::warn("app", utf8::toWide(std::string("removal gap: no setting ") + std::string(id) + "/" + std::string(option)));
    return {};
}

std::vector<AppAssociation> queuedAssociations(const ChangeSet& changes) {
    if (const auto* op = changes.find(OpKind::SetDefaultApps, kAssociations)) {
        if (auto list = core::parseAssociations(utf8::fromWide(op->value))) {
            return *list;
        }
    }
    return {};
}

void setAssociations(ChangeSet& changes, std::vector<AppAssociation> list) {
    if (list.empty()) {
        changes.remove(OpKind::SetDefaultApps, kAssociations);
        return;
    }
    std::ranges::sort(list, [](const AppAssociation& a, const AppAssociation& b) {
        return _wcsicmp(a.identifier.c_str(), b.identifier.c_str()) < 0;
    });
    Operation op{OpKind::SetDefaultApps, kAssociations, utf8::toWide(core::associationsXml(list))};
    op.risk = core::ops::Risk::Low;
    changes.add(std::move(op));
}

} // namespace

RemovalGaps::RemovalGaps(const ImageSettingsCatalog& settings, std::function<std::filesystem::path()> mountDir)
    : m_mountDir(std::move(mountDir)) {
    {
        Fix fix{L"Microsoft.ScreenSketch", Str::ComponentsGapPrintScreen, settingOps(settings, "print-screen", "copy"), {}, {}};
        m_fixes.push_back(std::move(fix));
    }
    {
        const std::wstring app = L"Windows Photo Viewer";
        Fix fix{L"Microsoft.Windows.Photos", Str::ComponentsGapPhotoViewer, settingOps(settings, "photo-viewer", "on"),
                {{L".bmp", L"PhotoViewer.FileAssoc.Bitmap", app},
                 {L".dib", L"PhotoViewer.FileAssoc.Bitmap", app},
                 {L".gif", L"PhotoViewer.FileAssoc.Gif", app},
                 {L".jfif", L"PhotoViewer.FileAssoc.Jpeg", app},
                 {L".jpe", L"PhotoViewer.FileAssoc.Jpeg", app},
                 {L".jpeg", L"PhotoViewer.FileAssoc.Jpeg", app},
                 {L".jpg", L"PhotoViewer.FileAssoc.Jpeg", app},
                 {L".png", L"PhotoViewer.FileAssoc.Png", app}},
                {}};
        m_fixes.push_back(std::move(fix));
    }
    {
        Fix fix{L"Microsoft.WindowsNotepad", Str::ComponentsGapNotepad, {}, {{L".txt", L"txtfile", L"Notepad"}},
                std::filesystem::path(L"Windows") / L"System32" / L"notepad.exe"};
        m_fixes.push_back(std::move(fix));
    }
}

std::wstring RemovalGaps::identityOf(std::wstring_view target) {
    return std::wstring(target.substr(0, std::min(target.find(L'_'), target.size())));
}

const RemovalGaps::Fix* RemovalGaps::fixOf(std::wstring_view identity) const {
    const auto it = std::ranges::find_if(m_fixes, [&](const Fix& f) { return sameId(f.app, identity); });
    return it != m_fixes.end() ? &*it : nullptr;
}

bool RemovalGaps::available(const Fix& fix) const {
    if (fix.ops.empty() && fix.associations.empty()) {
        return false;
    }
    if (fix.needs.empty()) {
        return true;
    }
    const std::filesystem::path root = m_mountDir ? m_mountDir() : std::filesystem::path();
    std::error_code ec;
    return !root.empty() && std::filesystem::exists(root / fix.needs, ec);
}

std::vector<std::wstring> RemovalGaps::watched(const ChangeSet& changes) const {
    std::vector<std::wstring> apps;
    if (changes.count(OpKind::RemoveAppx) == 0) {
        return apps;
    }
    for (const auto& op : changes.operations()) {
        if (op.kind != OpKind::RemoveAppx) {
            continue;
        }
        if (const Fix* fix = fixOf(identityOf(op.target));
            fix && std::ranges::none_of(apps, [&](const std::wstring& a) { return sameId(a, fix->app); })) {
            apps.push_back(fix->app);
        }
    }
    return apps;
}

void RemovalGaps::reconcile(ChangeSet& changes, const std::vector<std::wstring>& before) const {
    const std::vector<std::wstring> now = watched(changes);
    auto in = [](const std::vector<std::wstring>& list, const std::wstring& app) {
        return std::ranges::any_of(list, [&](const std::wstring& a) { return sameId(a, app); });
    };
    for (const auto& app : before) {
        if (!in(now, app)) {
            if (const Fix* fix = fixOf(app)) {
                removeFix(changes, *fix);
            }
        }
    }
    for (const auto& app : now) {
        if (!in(before, app)) {
            if (const Fix* fix = fixOf(app); fix && available(*fix)) {
                addFix(changes, *fix);
            }
        }
    }
}

void RemovalGaps::addFix(ChangeSet& changes, const Fix& fix) {
    for (const auto& op : fix.ops) {
        if (!changes.find(op.kind, op.target)) {
            changes.add(op);
        }
    }
    if (fix.associations.empty()) {
        return;
    }
    auto list = queuedAssociations(changes);
    bool added = false;
    for (const auto& a : fix.associations) {
        if (std::ranges::none_of(list, [&](const AppAssociation& b) { return sameId(a.identifier, b.identifier); })) {
            list.push_back(a);
            added = true;
        }
    }
    if (added) {
        setAssociations(changes, std::move(list));
    }
}

void RemovalGaps::removeFix(ChangeSet& changes, const Fix& fix) {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& op : fix.ops) {
        if (const auto* queued = changes.find(op.kind, op.target); queued && queued->value == op.value) {
            slots.emplace_back(op.kind, op.target);
        }
    }
    if (!slots.empty()) {
        changes.removeAll(slots);
    }
    if (fix.associations.empty()) {
        return;
    }
    auto list = queuedAssociations(changes);
    const std::size_t was = list.size();
    std::erase_if(list, [&](const AppAssociation& b) {
        return std::ranges::any_of(fix.associations, [&](const AppAssociation& a) {
            return sameId(a.identifier, b.identifier) && sameId(a.progId, b.progId);
        });
    });
    if (list.size() != was) {
        setAssociations(changes, std::move(list));
    }
}

std::vector<Str> RemovalGaps::fixesOf(std::wstring_view app) const {
    if (const Fix* fix = fixOf(identityOf(app)); fix && available(*fix)) {
        return {fix->what};
    }
    return {};
}

} // namespace wl::app
