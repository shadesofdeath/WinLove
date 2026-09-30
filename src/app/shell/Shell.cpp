#include "app/shell/Shell.h"

#include "app/Format.h"
#include "app/pages/GalleryPage.h"
#include "app/Resources.h"
#include "app/pages/ApplyPage.h"
#include "app/pages/ComponentsPage.h"
#include "app/pages/DriversPage.h"
#include "app/pages/ServicesPage.h"
#include "app/pages/RegistryPage.h"
#include "app/pages/components/ComponentInspector.h"
#include "app/pages/FeaturesPage.h"
#include "app/pages/apply/RiskConfirm.h"
#include "app/pages/ImagesPage.h"
#include "app/pages/IsoPage.h"
#include "app/pages/LogsPage.h"
#include "app/pages/SourcePage.h"
#include "app/pages/TweaksPage.h"
#include "app/pages/UnattendedPage.h"
#include "app/pages/UpdatesPage.h"
#include "app/pages/images/ImageInspector.h"
#include "base/Log.h"
#include "base/Path.h"
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UpdatePackage.h"
#include "core/image/dism/DismErrors.h"
#include "ui/platform/FileDialog.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/EmptyState.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace wl::app {

namespace size = ui::tokens::size;

namespace {

bool isSourceCandidate(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return true; // extracted setup folder; validated when opened
    }
    switch (core::formatFromPath(path.wstring())) {
    case core::ImageFormat::Iso:
    case core::ImageFormat::Wim:
    case core::ImageFormat::Esd:
    case core::ImageFormat::Swm: return true;
    default: return false;
    }
}

constexpr float kToastMargin = 16.0f;

} // namespace

Shell::Shell(const Localization& strings, Language language, AppState& state, Services services)
    : m_strings(strings), m_language(language), m_state(state), m_services(std::move(services)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_titleBar = &add<TitleBar>(TitleBar::Labels{s(Str::AppName), s(Str::TitleCmdk), s(Str::KbdCtrl),
                                                 s(Str::TitleMinimize), s(Str::TitleClose)});
    m_nav = &add<NavRail>(NavRail::Labels{s(Str::NavCollapse), s(Str::NavExpand), s(Str::KbdCtrl),
                                          s(Str::TooltipsCollapseNav)},
                          [&](Str key) { return strings.get(key); });
    m_status = &add<StatusBar>(StatusBar::Labels{s(Str::StatusNoMount), s(Str::StatusMounted), s(Str::StatusImage),
                                                 s(Str::StatusApply)});
    m_status->cta().onInvoke = [this] {
        if (m_apply && m_apply->running()) {
            m_apply->cancel();
        } else {
            showPage(PageId::Apply);
        }
    };

    m_titleBar->minimizeButton().onInvoke = [this] { m_services.minimize(); };
    m_titleBar->maximizeButton().onInvoke = [this] { m_services.toggleMaximize(); };
    m_titleBar->closeButton().onInvoke = [this] { m_services.close(); };
    m_nav->onSelect = [this](PageId page) { showPage(page); };
    m_nav->onToggleCollapse = [this] {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
    };

    m_features = std::make_unique<FeatureController>(m_state, m_services.postToUi);
    {
        // Embedded in WinLove.exe; tests and tools without the resource get an empty catalog.
        auto catalog = AppxCatalog::parse(embeddedAppxCatalog());
        if (!catalog) {
            catalog = AppxCatalog::parse(R"({"format":"winlove.catalog.appx","groups":[],"apps":[]})");
        }
        m_components = std::make_unique<ComponentController>(m_state, std::move(*catalog), m_language, m_services.postToUi);
    }
    {
        auto tweaks = TweakCatalog::parse(embeddedTweakCatalog());
        if (!tweaks) {
            tweaks = TweakCatalog::parse(R"({"format":"winlove.catalog.tweaks","categories":[],"tweaks":[]})");
        }
        m_registry = std::make_unique<RegistryController>(m_state, std::move(*tweaks));
    }
    {
        auto settings = ImageSettingsCatalog::parse(embeddedSettingsCatalog());
        if (!settings) {
            settings = ImageSettingsCatalog::parse(R"({"format":"winlove.catalog.settings"})");
        }
        m_imageSettings = std::make_unique<ImageSettingsController>(m_state, std::move(*settings));
    }
    m_serviceCtl = std::make_unique<ServiceController>(m_state, embeddedServiceCatalog(), m_services.postToUi);
    m_unattend = std::make_unique<UnattendController>(m_state);
    m_preload = std::make_unique<PreloadController>(m_state, m_services.postToUi);
    m_preload->onCancelled = [this] { showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesCancelledToast), L""); };
    m_iso =std::make_unique<IsoController>(m_state, IsoController::Events{
        m_services.postToUi,
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::IsoFailed), e.message); },
        [this](const core::IsoResult& result, const std::filesystem::path& output, bool openFolder) {
            showToast(ui::InfoKind::Success, m_strings.get(Str::IsoDone),
                      output.filename().wstring() + L" \u00b7 " + formatBytes(result.bytes, m_language));
            if (openFolder) {
                const std::wstring args = L"/select,\"" + output.wstring() + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            }
            // A repack rewrote the setup folder: re-read sizes when it is the open source.
            if (const auto& source = m_state.source(); source && source->format == core::ImageFormat::Folder) {
                openSource(source->path);
            }
        },
    });
    m_apply = std::make_unique<ApplyController>(m_state, ApplyController::Events{
        m_services.postToUi,
        [this](core::SourceInfo source) {
            const auto index = m_state.selectedIndex();
            m_state.setSource(std::move(source)); // new sizes after the commit
            m_state.select(index);
        },
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::ApplyFailedTitle), e.message); },
        {},
    });
    m_images = std::make_unique<ImageController>(m_state, ImageController::Events{
        m_services.postToUi,
        [this](ImageController::Failure f, const Error& e, int index) { onImageFailure(f, e, index); },
        [this](Str title, std::wstring detail) {
            showToast(ui::InfoKind::Success, m_strings.format(title, {{L"edition", detail}, {L"file", detail}}), L"");
        },
        [this](Str title) { showToast(ui::InfoKind::Warning, m_strings.get(title), L""); },
        [this](std::wstring args) { showAdminRequired(std::move(args)); },
        [this](std::filesystem::path source, MountedImage mounted) { restoreMount(source, std::move(mounted)); },
        [this] { m_preload->start(); },
    });

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::MountFolder && !m_autoRestoreTried) {
            const auto& folder = m_state.mountFolder();
            if (folder && folder->state == core::MountState::Ok && folder->record && !m_state.mounted()) {
                m_autoRestoreTried = true;
                continueFolderMount();
            }
        }
        if (change == AppState::Change::Queue) {
            updateQueue();
        }
        if (change == AppState::Change::Apply || change == AppState::Change::Queue ||
            change == AppState::Change::Mount) {
            updateApplyChrome();
        }
        if (change == AppState::Change::Iso || change == AppState::Change::Mount ||
            change == AppState::Change::Operation || change == AppState::Change::Source) {
            updateIsoChrome();
        }
        if (change != AppState::Change::Recent) {
            updateBreadcrumb();
            updateImagesChrome();
            updateStatus();
        }
    });
    showPage(PageId::Source);
    updateStatus();
}

Shell::~Shell() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

SourcePage* Shell::sourcePage() const {
    return m_page == PageId::Source ? static_cast<SourcePage*>(m_pageBody) : nullptr;
}

ImagesPage* Shell::imagesPage() const {
    return m_page == PageId::Images ? static_cast<ImagesPage*>(m_pageBody) : nullptr;
}

LogsPage* Shell::logsPage() const {
    return m_page == PageId::Logs ? dynamic_cast<LogsPage*>(m_pageBody) : nullptr;
}

FeaturesPage* Shell::featuresPage() const {
    return m_page == PageId::Features ? dynamic_cast<FeaturesPage*>(m_pageBody) : nullptr;
}

ApplyPage* Shell::applyPage() const {
    return m_page == PageId::Apply ? dynamic_cast<ApplyPage*>(m_pageBody) : nullptr;
}

IsoPage* Shell::isoPage() const {
    return m_page == PageId::Iso ? dynamic_cast<IsoPage*>(m_pageBody) : nullptr;
}

ComponentsPage* Shell::componentsPage() const {
    return m_page == PageId::Components ? dynamic_cast<ComponentsPage*>(m_pageBody) : nullptr;
}

UpdatesPage* Shell::updatesPage() const {
    return m_page == PageId::Updates ? dynamic_cast<UpdatesPage*>(m_pageBody) : nullptr;
}

RegistryPage* Shell::registryPage() const {
    return m_page == PageId::Registry ? dynamic_cast<RegistryPage*>(m_pageBody) : nullptr;
}

void Shell::importRegFiles(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    if (!m_state.mounted()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::RegistryNoMountTitle), m_strings.get(Str::RegistryNoMountBody));
        return;
    }
    struct Parsed {
        std::filesystem::path file;
        Result<std::vector<core::RegistryWrite>> writes;
    };
    auto parsed = std::make_shared<std::vector<Parsed>>();
    m_state.reader().run<bool>(
        [files, parsed](const core::TaskContext&) -> Result<bool> {
            for (const auto& f : files) {
                parsed->push_back({f, core::readRegFile(f)});
            }
            return true;
        },
        [this, post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive), parsed](Result<bool>) {
            post([this, alive, parsed] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                std::size_t values = 0;
                for (auto& p : *parsed) {
                    if (!p.writes) {
                        showToast(ui::InfoKind::Error, m_strings.get(Str::RegistryImportFailed),
                                  p.writes.error().message + L" \u2014 " + p.writes.error().context);
                        continue;
                    }
                    values += p.writes->size();
                    m_registry->addImport(p.file, std::move(*p.writes));
                }
                if (values > 0) {
                    showToast(ui::InfoKind::Success,
                              m_strings.format(Str::RegistryImported, {{L"n", std::to_wstring(values)}}), L"");
                }
                if (auto* page = registryPage()) {
                    page->showCustom();
                }
            });
        });
}

void Shell::importAnswerFile() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto file = ui::pickFile(owner, m_strings.get(Str::UnattendedImportXml),
                                   {{m_strings.get(Str::UnattendedXmlFiles), L"*.xml"}});
    if (!file) {
        return;
    }
    if (auto r = m_unattend->import(*file); !r) {
        log::error("app", describe(r.error()));
        showToast(ui::InfoKind::Error, m_strings.get(Str::UnattendedImportFailed),
                  r.error().message + (r.error().context.empty() ? L"" : L" — " + r.error().context));
        return;
    }
    showToast(ui::InfoKind::Success, m_strings.get(Str::UnattendedImported), file->filename().wstring());
}

void Shell::saveAnswerFile() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::UnattendedSaveXml),
                                         {{m_strings.get(Str::UnattendedXmlFiles), L"*.xml"}}, L"autounattend.xml", L"xml");
    if (!target) {
        return;
    }
    const auto saved = m_unattend->save(*target);
    showToast(saved ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(saved ? Str::UnattendedSaved : Str::UnattendedSaveFailed), target->wstring());
}

ServicesPage* Shell::servicesPage() const {
    return m_page == PageId::Services ? dynamic_cast<ServicesPage*>(m_pageBody) : nullptr;
}

DriversPage* Shell::driversPage() const {
    return m_page == PageId::Drivers ? dynamic_cast<DriversPage*>(m_pageBody) : nullptr;
}

void Shell::scanDriverFolder() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto folder = ui::pickFolder(owner, m_strings.get(Str::UpdatesScanFolder));
    if (!folder) {
        return;
    }
    showToast(ui::InfoKind::Info, m_strings.get(Str::DriversScanning), folder->wstring());
    auto infs = std::make_shared<std::vector<core::DriverInf>>();
    m_state.reader().run<bool>(
        [path = *folder, infs](const core::TaskContext&) -> Result<bool> {
            *infs = core::scanDrivers(path);
            return true;
        },
        [this, post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive), path = *folder, infs](Result<bool>) {
            post([this, alive, path, infs] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                if (infs->empty()) {
                    showToast(ui::InfoKind::Warning, m_strings.get(Str::DriversNoneFound), path.wstring());
                    return;
                }
                const std::size_t n = infs->size();
                m_state.addDriverScan(path, std::move(*infs));
                showToast(ui::InfoKind::Success, m_strings.format(Str::DriversScanned, {{L"n", std::to_wstring(n)}}),
                          path.wstring());
            });
        });
}

void Shell::addUpdates(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    if (!m_state.mounted()) {
        // The queue belongs to a mounted image (the next mount would silently clear it).
        showToast(ui::InfoKind::Warning, m_strings.get(Str::UpdatesNoMountTitle), m_strings.get(Str::UpdatesNoMountBody));
        return;
    }
    const std::size_t added = UpdatesPage::queuePackages(m_state, files);
    if (added > 0) {
        showToast(ui::InfoKind::Success, m_strings.format(Str::UpdatesAdded, {{L"n", std::to_wstring(added)}}), L"");
    }
}

void Shell::updateComponentInspector() {
    auto* page = componentsPage();
    auto* inspector = dynamic_cast<ComponentInspector*>(m_sideInspector);
    if (!page || !inspector) {
        return;
    }
    const auto item = page->selectedItem();
    const bool queued = item && m_components->queued(*item);
    const bool wasVisible = inspector->visible();
    inspector->set(item, page->selectedGroupName(), queued);
    inspector->setVisible(item.has_value());
    if (wasVisible != inspector->visible()) {
        layout();
    }
    invalidate();
}

void Shell::loadPreset() {
    if (!m_state.mounted()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ComponentsPresetNeedsMount), L"");
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto file = ui::pickFile(owner, m_strings.get(Str::ComponentsLoadPreset),
                                   {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}});
    if (!file) {
        return;
    }
    std::ifstream in(*file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto set = core::ops::ChangeSet::fromJson(buffer.str());
    if (!set) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::ComponentsPresetFailed), set.error().message);
        return;
    }
    for (const auto& op : set->operations()) {
        m_state.queue(op);
    }
    showToast(ui::InfoKind::Success,
              m_strings.format(Str::ComponentsPresetLoaded, {{L"n", std::to_wstring(set->size())}}), file->wstring());
}

void Shell::updateIsoChrome() {
    if (!m_actionIso) {
        return;
    }
    const bool running = m_iso->running();
    m_actionIso->setText(m_strings.get(running ? Str::IsoCancel : Str::IsoBuild));
    const auto* page = isoPage();
    m_actionIso->setEnabled(running || (!m_iso->blocker() && page && page->formValid()));
    if (m_pageView) {
        m_pageView->layout();
    }
}

void Shell::startIso() {
    auto* page = isoPage();
    if (!page || m_iso->blocker() || !page->formValid()) {
        return;
    }
    const auto request = page->request();
    m_state.setIsoFolder(request.output.parent_path());
    m_iso->start(request);
}

void Shell::updateApplyChrome() {
    const bool running = m_apply->running();
    m_status->cta().setOverride(running ? m_strings.get(Str::ApplyStop) : std::wstring());
    if (m_page != PageId::Apply) {
        return;
    }
    const int mode = static_cast<int>(ApplyPage::modeFor(m_state));
    if (mode != m_applyMode) {
        // Rebuild later: this may run inside a click on a header button of the page being replaced.
        m_applyMode = mode;
        m_services.postToUi([this, alive = std::weak_ptr<bool>(m_alive)] {
            if (const auto a = alive.lock(); a && *a && m_page == PageId::Apply) {
                showPage(PageId::Apply);
            }
        });
    } else if (auto* page = applyPage()) {
        const auto [title, description] = page->header();
        m_pageView->setHeader(title, description);
    }
}

void Shell::requestApply() {
    if (!m_apply->canStart()) {
        return;
    }
    if (!m_apply->highRisk().empty()) {
        showApplyConfirm();
        return;
    }
    m_apply->start();
}

void Shell::showApplyConfirm() {
    if (!host() || !m_apply->canStart()) {
        return;
    }
    std::vector<RiskConfirm::Item> items;
    for (const auto& op : m_apply->highRisk()) {
        items.push_back({ApplyPage::displayName(m_state, op),
                         op.sizeDelta < 0 ? formatBytes(static_cast<std::uint64_t>(-op.sizeDelta), m_language)
                                          : std::wstring()});
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::ApplyConfirmTitle), m_strings.get(Str::ApplyConfirmBody),
                                               ui::icons::Icon::ErrorOctagon, ui::tokens::Color::StatusError);
    ui::Dialog* raw = dialog.get();
    const std::size_t count = items.size();
    auto& content = raw->setContent<RiskConfirm>(RiskConfirm::heightFor(count), std::move(items),
                                                 m_strings.get(Str::ApplyConfirmAck));
    raw->onCancel = [this, raw] { host()->popModal(raw); };
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    ui::Button& go = raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::ApplyConfirmGo),
                                    [this, raw] {
                                        host()->popModal(raw);
                                        m_apply->start();
                                    },
                                    /*primary=*/true);
    go.setEnabled(false);
    content.onAck = [&go](bool on) { go.setEnabled(on); };
    pushDialog(std::move(dialog));
}

void Shell::savePreset(const core::ops::ChangeSet& changes) {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ApplyPresetTitle),
                                         {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}},
                                         L"WinLove.wlpreset", L"wlpreset");
    if (!target) {
        return;
    }
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    const std::string json = changes.toJson();
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    showToast(out ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(out ? Str::ApplyPresetSaved : Str::ApplySaveFailed), target->wstring());
}

void Shell::saveApplyLog() {
    const auto& run = m_state.applyRun();
    const auto buffer = m_state.logBuffer();
    if (!run || !buffer) {
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ApplySaveLog),
                                         {{m_strings.get(Str::LogsLogFiles), L"*.log;*.txt"}}, L"WinLove-apply.log", L"log");
    if (!target) {
        return;
    }
    std::uint64_t version = run->logVersion;
    std::wstring text;
    for (const auto& e : buffer->since(version)) {
        text += log::formatLine(e) + L"\r\n";
    }
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    const std::string bytes = utf8::fromWide(text);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    showToast(out ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(out ? Str::ApplyLogSaved : Str::ApplySaveFailed), target->wstring());
}

bool Shell::inspectorVisible() const {
    // Screen 02 shows it for the selected edition; screen 03 hides it while the engine works.
    if (m_sideInspector) {
        return m_sideInspector->visible();
    }
    return m_inspector && m_page == PageId::Images && m_state.selectedImage() && !m_state.operation();
}

void Shell::updateBreadcrumb() {
    // statusbar-titlebar.md: image › edition › state. Without a source: the page title.
    if (const auto& source = m_state.source()) {
        std::vector<std::wstring> parts{source->path.filename().empty() ? source->path.wstring()
                                                                         : source->path.filename().wstring()};
        if (const auto& mounted = m_state.mounted()) {
            parts.push_back(mounted->edition);
            parts.push_back(m_strings.get(Str::StatusMounted));
        } else if (const auto& op = m_state.operation(); op && op->kind == EngineOperation::Kind::Mounting) {
            parts.push_back(op->edition);
            parts.push_back(m_strings.get(Str::StatusMounting));
        } else if (const auto* image = m_state.selectedImage(); image && m_page == PageId::Images) {
            parts.push_back(image->name);
        }
        m_titleBar->setBreadcrumb(std::move(parts));
    } else if (m_page == PageId::Gallery) {
        m_titleBar->setBreadcrumb({L"Widget gallery"});
    } else {
        m_titleBar->setBreadcrumb({m_strings.get(pageInfo(m_page).title)});
    }
}

void Shell::updateQueue() {
    const auto& changes = m_state.changes();
    m_status->cta().setQueue(static_cast<int>(changes.size()));
    const auto featureOps = static_cast<int>(m_features->queuedCount());
    m_nav->setBadge(PageId::Features, featureOps);
    m_nav->setBadge(PageId::Apply, static_cast<int>(changes.size()));
    m_nav->setBadge(PageId::Components, static_cast<int>(m_components->queuedCount()));
    m_nav->setBadge(PageId::Updates, static_cast<int>(changes.count(core::ops::OpKind::AddPackage)));
    m_nav->setBadge(PageId::Drivers, static_cast<int>(changes.count(core::ops::OpKind::AddDriver)));
    m_nav->setBadge(PageId::Services, static_cast<int>(changes.count(core::ops::OpKind::SetServiceStart)));
    m_nav->setBadge(PageId::Registry, m_registry->checkedCount());
    m_nav->setBadge(PageId::Tweaks, m_imageSettings->changedCount());
    if (m_actionReset) {
        m_actionReset->setEnabled(featureOps > 0);
    }
}

void Shell::updateStatus() {
    if (const auto& mounted = m_state.mounted()) {
        std::wstring imageSize;
        if (const auto& source = m_state.source()) {
            for (const auto& image : source->install.images) {
                if (image.index == mounted->index) {
                    imageSize = formatBytes(image.totalBytes, m_language);
                }
            }
        }
        m_status->setMount(mounted->mountDir.wstring(), imageSize);
    } else {
        m_status->setMount(std::nullopt, L"");
    }
    const auto& run = m_state.applyRun();
    const auto& iso = m_state.isoRun();
    if (run && run->stage != AppState::ApplyRun::Stage::Done) {
        m_status->setTask(m_strings.get(Str::StatusApplying), static_cast<float>(run->fraction));
    } else if (iso && iso->running) {
        m_status->setTask(m_strings.get(Str::StatusBuilding), static_cast<float>(iso->fraction));
    } else if (const auto& op = m_state.operation()) {
        const Str label = op->kind == EngineOperation::Kind::Mounting  ? Str::StatusMounting
                          : op->kind == EngineOperation::Kind::Reading ? Str::StatusReading
                                                                       : Str::StatusScanning;
        m_status->setTask(m_strings.get(label), static_cast<float>(op->fraction));
    } else {
        m_status->setTask(std::nullopt, 0);
    }
}

void Shell::updateImagesChrome() {
    const auto* image = m_state.selectedImage();
    const bool mounted = m_state.mounted().has_value();
    const bool mountedHere = mounted && image && m_state.mounted()->index == image->index;
    const bool busy = m_images->busy();
    if (m_actionMount) {
        m_actionMount->setText(m_strings.get(mounted ? Str::ImagesUnmount : Str::ImagesMount));
        m_actionMount->setEnabled(!busy && (mounted || (image && m_images->canMount())));
        m_actionMount->setTooltip(m_images->isEsdSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{});
    }
    if (m_actionExport) {
        m_actionExport->setEnabled(!busy && image != nullptr);
    }
    if (m_actionEsd) {
        m_actionEsd->setEnabled(!busy && m_images->isEsdSource());
        m_actionEsd->setTooltip(m_images->isEsdSource() ? std::wstring{} : m_strings.get(Str::ImagesEsdOnly));
    }
    if (m_inspector) {
        m_inspector->set(m_state.source() ? &*m_state.source() : nullptr, image, mountedHere,
                         image && m_images->canMount(), image && m_images->canDelete(),
                         m_images->isEsdSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{},
                         m_images->canDelete() ? std::wstring{} : m_strings.get(Str::ImagesReadOnlySource));
        m_inspector->setVisible(inspectorVisible());
    }
    layout();
    invalidate();
}

void Shell::showPage(PageId page) {
    if ((m_page == PageId::Logs || m_page == PageId::Apply) && page != m_page && m_services.stopTimer) {
        m_services.stopTimer(kLogTimer);
    }
    m_page = page;
    const PageInfo& info = pageInfo(page);
    m_nav->setActive(page);

    if (m_pageView) {
        removeChild(m_pageView);
        m_pageView = nullptr;
        m_pageBody = nullptr;
    }
    if (m_inspector) {
        removeChild(m_inspector);
        m_inspector = nullptr;
    }
    if (m_sideInspector) {
        removeChild(m_sideInspector);
        m_sideInspector = nullptr;
    }
    m_actionExpand = nullptr;
    m_actionMount = m_actionExport = m_actionEsd = nullptr;
    m_actionReset = nullptr;
    m_actionIso = nullptr;

    if (page == PageId::Gallery) {
        m_pageView = &add<PageView>(L"Widget gallery", L"Faz 1.8 — every widget in every state (dev only).");
        m_pageBody = &m_pageView->setBody<GalleryPage>();
    } else {
        const std::wstring title = m_strings.get(info.title);
        m_pageView = &add<PageView>(title, info.description ? m_strings.get(*info.description) : std::wstring{});
        if (page == PageId::Source) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFile), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] { pickSourceFile(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFolder)).onInvoke = [this] {
                pickSourceFolder();
            };
            m_pageBody = &m_pageView->setBody<SourcePage>(
                m_state, m_strings, m_language,
                SourcePage::Intents{[this] { pickSourceFile(); },
                                    [this](const std::filesystem::path& p) { openSource(p); }});
        } else if (page == PageId::Images) {
            if (m_state.source()) {
                m_actionEsd = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesEsdToWim));
                m_actionEsd->onInvoke = [this] { convertEsd(); };
                m_actionExport = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesExport));
                m_actionExport->onInvoke = [this] { exportSelected(); };
                m_actionMount = &m_pageView->addAction(ui::ButtonKind::Primary, m_strings.get(Str::ImagesMount));
                m_actionMount->onInvoke = [this] {
                    if (m_state.mounted()) {
                        askUnmount();
                    } else if (const auto index = m_state.selectedIndex()) {
                        m_images->mount(*index);
                    }
                };
            }
            m_pageBody = &m_pageView->setBody<ImagesPage>(m_state, *m_images, m_strings, m_language,
                                                          [this] { showPage(PageId::Source); });
            static_cast<ImagesPage*>(m_pageBody)->onContinueMount = [this] { continueFolderMount(); };
            m_inspector = &add<ImageInspector>(m_strings, m_language);
            m_inspector->onMount = [this] {
                if (const auto index = m_state.selectedIndex()) {
                    m_images->mount(*index);
                }
            };
            m_inspector->onUnmount = [this] { askUnmount(); };
            m_inspector->onDelete = [this] { askDeleteSelected(); };
        } else if (page == PageId::Components) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ComponentsLoadPreset),
                                  ui::icons::Icon::PresetBookmark)
                .onInvoke = [this] { loadPreset(); };
            m_actionExpand = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ComponentsCollapseAll));
            m_actionExpand->onInvoke = [this] {
                if (auto* p = componentsPage()) {
                    p->setAllExpanded(!p->allExpanded());
                    m_actionExpand->setText(
                        m_strings.get(p->allExpanded() ? Str::ComponentsCollapseAll : Str::ComponentsExpandAll));
                    m_pageView->layout();
                }
            };
            auto& body = m_pageView->setBody<ComponentsPage>(m_state, *m_components, m_strings, m_language,
                                                             [this] { showPage(PageId::Images); });
            m_pageBody = &body;
            auto& inspector = add<ComponentInspector>(m_strings, m_language);
            m_sideInspector = &inspector;
            inspector.setVisible(false);
            inspector.onToggle = [this] {
                if (auto* p = componentsPage()) {
                    if (const auto item = p->selectedItem()) {
                        m_components->toggle(*item);
                    }
                }
            };
            body.onSelectionChanged = [this] { updateComponentInspector(); };
        } else if (page == PageId::Registry) {
            auto pick = [this] {
                const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
                importRegFiles(ui::pickFiles(owner, m_strings.get(Str::RegistryImportReg),
                                             {{m_strings.get(Str::RegistryFilter), L"*.reg"}}));
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::RegistryImportReg), ui::icons::Icon::RegFile)
                .onInvoke = pick;
            m_pageBody = &m_pageView->setBody<RegistryPage>(
                m_state, *m_registry, m_strings, m_language,
                RegistryPage::Intents{pick, [this] { showPage(PageId::Images); }});
        } else if (page == PageId::Unattended) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UnattendedImportXml), ui::icons::Icon::Import)
                .onInvoke = [this] { importAnswerFile(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UnattendedSaveXml), ui::icons::Icon::Save)
                .onInvoke = [this] { saveAnswerFile(); };
            m_pageBody = &m_pageView->setBody<UnattendedPage>(m_state, *m_unattend, m_strings);
        } else if (page == PageId::Tweaks) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::TweaksApplyRecommended)).onInvoke = [this] {
                if (!m_state.mounted()) {
                    showToast(ui::InfoKind::Warning, m_strings.get(Str::TweaksNoMountTitle),
                              m_strings.get(Str::TweaksNoMountBody));
                    return;
                }
                const int changed = m_imageSettings->applyRecommended();
                showToast(ui::InfoKind::Success,
                          changed > 0 ? m_strings.format(Str::TweaksRecommendedApplied, {{L"n", std::to_wstring(changed)}})
                                      : m_strings.get(Str::TweaksRecommendedNone),
                          L"");
            };
            m_pageBody = &m_pageView->setBody<TweaksPage>(m_state, *m_imageSettings, m_strings, m_language,
                                                          [this] { showPage(PageId::Images); });
        } else if (page == PageId::Services) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ServicesReset)).onInvoke = [this] {
                m_serviceCtl->resetChanges();
            };
            m_pageBody = &m_pageView->setBody<ServicesPage>(
                m_state, *m_serviceCtl, m_strings, m_language,
                ServicesPage::Intents{[this] { showPage(PageId::Images); },
                                      [this](std::wstring title, std::wstring body) {
                                          showToast(ui::InfoKind::Warning, std::move(title), std::move(body));
                                      }});
        } else if (page == PageId::Drivers) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesScanFolder), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] { scanDriverFolder(); };
            m_pageBody = &m_pageView->setBody<DriversPage>(
                m_state, m_strings, m_language,
                DriversPage::Intents{[this] { scanDriverFolder(); }, [this] { showPage(PageId::Images); }});
        } else if (page == PageId::Updates) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesScanFolder), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] {
                    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
                    if (const auto folder = ui::pickFolder(owner, m_strings.get(Str::UpdatesScanFolder))) {
                        const auto files = core::scanUpdates(*folder);
                        if (files.empty()) {
                            showToast(ui::InfoKind::Warning, m_strings.get(Str::UpdatesNoneFound), folder->wstring());
                        } else {
                            addUpdates(files);
                        }
                    }
                };
            auto pick = [this] {
                const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
                addUpdates(ui::pickFiles(owner, m_strings.get(Str::UpdatesPickTitle),
                                         {{m_strings.get(Str::UpdatesFilter), L"*.msu;*.cab"}}));
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesAddPackage), ui::icons::Icon::Add)
                .onInvoke = pick;
            m_pageBody = &m_pageView->setBody<UpdatesPage>(m_state, m_strings, m_language,
                                                           UpdatesPage::Intents{pick, [this] { showPage(PageId::Images); }});
        } else if (page == PageId::Features) {
            m_actionReset = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::FeaturesResetChanges));
            m_actionReset->onInvoke = [this] { m_features->resetChanges(); };
            m_pageBody = &m_pageView->setBody<FeaturesPage>(m_state, *m_features, m_strings, m_language,
                                                            [this] { showPage(PageId::Images); });
        } else if (page == PageId::Apply) {
            const auto mode = ApplyPage::modeFor(m_state);
            m_applyMode = static_cast<int>(mode);
            if (mode == ApplyPage::Mode::Summary) {
                m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ApplySaveAsPreset),
                                      ui::icons::Icon::PresetBookmark)
                    .onInvoke = [this] { savePreset(m_state.changes()); };
                m_pageView->addAction(ui::ButtonKind::Primary,
                                      m_strings.format(Str::ApplyApplyN, {{L"n", std::to_wstring(m_state.changes().size())}}),
                                      ui::icons::Icon::ApplyPlay)
                    .onInvoke = [this] { requestApply(); };
            } else if (mode == ApplyPage::Mode::Running) {
                m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ApplyStop), ui::icons::Icon::Stop)
                    .onInvoke = [this] { m_apply->cancel(); };
            } else if (mode == ApplyPage::Mode::Done) {
                m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ApplySaveLog), ui::icons::Icon::Save)
                    .onInvoke = [this] { saveApplyLog(); };
                m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ApplySaveToPreset),
                                      ui::icons::Icon::PresetBookmark)
                    .onInvoke = [this] {
                        if (const auto& run = m_state.applyRun()) {
                            savePreset(run->changes);
                        }
                    };
                m_pageView->addAction(ui::ButtonKind::Primary, m_strings.get(Str::NavIso), ui::icons::Icon::IsoBuild)
                    .onInvoke = [this] { showPage(PageId::Iso); };
            }
            auto& body = m_pageView->setBody<ApplyPage>(
                m_state, *m_apply, m_strings, m_language,
                ApplyPage::Intents{[this] { showApplyConfirm(); }, [this] { showPage(PageId::Images); },
                                   [this] { showPage(PageId::Features); }});
            m_pageBody = &body;
            const auto [applyTitle, applyDescription] = body.header();
            m_pageView->setHeader(applyTitle, applyDescription);
            if (mode == ApplyPage::Mode::Running && m_services.startTimer) {
                m_services.startTimer(kLogTimer, 250);
            }
        } else if (page == PageId::Iso) {
            m_actionIso = &m_pageView->addAction(ui::ButtonKind::Primary, m_strings.get(Str::IsoBuild), ui::icons::Icon::IsoBuild);
            m_actionIso->onInvoke = [this] {
                if (m_iso->running()) {
                    m_iso->cancel();
                } else {
                    startIso();
                }
            };
            m_pageBody = &m_pageView->setBody<IsoPage>(
                m_state, *m_iso, m_strings, m_language,
                IsoPage::Intents{[this]() -> std::optional<std::filesystem::path> {
                                     const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
                                     return ui::pickFolder(owner, m_strings.get(Str::IsoFolder));
                                 },
                                 [](const std::filesystem::path& file) {
                                     const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
                                     ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                                 },
                                 [this] { showPage(PageId::Source); }, m_services.postToUi});
        } else if (page == PageId::Logs) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::LogsClear)).onInvoke = [this] {
                if (auto* logs = logsPage()) {
                    logs->clear();
                }
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::CommonExport), ui::icons::Icon::Export)
                .onInvoke = [this] { exportLog(); };
            m_pageBody = &m_pageView->setBody<LogsPage>(m_state, m_strings, m_language);
            if (m_services.startTimer) {
                m_services.startTimer(kLogTimer, 250);
            }
        } else {
            // Placeholder until the page's roadmap step is done (docs/ROADMAP.md, Faz 3).
            const std::wstring step(info.roadmapStep.begin(), info.roadmapStep.end());
            m_pageBody = &m_pageView->setBody<ui::EmptyState>(info.icon, m_strings.get(Str::ShellPendingTitle),
                                                              m_strings.format(Str::ShellPendingBody, {{L"id", step}}));
        }
    }
    // The toast stays on top of whatever page is shown.
    if (!m_toast) {
        m_toast = &add<ui::Toast>();
    }
    bringToFront(m_toast);
    updateQueue();
    updateIsoChrome();
    updateBreadcrumb();
    updateImagesChrome();
    layout();
    invalidate();
}

// ---- toast ---------------------------------------------------------------------------------

void Shell::showToast(ui::InfoKind kind, std::wstring title, std::wstring message) {
    m_toast->show(kind, std::move(title), std::move(message));
    layout();
    if (m_services.startTimer) {
        m_services.startTimer(kToastTimer, ui::Toast::kDurationMs);
    }
}

void Shell::onTimer(UINT id) {
    if (id == kLogTimer) {
        if (auto* logs = logsPage()) {
            logs->poll();
        }
        if (auto* apply = applyPage()) {
            apply->poll();
            const auto [title, description] = apply->header(); // live ETA
            m_pageView->setHeader(title, description);
        }
        return;
    }
    if (id != kToastTimer || m_toast->hoveredNow()) {
        return; // hovered: keep it; the timer fires again
    }
    if (m_services.stopTimer) {
        m_services.stopTimer(kToastTimer);
    }
    m_toast->hide();
}

// ---- sources -------------------------------------------------------------------------------

void Shell::openSource(const std::filesystem::path& path, std::function<void()> then) {
    if (m_images->busy()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesBusy), L"");
        return;
    }
    if (m_state.mounted()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesUnmountFirst), L"");
        return;
    }
    const std::uint64_t serial = ++m_openSerial; // a newer request wins over one still running
    if (auto* page = sourcePage()) {
        page->setLoading(true);
    }
    log::info("app", L"opening source " + path.wstring());
    // Reader thread (not the DISM engine: a mount inspection or feature read must not delay this).
    m_state.reader().run<core::SourceInfo>(
        [path](const core::TaskContext&) { return core::openSource(path); },
        [this, post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive), serial,
         then = std::move(then)](Result<core::SourceInfo> result) {
            post([this, alive, then, serial, result = std::move(result)]() mutable {
                const auto stillAlive = alive.lock();
                if (!stillAlive || !*stillAlive || serial != m_openSerial) {
                    return;
                }
                if (auto* page = sourcePage()) {
                    page->setLoading(false);
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (auto* page = sourcePage()) {
                        page->showError(result.error().message + L" — " + result.error().context);
                    } else {
                        showToast(ui::InfoKind::Error, m_strings.get(Str::SourceOpenFailed), result.error().message);
                    }
                    return;
                }
                m_state.setSource(std::move(*result));
                showPage(PageId::Images);
                if (then) {
                    then();
                }
            });
        });
}

void Shell::continueFolderMount() {
    const auto& folder = m_state.mountFolder();
    if (!folder || !folder->record || m_state.mounted()) {
        return;
    }
    const auto& m = *folder->record;
    restoreMount(sourceForMountedImage(m.imagePath), MountedImage{m.mountPath, m.imagePath, m.index, {}, m.readOnly});
}

void Shell::restoreMount(const std::filesystem::path& source, MountedImage mounted) {
    log::info("app", L"restore: opening " + source.wstring());
    if (m_state.source() && _wcsicmp(m_state.source()->path.c_str(), nativePath(source).c_str()) == 0) {
        // The right source is already open: just mark the mount.
        m_state.select(mounted.index);
        mounted.edition = m_state.selectedImage() ? m_state.selectedImage()->name : std::format(L"#{}", mounted.index);
        const std::wstring edition = mounted.edition;
        m_state.setMounted(std::move(mounted));
        showToast(ui::InfoKind::Info, m_strings.format(Str::ImagesMountRestored, {{L"edition", edition}}), L"");
        m_preload->start();
        return;
    }
    openSource(source, [this, mounted = std::move(mounted)]() mutable {
        mounted.edition = std::format(L"#{}", mounted.index);
        if (const auto& info = m_state.source()) {
            for (const auto& image : info->install.images) {
                if (image.index == mounted.index) {
                    mounted.edition = image.name;
                }
            }
        }
        m_state.select(mounted.index);
        const std::wstring edition = mounted.edition;
        m_state.setMounted(std::move(mounted));
        showToast(ui::InfoKind::Info, m_strings.format(Str::ImagesMountRestored, {{L"edition", edition}}),
                  m_state.mounted()->imagePath.wstring());
        m_preload->start();
    });
}

void Shell::pickSourceFile() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto file = ui::pickFile(owner, m_strings.get(Str::SourceOpenFile),
                                   {{m_strings.get(Str::SourceFilterImages), L"*.iso;*.wim;*.esd;*.swm"},
                                    {m_strings.get(Str::SourceFilterAll), L"*.*"}});
    if (file) {
        openSource(*file);
    }
}

void Shell::pickSourceFolder() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    if (const auto folder = ui::pickFolder(owner, m_strings.get(Str::SourcePickFolder))) {
        openSource(*folder);
    }
}

ui::Dialog& Shell::pushDialog(std::unique_ptr<ui::Dialog> dialog) {
    // Push after the buttons exist: the modal focuses its first focusable (the cancel button).
    ui::Dialog* raw = dialog.get();
    raw->onCancel = [this, raw] { host()->popModal(raw); };
    host()->pushModal(std::move(dialog));
    return *raw;
}

void Shell::showAdminRequired(std::wstring relaunchArgs) {
    if (!host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::DialogsAdminTitle),
                                                           m_strings.get(Str::DialogsAdminBody), ui::icons::Icon::ShieldWarning,
                                                           ui::tokens::Color::StatusWarning, 440.0f);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::DialogsAdminGo),
                   [this, raw, args = std::move(relaunchArgs)] {
                       host()->popModal(raw);
                       if (m_services.relaunchElevated && m_services.relaunchElevated(args)) {
                           m_services.close();
                       }
                   },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
}

bool Shell::dragEnter(const std::vector<std::filesystem::path>& files) {
    if (registryPage() && m_state.mounted()) {
        return std::ranges::any_of(files, [](const auto& f) { return _wcsicmp(f.extension().c_str(), L".reg") == 0; });
    }
    if (auto* updates = updatesPage(); updates && m_state.mounted()) {
        const bool any = std::ranges::any_of(files, [](const auto& f) { return core::isUpdateFile(f); });
        updates->setDragState(any ? ui::DropZone::DragState::Valid : ui::DropZone::DragState::Invalid);
        return any;
    }
    const bool valid = !files.empty() && isSourceCandidate(files.front());
    if (auto* page = sourcePage()) {
        page->setDragState(valid ? ui::DropZone::DragState::Valid : ui::DropZone::DragState::Invalid);
    }
    return valid;
}

void Shell::dragLeave() {
    if (auto* updates = updatesPage()) {
        updates->setDragState(ui::DropZone::DragState::None);
    }
    if (auto* page = sourcePage()) {
        page->setDragState(ui::DropZone::DragState::None);
    }
}

void Shell::drop(const std::vector<std::filesystem::path>& files) {
    if (registryPage() && m_state.mounted()) {
        std::vector<std::filesystem::path> regs;
        std::ranges::copy_if(files, std::back_inserter(regs),
                             [](const auto& f) { return _wcsicmp(f.extension().c_str(), L".reg") == 0; });
        importRegFiles(regs);
        return;
    }
    if (auto* updates = updatesPage(); updates && m_state.mounted()) {
        updates->setDragState(ui::DropZone::DragState::None);
        addUpdates(files);
        return;
    }
    // Several files may be dropped; the first source-type one opens (others are ignored for now).
    for (const auto& file : files) {
        if (isSourceCandidate(file)) {
            if (m_page != PageId::Source) {
                showPage(PageId::Source);
            }
            openSource(file);
            return;
        }
    }
}

// ---- images (P02) ----------------------------------------------------------------------------

void Shell::onImageFailure(ImageController::Failure failure, const Error& error, int /*index*/) {
    const std::wstring code = std::format(L"0x{:08X}", static_cast<unsigned>(error.hresult));
    const bool mountFailure = failure == ImageController::Failure::Mount;
    const std::wstring title = mountFailure ? m_strings.format(Str::ImagesMountFailed, {{L"code", code}})
                                            : std::format(L"{} ({})", m_strings.get(Str::ImagesFailedTitle), code);
    // What the code means (DismErrors catalog) decides the advice and whether "Onar" helps; the
    // raw DISM/WIM text goes to the log.
    const auto info = core::explainError(error.hresult);
    log::error("app", std::format(L"{} — {} [{}] {}", code, info.label, core::remedyName(info.remedy),
                                  core::systemMessage(error.hresult)));
    std::wstring message = m_strings.get(ImagesPage::remedyText(info.remedy));
    if (!info.known && !error.message.empty()) {
        message = error.message;
    }
    if (auto* page = imagesPage(); page && m_state.source()) {
        page->showFailure(title, message, ImagesPage::remedyRepairs(info.remedy));
    }
    showToast(ui::InfoKind::Error, mountFailure ? m_strings.get(Str::ToastsMountFailed) : m_strings.get(Str::ImagesFailedTitle),
              m_strings.format(Str::ToastsMountFailedBody, {{L"code", code}}));
}

bool Shell::confirmClose() {
    // Reading the mounted image's lists changes nothing: stop it and let the window go.
    const auto& op = m_state.operation();
    const bool reading = op && op->kind == EngineOperation::Kind::Reading;
    const bool busy = (m_images->busy() && !reading) || m_apply->running() || m_iso->running();
    if (!busy || !host()) {
        if (reading) {
            m_images->cancel();
        }
        return true;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::DialogsBusyCloseTitle),
                                               m_strings.get(Str::DialogsBusyCloseBody), ui::icons::Icon::WarningTriangle,
                                               ui::tokens::Color::StatusWarning);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonOk), [this, raw] { host()->popModal(raw); },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
    return false;
}

void Shell::askUnmount() {
    if (!host() || !m_state.mounted()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::ImagesUnmountTitle),
                                                           m_strings.get(Str::ImagesUnmountBody), ui::icons::Icon::Unmount,
                                                           ui::tokens::Color::TextSecondary);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesUnmountDiscard), [this, raw] {
        host()->popModal(raw);
        m_images->unmount(/*commit=*/false);
    });
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::ImagesUnmountCommit),
                   [this, raw] {
                       host()->popModal(raw);
                       m_images->unmount(/*commit=*/true);
                   },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
}

void Shell::askDeleteSelected() {
    const auto* image = m_state.selectedImage();
    if (!host() || !image) {
        return;
    }
    const int index = image->index;
    auto dialog = std::make_unique<ui::Dialog>(
        m_strings.get(Str::DialogsDeleteIndexTitle),
        m_strings.format(Str::DialogsDeleteIndexBody, {{L"name", std::format(L"{} · {}", index, image->name)}}),
        ui::icons::Icon::ErrorOctagon, ui::tokens::Color::StatusError);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::CommonDelete), [this, raw, index] {
        host()->popModal(raw);
        m_images->deleteIndex(index);
    });
    pushDialog(std::move(dialog));
}

void Shell::exportSelected() {
    const auto* image = m_state.selectedImage();
    if (!image) {
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ImagesExport), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         image->name + L".wim", L"wim");
    if (target) {
        m_images->exportIndex(image->index, *target);
    }
}

void Shell::exportLog() {
    auto* logs = logsPage();
    if (!logs) {
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::LogsExportTitle),
                                         {{m_strings.get(Str::LogsLogFiles), L"*.log;*.txt"}}, L"WinLove.log", L"log");
    if (!target) {
        return;
    }
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    const std::string utf8 = utf8::fromWide(logs->exportText());
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    if (out) {
        showToast(ui::InfoKind::Success, m_strings.get(Str::LogsExported), target->wstring());
    } else {
        showToast(ui::InfoKind::Error, m_strings.get(Str::LogsExportFailed), target->wstring());
    }
}

void Shell::convertEsd() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ImagesEsdToWim), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         L"install.wim", L"wim");
    if (target) {
        m_images->convertEsd(*target);
    }
}

// ---- layout, shortcuts -----------------------------------------------------------------------

void Shell::setNavCollapsed(bool collapsed, bool animated) {
    if (!animated) {
        m_userCollapsed = collapsed;
    }
    m_navTarget = collapsed ? 0.0f : 1.0f;
    if (animated && m_navExpansion.animateTo(m_navTarget, ui::tokens::motion::slowMs)) {
        animate();
    } else {
        m_navExpansion.snapTo(m_navTarget);
        m_nav->setExpansion(m_navTarget);
        layout();
    }
    invalidate();
}

bool Shell::tick(double now) {
    const bool running = m_navExpansion.tick(now);
    m_nav->setExpansion(m_navExpansion.value());
    layout();
    invalidate();
    return running;
}

bool Shell::handleShortcut(const ui::KeyEvent& key) {
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey == 'F') {
        if (auto* logs = logsPage()) {
            logs->focusSearch();
            return true;
        }
        if (auto* features = featuresPage()) {
            features->focusSearch();
            return true;
        }
        if (auto* components = componentsPage()) {
            components->focusSearch();
            return true;
        }
        if (auto* drivers = driversPage()) {
            drivers->focusSearch();
            return true;
        }
        if (auto* services = servicesPage()) {
            services->focusSearch();
            return true;
        }
    }
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey == 'B') {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
        return true;
    }
    if (key.ctrl && key.shift && key.virtualKey == 'T') {
        if (m_services.toggleTheme) {
            m_services.toggleTheme();
        }
        return true;
    }
    if (key.ctrl && key.shift && key.virtualKey == 'G') {
        showPage(PageId::Gallery);
        return true;
    }
    if (key.ctrl && !key.shift && key.virtualKey == VK_OEM_COMMA) {
        showPage(PageId::Settings);
        return true;
    }
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey >= '1' && key.virtualKey <= '9') {
        // Ctrl+1…9: nav items in display order.
        int index = static_cast<int>(key.virtualKey - '1');
        for (const auto& page : allPages()) {
            if (page.navGroup >= 0 && index-- == 0) {
                showPage(page.id);
                return true;
            }
        }
    }
    return false;
}

void Shell::layout() {
    const ui::RectF b = bounds();
    // layout-system.md: below 1200 the rail collapses to 44; the user can still open it (Ctrl B),
    // and widening the window restores their own preference.
    constexpr float kNarrowWidth = 1200.0f;
    const bool narrow = b.width > 0 && b.width < kNarrowWidth;
    if (narrow != m_narrow) {
        m_narrow = narrow;
        const float target = (narrow || m_userCollapsed) ? 0.0f : 1.0f;
        m_navTarget = target;
        m_navExpansion.snapTo(target);
        m_nav->setExpansion(target);
    }
    const float navWidth =
        std::round(size::navCollapsed + (size::navExpanded - size::navCollapsed) * m_navExpansion.value());
    m_titleBar->setBounds({b.x, b.y, b.width, size::titleBar});
    m_status->setBounds({b.x, b.bottom() - size::statusBar, b.width, size::statusBar});
    const float top = b.y + size::titleBar;
    const float middle = std::max(b.bottom() - size::statusBar - top, 0.0f);
    m_nav->setBounds({b.x, top, navWidth, middle});
    const float inspectorWidth = inspectorVisible() ? size::inspector : 0.0f;
    if (m_pageView) {
        m_pageView->setBounds({b.x + navWidth, top, std::max(b.width - navWidth - inspectorWidth, 0.0f), middle});
    }
    if (m_inspector) {
        m_inspector->setBounds({b.right() - size::inspector, top, size::inspector, middle});
    }
    if (m_sideInspector) {
        m_sideInspector->setBounds({b.right() - size::inspector, top, size::inspector, middle});
    }
    if (m_toast) {
        m_toast->setBounds({b.right() - kToastMargin - ui::Toast::kWidth,
                            b.bottom() - size::statusBar - kToastMargin - ui::Toast::kHeight, ui::Toast::kWidth,
                            ui::Toast::kHeight});
    }
}

} // namespace wl::app
