#include "app/controllers/AppsController.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"

#include <windows.h>

#include <algorithm>
#include <format>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

namespace {
constexpr wchar_t kAssociations[] = L"associations";
constexpr std::wstring_view kBrowserIds[] = {L"http", L"https", L".htm", L".html"};

bool sameId(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}
} // namespace

AppsController::AppsController(AppState& state, Events events)
    : m_state(state), m_events(std::move(events)), m_background(std::make_unique<core::TaskRunner>()) {}

AppsController::~AppsController() {
    *m_alive = false;
    m_background.reset();
}

std::wstring AppsController::imageArchitecture() const {
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (mounted && source) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted->index) {
                return core::architectureName(image.architecture);
            }
        }
    }
    return L"x64";
}

Operation AppsController::operationFor(const core::AppxInstall& install) {
    Operation op{OpKind::AddAppx, install.package.wstring(), utf8::toWide(core::appxInstallToJson(install))};
    // Sideloaded code for every user; a framework alone is plumbing.
    op.risk = install.framework ? Risk::Low : Risk::Medium;
    std::int64_t bytes = 0;
    std::error_code ec;
    bytes += static_cast<std::int64_t>(std::filesystem::file_size(install.package, ec));
    for (const auto& d : install.dependencies) {
        bytes += static_cast<std::int64_t>(std::filesystem::file_size(d, ec));
    }
    op.sizeDelta = bytes * 2; // a package unpacks to about twice its size
    return op;
}

void AppsController::addPackages(std::vector<std::filesystem::path> files) {
    std::erase_if(files, [](const std::filesystem::path& f) { return !core::isAppxFile(f); });
    if (files.empty()) {
        return;
    }
    const std::wstring arch = imageArchitecture();
    // The plan is for this mount (its architecture): queued only if it is still the one mounted.
    const std::filesystem::path mountDir = m_state.mounted() ? m_state.mounted()->mountDir : std::filesystem::path();
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    struct Planned {
        std::vector<core::AppxInstall> installs;
        std::vector<Error> errors;
    };
    auto planned = std::make_shared<Planned>();
    m_state.reader().run<bool>(
        [files, arch, planned](const core::TaskContext&) -> Result<bool> {
            for (const auto& f : files) {
                auto install = core::planAppxInstall(f, arch);
                if (install) {
                    planned->installs.push_back(std::move(*install));
                } else {
                    planned->errors.push_back(install.error());
                }
            }
            return true;
        },
        [this, post, alive, planned, mountDir](Result<bool>) {
            post([this, alive, planned, mountDir] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                std::vector<Operation> ops;
                for (const auto& install : planned->installs) {
                    ops.push_back(operationFor(install));
                }
                // What the toast reports is what was actually queued: nothing when the image was
                // unmounted / replaced meanwhile or the queue is locked (Uygula running).
                int n = 0;
                if (m_state.mounted() && m_state.mounted()->mountDir == mountDir && !m_state.queueLocked()) {
                    n = static_cast<int>(ops.size());
                    m_state.queueMany(std::move(ops));
                }
                for (const auto& e : planned->errors) {
                    log::warn("app", describe(e));
                }
                if (m_events.packagesAdded) {
                    m_events.packagesAdded(n, std::move(planned->errors));
                }
            });
        });
}

std::vector<core::AppxInstall> AppsController::queuedApps() const {
    std::vector<core::AppxInstall> apps;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::AddAppx) {
            if (auto install = core::appxInstallFromJson(op.target, utf8::fromWide(op.value))) {
                apps.push_back(std::move(*install));
            }
        }
    }
    return apps;
}

int AppsController::appCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::AddAppx));
}

std::vector<core::AppAssociation> AppsController::associations() const {
    if (const auto* op = m_state.changes().find(OpKind::SetDefaultApps, kAssociations)) {
        if (auto list = core::parseAssociations(utf8::fromWide(op->value))) {
            return *list;
        }
    }
    return {};
}

void AppsController::setAssociations(const std::vector<core::AppAssociation>& list) {
    if (list.empty()) {
        m_state.unqueue(OpKind::SetDefaultApps, kAssociations);
        return;
    }
    Operation op{OpKind::SetDefaultApps, kAssociations, utf8::toWide(core::associationsXml(list))};
    op.risk = Risk::Low;
    m_state.queue(std::move(op));
}

void AppsController::mergeAssociations(const std::vector<core::AppAssociation>& list) {
    auto current = associations();
    for (const auto& a : list) {
        std::erase_if(current, [&](const core::AppAssociation& b) { return sameId(a.identifier, b.identifier); });
        current.push_back(a);
    }
    std::ranges::sort(current, [](const core::AppAssociation& a, const core::AppAssociation& b) {
        return _wcsicmp(a.identifier.c_str(), b.identifier.c_str()) < 0;
    });
    setAssociations(current);
}

void AppsController::removeAssociation(std::wstring_view identifier) {
    auto current = associations();
    std::erase_if(current, [&](const core::AppAssociation& a) { return sameId(a.identifier, identifier); });
    setAssociations(current);
}

Result<int> AppsController::importAssociationsFile(const std::filesystem::path& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto list = core::parseAssociations(*bytes);
    if (!list) {
        return std::unexpected(list.error());
    }
    mergeAssociations(*list);
    return static_cast<int>(list->size());
}

void AppsController::importHostAssociations() {
    if (m_importingHost) {
        return;
    }
    m_importingHost = true;
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_background->run<std::vector<core::AppAssociation>>(
        [](const core::TaskContext&) -> Result<std::vector<core::AppAssociation>> {
            wchar_t temp[MAX_PATH];
            GetTempPathW(MAX_PATH, temp);
            auto xml = core::exportHostAssociations(std::filesystem::path(temp) / L"WinLove");
            if (!xml) {
                return std::unexpected(xml.error());
            }
            return core::parseAssociations(*xml);
        },
        [this, post, alive](Result<std::vector<core::AppAssociation>> result) {
            post([this, alive, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_importingHost = false;
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (m_events.failed) {
                        m_events.failed(result.error());
                    }
                    return;
                }
                int imported = 0;
                if (m_state.mounted() && !m_state.queueLocked()) {
                    mergeAssociations(*result);
                    imported = static_cast<int>(result->size());
                }
                if (m_events.hostImported) {
                    m_events.hostImported(imported);
                }
            });
        });
}

const std::vector<AppsController::Browser>& AppsController::browsers() {
    static const std::vector<Browser> kBrowsers = {
        {Str::AppsBrowserChrome, L"ChromeHTML", L"Google Chrome"},
        {Str::AppsBrowserFirefox, L"FirefoxURL-308046B0AF4A39CB", L"Firefox"},
        {Str::AppsBrowserBrave, L"BraveHTML", L"Brave"},
        {Str::AppsBrowserEdge, L"MSEdgeHTM", L"Microsoft Edge"},
    };
    return kBrowsers;
}

int AppsController::browser() const {
    for (const auto& a : associations()) {
        if (sameId(a.identifier, L"http")) {
            const auto& all = browsers();
            for (std::size_t i = 0; i < all.size(); ++i) {
                if (sameId(a.progId, all[i].progId)) {
                    return static_cast<int>(i);
                }
            }
        }
    }
    return -1;
}

void AppsController::setBrowser(int index) {
    auto current = associations();
    std::erase_if(current, [](const core::AppAssociation& a) {
        return std::ranges::any_of(kBrowserIds, [&](std::wstring_view id) { return sameId(a.identifier, id); });
    });
    if (index >= 0 && index < static_cast<int>(browsers().size())) {
        const auto& b = browsers()[static_cast<std::size_t>(index)];
        for (const auto id : kBrowserIds) {
            // Firefox registers its pages under another ProgId than its links.
            std::wstring progId = b.progId;
            if (b.progId == L"FirefoxURL-308046B0AF4A39CB" && id.starts_with(L".")) {
                progId = L"FirefoxHTML-308046B0AF4A39CB";
            }
            current.push_back({std::wstring(id), progId, b.name});
        }
    }
    setAssociations(current);
}

} // namespace wl::app
