#include "app/shell/Shell.h"

#include "app/pages/images/EditionDialog.h"
#include "app/pages/images/RenameDialog.h"
#include "app/pages/images/ToolDialogs.h"
#include "core/iso/IsoBuilder.h"
#include "core/image/dism/Edition.h"
#include "core/image/WindowsRelease.h"
#include "ui/platform/Clipboard.h"

#include "app/Format.h"
#include "app/pages/GalleryPage.h"
#include "app/Resources.h"
#include "app/pages/AboutPage.h"
#include "app/pages/ApplyPage.h"
#include "app/pages/ComponentsPage.h"
#include "app/pages/DriversPage.h"
#include "app/pages/ServicesPage.h"
#include "app/pages/RegistryPage.h"
#include "app/pages/StartMenuPage.h"
#include "app/pages/SettingsPage.h"
#include "app/pages/components/ComponentInspector.h"
#include "app/pages/FeaturesPage.h"
#include "app/pages/apply/RiskConfirm.h"
#include "app/pages/ImagesPage.h"
#include "app/pages/IsoPage.h"
#include "app/pages/LogsPage.h"
#include "app/pages/PostSetupPage.h"
#include "app/pages/ProgramsPage.h"
#include "app/pages/programs/ProgramInspector.h"
#include "app/pages/PresetsPage.h"
#include "app/ApplyReport.h"
#include "app/pages/postsetup/AppsDialog.h"
#include "app/pages/updates/UpdateCatalogDialog.h"
#include "app/pages/AppsPage.h"
#include "app/pages/IconsPage.h"
#include "app/pages/apps/StoreDialog.h"
#include "app/pages/LanguagesPage.h"
#include "app/pages/languages/LanguageAddDialog.h"
#include "app/pages/FilesPage.h"
#include "core/system/Privileges.h"
#include "app/pages/HostsPage.h"
#include "app/pages/BrandingPage.h"
#include "app/pages/TasksPage.h"
#include "app/pages/postsetup/StepDialog.h"
#include "app/pages/registry/ValueDialog.h"
#include "app/pages/SourcePage.h"
#include "app/pages/TweaksPage.h"
#include "app/pages/UnattendedPage.h"
#include "app/pages/UpdatesPage.h"
#include "app/pages/images/ImageInspector.h"
#include "app/shell/CommandPalette.h"
#include "base/File.h"
#include "base/Log.h"
#include "base/Path.h"
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UpdatePackage.h"
#include "core/image/dism/DismErrors.h"
#include "ui/platform/FileDialog.h"
#include "ui/render/Canvas.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"

#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace wl::app {

namespace size = ui::tokens::size;

namespace {

// Explorer, with the file selected.
void revealInExplorer(const std::filesystem::path& file) {
    const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

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
    LanguageController::setNameLanguage(language);
    auto s = [&](Str key) { return strings.get(key); };
    m_titleBar = &add<TitleBar>(TitleBar::Labels{s(Str::AppName), s(Str::TitleCmdk), s(Str::KbdCtrl), s(Str::TitleMinimize),
                                                 s(Str::TitleClose), s(Str::TitleMaximize), s(Str::TitleRestore)});
    m_nav = &add<NavRail>(NavRail::Labels{s(Str::NavCollapse), s(Str::NavExpand), s(Str::KbdCtrl),
                                          s(Str::TooltipsCollapseNav)},
                          [&](Str key) { return strings.get(key); });
    m_status = &add<StatusBar>(StatusBar::Labels{s(Str::StatusNoMount), s(Str::StatusMounted), s(Str::StatusImage),
                                                 s(Str::StatusApply)});
    m_status->cta().onInvoke = [this] {
        if (m_apply && m_apply->running()) {
            m_apply->cancel();
        } else if (m_page == PageId::Apply) {
            requestApply(); // already looking at the summary: the button starts the run, like the header's
        } else {
            showPage(PageId::Apply);
        }
    };

    m_titleBar->palette().onInvoke = [this] { openPalette(); };
    m_titleBar->minimizeButton().onInvoke = [this] { m_services.minimize(); };
    m_titleBar->maximizeButton().onInvoke = [this] { m_services.toggleMaximize(); };
    m_titleBar->closeButton().onInvoke = [this] { m_services.close(); };
    m_nav->onSelect = [this](PageId page) { showPage(page); };
    m_nav->onToggleCollapse = [this] { toggleNav(); };

    m_features = std::make_unique<FeatureController>(m_state, m_services.postToUi);
    {
        // Embedded in WinLove.exe; tests and tools without the resource get an empty catalog.
        auto catalog = AppxCatalog::parse(embeddedAppxCatalog());
        if (!catalog) {
            catalog = AppxCatalog::parse(R"({"format":"winlove.catalog.appx","groups":[],"apps":[]})");
        }
        // Without the resource (tools, tests): no system components, the apps still work.
        auto system = ComponentCatalog::parse(embeddedComponentsCatalog());
        m_components = std::make_unique<ComponentController>(m_state, std::move(*catalog), m_language, m_services.postToUi,
                                                             system ? std::move(*system) : ComponentCatalog{});
    }
    m_registry = std::make_unique<RegistryController>(m_state);
    {
        auto settings = ImageSettingsCatalog::parse(embeddedSettingsCatalog());
        if (!settings) {
            settings = ImageSettingsCatalog::parse(R"({"format":"winlove.catalog.settings"})");
        }
        m_imageSettings = std::make_unique<ImageSettingsController>(m_state, std::move(*settings));
    }
    m_serviceCtl = std::make_unique<ServiceController>(m_state, embeddedServiceCatalog(), m_services.postToUi);
    m_tasks = std::make_unique<TaskController>(m_state, embeddedTaskCatalog());
    m_hosts = std::make_unique<HostsController>(m_state, embeddedHostsCatalog());
    m_branding = std::make_unique<BrandingController>(m_state);
    m_icons = std::make_unique<IconController>(m_state);
    m_iconPatch = std::make_unique<IconPatchController>(m_state, m_services.postToUi);
    m_startPins = std::make_unique<StartPinsController>(m_state, m_services.postToUi);
    m_store = std::make_unique<StoreController>(m_state, StoreController::Events{
        m_services.postToUi,
        [this](const std::wstring& query, std::vector<core::StoreSearchResult> results) {
            if (m_storeDialog) {
                m_storeDialog->setResults(query, std::move(results));
            }
        },
        [this](const Error& e) {
            if (m_storeDialog) {
                m_storeDialog->setResults(L"", {});
            }
            showToast(ui::InfoKind::Error, m_strings.get(Str::AppsStoreFailed), errorText(e));
        },
        [this](std::wstring title, std::filesystem::path app) {
            if (!m_state.mounted()) {
                showToast(ui::InfoKind::Warning, m_strings.get(Str::AppsStoreDownloadedNoMount), app.parent_path().wstring());
                return;
            }
            m_apps->addPackages({app}); // the frameworks are next to it
            showToast(ui::InfoKind::Success, m_strings.format(Str::AppsStoreAdded, {{L"name", title}}), app.parent_path().wstring());
        },
        [this] { showToast(ui::InfoKind::Warning, m_strings.get(Str::AppsStoreStopped), L""); },
    });
    m_files = std::make_unique<FilesController>(m_state);
    m_languages = std::make_unique<LanguageController>(m_state, m_services.postToUi);
    m_languageFetch = std::make_unique<LanguageFetchController>(m_state, LanguageFetchController::Events{
        m_services.postToUi,
        [this](const LanguageTarget& target, const std::vector<core::UupLanguage>& languages) { showLanguageOffers(target, languages); },
        [this](const Error& e, bool download) {
            showToast(ui::InfoKind::Error, m_strings.get(download ? Str::LanguagesDownloadFailed : Str::LanguagesFindFailed), errorText(e));
        },
        [this](std::vector<std::filesystem::path> files) {
            const std::wstring n = std::to_wstring(files.size());
            const auto folder = files.empty() ? std::wstring() : files.front().parent_path().wstring();
            if (!m_state.mounted()) {
                showToast(ui::InfoKind::Warning, m_strings.format(Str::LanguagesDownloadedNoMount, {{L"n", n}}), folder);
                return;
            }
            m_languages->queueFiles(files, std::exchange(m_pendingUiLanguage, {}));
            showToast(ui::InfoKind::Success, m_strings.format(Str::LanguagesDownloaded, {{L"n", n}}), folder);
        },
        [this] { showToast(ui::InfoKind::Warning, m_strings.get(Str::LanguagesDownloadStopped), L""); },
    });
    m_apps = std::make_unique<AppsController>(m_state, AppsController::Events{
        m_services.postToUi,
        [this](int added, std::vector<Error> errors) {
            if (!errors.empty()) {
                showToast(ui::InfoKind::Warning, m_strings.get(Str::AppsAddFailed), errors.front().message + L" — " + errors.front().context);
            }
            if (added > 0) {
                showToast(ui::InfoKind::Success, m_strings.format(Str::AppsAdded, {{L"n", std::to_wstring(added)}}), L"");
            }
        },
        [this](int n) { showToast(ui::InfoKind::Success, m_strings.format(Str::AppsImported, {{L"n", std::to_wstring(n)}}), L""); },
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::AppsHostFailed), errorText(e)); },
    });
    m_imageDriverCtl = std::make_unique<ImageDriverController>(m_state, ImageDriverController::Events{
        m_services.postToUi,
        [this](int packages, const std::filesystem::path& folder) {
            showToast(ui::InfoKind::Success, m_strings.format(Str::DriversHostExported, {{L"n", std::to_wstring(packages)}}),
                      folder.wstring());
        },
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::DriversHostFailed), errorText(e)); },
    });
    m_unattend = std::make_unique<UnattendController>(m_state);
    m_postSetup = std::make_unique<PostSetupController>(m_state);
    {
        // Without the resource (tools, tests): no catalog, the search still works.
        auto catalog = ProgramCatalog::parse(embeddedProgramsCatalog());
        m_programs = std::make_unique<ProgramsController>(m_state, *m_postSetup, catalog ? std::move(*catalog) : ProgramCatalog{},
                                                          m_strings, m_language, ProgramsController::Events{m_services.postToUi});
    }
    m_presets = std::make_unique<PresetController>(m_state, m_imageSettings->catalog(), m_strings, m_language,
                                                   PresetController::defaultFolder());
    m_preload = std::make_unique<PreloadController>(m_state, m_services.postToUi);
    m_imageValues = std::make_unique<ImageValuesController>(
        m_state, ImageValueProbes::from(m_imageSettings->catalog()), m_services.postToUi);
    m_palette = std::make_unique<PaletteIndex>(PaletteIndex::Sources{
        m_state, m_strings, m_language, *m_imageSettings, *m_components, *m_features, *m_serviceCtl,
        [this](PaletteCommand command) { return paletteCommandAvailable(command); },
        [this] { return navCollapsed(); },
    });
    m_preload->onCancelled = [this] { showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesCancelledToast), L""); };
    m_iso =std::make_unique<IsoController>(m_state, IsoController::Events{
        m_services.postToUi,
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::IsoFailed), errorText(e)); },
        [this](const core::IsoResult& result, const std::filesystem::path& output, bool openFolder) {
            const bool usb = m_state.isoRun() && m_state.isoRun()->usb;
            if (usb) { // D-047: the stick's drive
                showToast(ui::InfoKind::Success, m_strings.get(Str::IsoUsbDone),
                          output.wstring() + L" \u00b7 " + formatBytes(result.bytes, m_language));
                if (openFolder) {
                    ShellExecuteW(nullptr, L"open", output.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                return;
            }
            showToast(ui::InfoKind::Success, m_strings.get(Str::IsoDone),
                      output.filename().wstring() + L" \u00b7 " + formatBytes(result.bytes, m_language));
            if (openFolder) {
                revealInExplorer(output);
            }
            // A repack rewrote the setup folder: re-read sizes when it is the open source.
            if (const auto& source = m_state.source(); source && source->format == core::ImageFormat::Folder) {
                openSource(source->path);
            }
        },
        [this](std::wstring args) { showAdminRequired(std::move(args)); },
    });
    m_updateCatalog = std::make_unique<UpdateCatalogController>(m_state, UpdateCatalogController::Events{
        m_services.postToUi,
        [this](const core::CatalogTarget& target, std::vector<core::CatalogOffer> offers) {
            showUpdateOffers(target, std::move(offers));
        },
        [this](const Error& e, bool download) {
            showToast(ui::InfoKind::Error, m_strings.get(download ? Str::UpdatesDownloadFailed : Str::UpdatesCatalogFailed),
                      errorText(e));
        },
        [this](std::vector<core::DownloadedUpdate> downloaded) {
            // D-080: the image's updates are queued; the Safe OS update goes to its WinRE; the Setup
            // update and the cumulative update are kept for the setup media (ISO Oluştur).
            std::vector<std::filesystem::path> packages;
            std::vector<std::filesystem::path> safeOs;
            auto media = m_state.mediaUpdate();
            for (const auto& d : downloaded) {
                if (d.kind == core::CatalogKind::Setup) {
                    media.setupDu = d.main;
                    continue;
                }
                if (d.kind == core::CatalogKind::Cumulative) {
                    media.lcu = d.main;
                }
                (d.kind == core::CatalogKind::SafeOs ? safeOs : packages).push_back(d.main); // a checkpoint stays next to it for DISM
            }
            m_state.setMediaUpdate(std::move(media));
            const std::wstring n = std::to_wstring(downloaded.size());
            if (!m_state.mounted()) {
                showToast(ui::InfoKind::Warning, m_strings.format(Str::UpdatesDownloadedNoMount, {{L"n", n}}),
                          m_updateCatalog->folder().wstring());
                return;
            }
            UpdatesPage::queuePackages(m_state, packages);
            UpdatesPage::queuePackages(m_state, safeOs, core::UpdateKind::SafeOs);
            showToast(ui::InfoKind::Success, m_strings.format(Str::UpdatesDownloaded, {{L"n", n}}),
                      m_updateCatalog->folder().wstring());
        },
        [this] { showToast(ui::InfoKind::Warning, m_strings.get(Str::UpdatesDownloadStopped), L""); },
    });
    m_apply = std::make_unique<ApplyController>(m_state, ApplyController::Events{
        m_services.postToUi,
        [this](core::SourceInfo source) {
            const auto index = m_state.selectedIndex();
            m_state.setSource(std::move(source)); // new sizes after the commit
            m_state.select(index);
        },
        [this](const Error& e) { showToast(ui::InfoKind::Error, m_strings.get(Str::ApplyFailedTitle), errorText(e)); },
        {},
    });
    m_images = std::make_unique<ImageController>(m_state, ImageController::Events{
        m_services.postToUi,
        [this](ImageController::Failure f, const Error& e) { onImageFailure(f, e); },
        [this](Str title, std::wstring detail) {
            showToast(ui::InfoKind::Success, m_strings.format(title, {{L"edition", detail}, {L"file", detail}}), L"");
        },
        [this](Str title) { showToast(ui::InfoKind::Warning, m_strings.get(title), L""); },
        [this](std::wstring args) { showAdminRequired(std::move(args)); },
        [this](std::filesystem::path source, MountedImage mounted) { restoreMount(source, std::move(mounted)); },
        [this] { startPreload(); },
        [this](const core::WimVerifyReport& report, std::wstring file) { onImageVerified(report, file); },
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
        if (change == AppState::Change::Settings && m_services.settingsChanged) {
            m_services.settingsChanged(); // theme, motion, language: the App applies them
        }
        if (change == AppState::Change::Apply || change == AppState::Change::Queue ||
            change == AppState::Change::Mount) {
            updateApplyChrome();
        }
        if (change == AppState::Change::Mount) {
            // A new mount reuses the mount folder: the icons drawn from its files (Simgeler, Başlat
            // menüsü) may have changed since, e.g. an icon pack applied and the edition mounted again.
            ui::Canvas::forgetFileIcons();
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

bool Shell::requireMount(Str title, Str body) {
    if (m_state.mounted()) {
        return true;
    }
    showToast(ui::InfoKind::Warning, m_strings.get(title), m_strings.get(body));
    return false;
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

ProgramsPage* Shell::programsPage() const {
    return m_page == PageId::Programs ? dynamic_cast<ProgramsPage*>(m_pageBody) : nullptr;
}

void Shell::updateProgramInspector() {
    auto* page = programsPage();
    auto* inspector = dynamic_cast<ProgramInspector*>(m_sideInspector);
    if (!page || !inspector) {
        return;
    }
    const auto program = page->selectedProgram();
    const core::WingetDetails* details = program ? m_programs->details(program->id) : nullptr;
    const auto icon = program ? m_programs->icon(program->id) : std::filesystem::path();
    const bool wasVisible = inspector->visible();
    inspector->set(program, details, icon, program && m_programs->picked(program->id),
                   program && m_programs->detailsFailed(program->id));
    inspector->setVisible(program.has_value());
    if (wasVisible != inspector->visible()) {
        layout();
    }
    invalidate();
}

UpdatesPage* Shell::updatesPage() const {
    return m_page == PageId::Updates ? dynamic_cast<UpdatesPage*>(m_pageBody) : nullptr;
}

StartMenuPage* Shell::startMenuPageForDemo() const {
    return m_page == PageId::StartMenu ? dynamic_cast<StartMenuPage*>(m_pageBody) : nullptr;
}

IconsPage* Shell::iconsPageForDemo() const {
    return m_page == PageId::Icons ? dynamic_cast<IconsPage*>(m_pageBody) : nullptr;
}

RegistryPage* Shell::registryPage() const {
    return m_page == PageId::Registry ? dynamic_cast<RegistryPage*>(m_pageBody) : nullptr;
}

void Shell::importRegFiles(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    if (!requireMount(Str::RegistryNoMountTitle, Str::RegistryNoMountBody)) {
        return;
    }
    struct Parsed {
        std::filesystem::path file;
        Result<std::vector<core::RegistryWrite>> writes;
    };
    auto parsed = std::make_shared<std::vector<Parsed>>();
    readThenUi<bool>(
        [files, parsed](const core::TaskContext&) -> Result<bool> {
            for (const auto& f : files) {
                parsed->push_back({f, core::readRegFile(f)});
            }
            return true;
        },
        [this, parsed](Result<bool>) {
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
        });
}

void Shell::editRegistryValue(std::optional<std::size_t> entry) {
    if (!host()) {
        return;
    }
    if (!requireMount(Str::RegistryNoMountTitle, Str::RegistryNoMountBody)) {
        return;
    }
    std::optional<core::RegistryWrite> initial;
    bool afterSetup = true;
    if (entry) {
        const auto& entries = m_state.regImports();
        if (*entry >= entries.size() || !entries[*entry].typed || entries[*entry].writes.empty()) {
            return;
        }
        initial = entries[*entry].writes.front();
        afterSetup = entries[*entry].afterSetup;
    }
    const ModalSlot slot = modalSlot();
    ValueDialogActions actions;
    actions.close = slot.close;
    actions.accept = [this, entry](core::RegistryWrite write, bool later) {
        if (entry) {
            m_registry->replaceValue(*entry, std::move(write), later);
        } else {
            m_registry->addValue(std::move(write), later);
        }
    };
    ValueDialog built = makeValueDialog(m_strings, initial, afterSetup, std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::editPostSetupStep(core::PostSetupStep::Type type, std::optional<std::size_t> index) {
    if (!host()) {
        return;
    }
    // The queue belongs to a mounted image (the next mount would silently clear it).
    if (!requireMount(Str::PostsetupNoMountTitle, Str::PostsetupNoMountBody)) {
        return;
    }
    core::PostSetupStep step;
    step.type = type;
    if (index) {
        step = m_postSetup->plan().steps[*index];
    }
    const ModalSlot slot = modalSlot();
    StepDialogActions actions;
    actions.pickFile = [this]() -> std::optional<std::filesystem::path> {
        return ui::pickFile(owner(), m_strings.get(Str::PostsetupPickTitle), {{m_strings.get(Str::SourceFilterAll), L"*.*"}});
    };
    actions.pickFolder = [this]() -> std::optional<std::filesystem::path> {
        return ui::pickFolder(owner(), m_strings.get(Str::PostsetupPickTitle));
    };
    actions.close = slot.close;
    actions.accept = [this, index](core::PostSetupStep done) {
        if (index) {
            m_postSetup->replace(*index, std::move(done));
        } else {
            m_postSetup->add(std::move(done));
        }
    };
    StepDialog built = makeStepDialog(m_strings, std::move(step), index.has_value(), std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::pickPostSetupCommands() {
    if (!host()) {
        return;
    }
    if (!requireMount(Str::PostsetupNoMountTitle, Str::PostsetupNoMountBody)) {
        return;
    }
    const ModalSlot slot = modalSlot();
    AppsDialogActions actions;
    actions.present = [this](std::size_t index) { return m_postSetup->hasCommand(index); };
    actions.close = slot.close;
    actions.accept = [this](std::vector<std::size_t> picked) {
        const std::size_t added = m_postSetup->addCommands(picked, m_language);
        showToast(ui::InfoKind::Success, m_strings.format(Str::PostsetupCommandsAdded, {{L"n", std::to_wstring(added)}}), L"");
    };
    CatalogDialogSpec spec{m_strings.get(Str::PostsetupCommands), m_strings.get(Str::PostsetupCommandsBody),
                           m_strings.get(Str::PostsetupCommand), Str::PostsetupCommandsAdd, commandRows(m_strings, m_language),
                           m_strings.get(Str::PostsetupName)};
    AppsDialog built = makeCatalogDialog(m_strings, std::move(spec), std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::importAnswerFile() {
    const auto file = ui::pickFile(owner(), m_strings.get(Str::UnattendedImportXml),
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
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::UnattendedSaveXml),
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
    const auto folder = ui::pickFolder(owner(), m_strings.get(Str::UpdatesScanFolder));
    if (!folder) {
        return;
    }
    showToast(ui::InfoKind::Info, m_strings.get(Str::DriversScanning), folder->wstring());
    auto infs = std::make_shared<std::vector<core::DriverInf>>();
    readThenUi<bool>(
        [path = *folder, infs](const core::TaskContext&) -> Result<bool> {
            *infs = core::scanDrivers(path);
            return true;
        },
        [this, path = *folder, infs](Result<bool>) {
            if (infs->empty()) {
                showToast(ui::InfoKind::Warning, m_strings.get(Str::DriversNoneFound), path.wstring());
                return;
            }
            const std::size_t n = infs->size();
            m_state.addDriverScan(path, std::move(*infs));
            showToast(ui::InfoKind::Success, m_strings.format(Str::DriversScanned, {{L"n", std::to_wstring(n)}}),
                      path.wstring());
        });
}

void Shell::addUpdates(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    // The queue belongs to a mounted image (the next mount would silently clear it).
    if (!requireMount(Str::UpdatesNoMountTitle, Str::UpdatesNoMountBody)) {
        return;
    }
    const std::size_t added = UpdatesPage::queuePackages(m_state, files);
    if (added > 0) {
        showToast(ui::InfoKind::Success, m_strings.format(Str::UpdatesAdded, {{L"n", std::to_wstring(added)}}), L"");
    }
}

void Shell::findUpdates() {
    // The queue belongs to a mounted image, and the image says which updates fit.
    if (!requireMount(Str::UpdatesNoMountTitle, Str::UpdatesNoMountBody)) {
        return;
    }
    if (m_updateCatalog->busy()) {
        return;
    }
    const auto target = UpdateCatalogController::targetFor(m_state);
    if (!target || target->release.empty()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::UpdatesNoTarget),
                  target ? std::format(L"{}.{}", target->build, target->revision) : std::wstring());
        return;
    }
    m_updateCatalog->search();
}

void Shell::showUpdateOffers(const core::CatalogTarget& target, std::vector<core::CatalogOffer> offers) {
    if (offers.empty()) {
        showToast(ui::InfoKind::Info, m_strings.get(Str::UpdatesCatalogNone),
                  std::format(L"Windows {} {} {}", target.windows, target.release, target.architecture));
        return;
    }
    if (!host()) {
        return;
    }
    const ModalSlot slot = modalSlot();
    UpdateCatalogActions actions;
    actions.close = slot.close;
    actions.download = [this](std::vector<core::CatalogEntry> entries) { m_updateCatalog->download(std::move(entries)); };
    UpdateCatalogDialog built = makeUpdateCatalogDialog(m_strings, m_language, target, std::move(offers), std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
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
    const auto file = ui::pickFile(owner(), m_strings.get(Str::ComponentsLoadPreset),
                                   {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}});
    if (!file) {
        return;
    }
    const auto preset = readPreset(*file);
    if (!preset) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::ComponentsPresetFailed), errorText(preset.error()));
        return;
    }
    // The same path as the Presets page: a preset that only carries answers needs no mount.
    applyPreset(*preset);
}

PresetsPage* Shell::presetsPage() const {
    return m_page == PageId::Presets ? dynamic_cast<PresetsPage*>(m_pageBody) : nullptr;
}

void Shell::applyPreset(const Preset& preset) {
    if (!preset.changes.empty() && !m_state.mounted()) {
        // The queue belongs to a mounted image.
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ComponentsPresetNeedsMount), L"");
        return;
    }
    Preset current = preset;
    current.changes = m_components->withCurrentRecipes(preset.changes);
    const std::size_t queued = m_presets->apply(current);
    showToast(ui::InfoKind::Success, m_strings.format(Str::PresetsLoaded, {{L"n", std::to_wstring(queued)}}), preset.name);
}

void Shell::importPreset() {
    const auto file = ui::pickFile(owner(), m_strings.get(Str::CommonImport),
                                   {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}});
    if (!file) {
        return;
    }
    if (auto r = m_presets->importFile(*file); !r) {
        log::error("app", describe(r.error()));
        showToast(ui::InfoKind::Error, m_strings.get(Str::PresetsFailed), errorText(r.error()));
        return;
    }
    showToast(ui::InfoKind::Success, m_strings.get(Str::PresetsImported), file->filename().wstring());
    if (auto* page = presetsPage()) {
        page->reloadList();
    }
}

void Shell::addTaskDialog() {
    if (!host()) {
        return;
    }
    if (!requireMount(Str::TasksNoMountTitle, Str::TasksNoMountBody)) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::TasksAddTitle), m_strings.get(Str::TasksAddBody),
                                               ui::icons::Icon::QueueClock, ui::tokens::Color::TextSecondary, 520.0f);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow, 64.0f);
    auto& path = form.addRow<ui::SearchBox>(m_strings.get(Str::TasksAddPath), std::wstring(), 400.0f, std::wstring());
    path.setPlain(true);
    auto add = [this, raw, &path] {
        const std::wstring text = path.text();
        if (!m_tasks->addCustom(text)) {
            showToast(ui::InfoKind::Warning, m_strings.get(Str::TasksAddInvalid), text);
            return;
        }
        host()->popModal(raw);
    };
    path.onSubmit = add;
    raw->onCancel = closer(raw);
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonAdd), add, /*primary=*/true);
    host()->pushModal(std::move(dialog), &path);
}

void Shell::addFonts(std::vector<std::filesystem::path> files) {
    if (files.empty()) {
        return;
    }
    if (!requireMount(Str::BrandingNoMountTitle, Str::BrandingNoMountBody)) {
        return;
    }
    std::vector<std::wstring> refused;
    m_branding->addFonts(files, &refused);
    if (!refused.empty()) {
        std::wstring names;
        for (const auto& name : refused) {
            names += (names.empty() ? L"" : L", ") + name;
        }
        showToast(ui::InfoKind::Warning, m_strings.get(Str::BrandingNotFont), names);
    }
}

void Shell::importHostsFile() {
    if (!requireMount(Str::HostsNoMountTitle, Str::HostsNoMountBody)) {
        return;
    }
    const auto file = ui::pickFile(owner(), m_strings.get(Str::HostsImport),
                                   {{m_strings.get(Str::HostsFilter), L"hosts;*.txt;*.hosts;*"}});
    if (!file) {
        return;
    }
    auto read = readFileBytes(*file);
    if (!read) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::HostsImport), errorText(read.error()));
        return;
    }
    std::string bytes = std::move(*read);
    if (bytes.size() > (64u << 20)) {
        bytes.resize(64u << 20);
    }
    const std::size_t added = m_hosts->importText(utf8::toWide(bytes));
    showToast(added > 0 ? ui::InfoKind::Success : ui::InfoKind::Info,
              added > 0 ? m_strings.format(Str::HostsImported, {{L"n", std::to_wstring(added)}})
                        : m_strings.get(Str::HostsImportedNone),
              file->filename().wstring());
}

void Shell::addLanguages() {
    if (!requireMount(Str::LanguagesNoMountTitle, Str::LanguagesNoMountBody)) {
        return;
    }
    if (m_languageFetch->busy()) {
        showToast(ui::InfoKind::Info, m_strings.get(Str::LanguagesBusy), L"");
        return;
    }
    if (!LanguageFetchController::targetFor(m_state)) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::LanguagesNoTarget), L"");
        return;
    }
    m_languageFetch->find();
}

void Shell::showLanguageOffers(const LanguageTarget& target, const std::vector<core::UupLanguage>& languages) {
    if (!host() || !m_state.mounted()) {
        return;
    }
    const ModalSlot slot = modalSlot();
    LanguageAddActions actions;
    actions.close = slot.close;
    actions.download = [this](std::vector<core::UupLanguageFile> files, std::wstring uiLanguage) {
        m_pendingUiLanguage = std::move(uiLanguage);
        m_languageFetch->download(std::move(files));
    };
    LanguageAddDialog built = makeLanguageAddDialog(m_strings, m_language, *m_languages, target, languages, std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::showStoreDialog() {
    if (!requireMount(Str::AppsNoMountTitle, Str::AppsNoMountBody)) {
        return;
    }
    if (m_store->busy()) {
        showToast(ui::InfoKind::Info, m_strings.get(Str::AppsStoreBusy), L"");
        return;
    }
    if (!host()) {
        return;
    }
    const ModalSlot slot = modalSlot();
    StoreDialogActions actions;
    actions.close = [this, close = slot.close] {
        m_storeDialog = nullptr;
        close();
    };
    actions.search = [this](std::wstring query) {
        if (m_storeDialog) {
            m_storeDialog->setSearching(true);
        }
        m_store->search(std::move(query));
    };
    actions.install = [this](core::StoreSearchResult app) { m_store->install(std::move(app)); };
    StoreDialog built = makeStoreDialog(m_strings, m_store->architecture(), std::move(actions));
    m_storeDialog = built.handle;
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::storeResultsForDemo(const std::wstring& query, std::vector<core::StoreSearchResult> results) {
    if (m_storeDialog) {
        m_storeDialog->setResults(query, std::move(results));
    }
}

void Shell::showIconPackMenu(ui::RectF anchor) {
    if (!host()) {
        return;
    }
    auto popup = std::make_unique<ui::MenuPopup>(
        anchor, std::vector<std::wstring>{m_strings.get(Str::IconsPackFromArchive), m_strings.get(Str::IconsPackFromFolder)}, -1,
        [this](int picked) {
            if (picked == 0 || picked == 1) {
                loadIconPack(picked == 0);
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false);
}

void Shell::loadIconPack(bool archive) {
    if (!requireMount(Str::IconsNoMountTitle, Str::IconsNoMountBody)) {
        return;
    }
    const auto folder = archive ? ui::pickFile(owner(), m_strings.get(Str::IconsLoadPack),
                                               {{m_strings.get(Str::IconsPackArchives), L"*.7z;*.zip;*.rar"}})
                                : ui::pickFolder(owner(), m_strings.get(Str::IconsLoadPack));
    if (!folder) {
        return;
    }
    // One folder may carry both kinds (D-065 slots, D-068 files): each takes what is its. An
    // archive is a 7TSP pack or a D-068 pack (importPack keeps it under the pack root).
    auto pack = archive ? Result<IconController::PackResult>(std::unexpected(Error{ErrorCode::NotFound, L"", L""}))
                        : m_icons->applyPack(*folder);
    auto files = m_iconPatch->importPack(*folder);
    if (!pack && !files && archive) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::IconsPackFailed), errorText(files.error()));
        return;
    }
    if (!pack && !files) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::IconsPackFailed), errorText(pack.error()));
        return;
    }
    const int slots = pack ? pack->matched : 0;
    const int patched = files ? files->matched : 0;
    if (slots == 0 && patched == 0) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::IconsPackNone), folder->wstring());
        return;
    }
    std::wstring detail = files && patched > 0 ? files->name : pack ? pack->name : folder->filename().wstring();
    const std::size_t unmatched = (pack ? pack->unmatched.size() : 0) + (files ? files->unmatched.size() : 0);
    if (unmatched > 0) {
        detail += L" \u00b7 " + m_strings.format(Str::IconsPackUnmatched, {{L"n", std::to_wstring(unmatched)}});
    }
    std::wstring title;
    if (slots > 0) {
        title = m_strings.format(Str::IconsPackLoaded, {{L"n", std::to_wstring(slots)}});
    }
    if (patched > 0) {
        title += (title.empty() ? L"" : L" \u00b7 ") + m_strings.format(Str::IconsPackPatchLoaded, {{L"n", std::to_wstring(patched)}});
    }
    showToast(ui::InfoKind::Success, title, detail);
}

void Shell::exportIconPack() {
    if (m_iconPatch->changedCount() == 0) {
        showToast(ui::InfoKind::Info, m_strings.get(Str::IconsExportNothing), L"");
        return;
    }
    const auto folder = ui::pickFolder(owner(), m_strings.get(Str::IconsExportPack));
    if (!folder) {
        return;
    }
    auto written = m_iconPatch->exportPack(*folder);
    if (!written) {
        showToast(ui::InfoKind::Error, m_strings.get(Str::IconsExportPack), errorText(written.error()));
        return;
    }
    showToast(ui::InfoKind::Success, m_strings.format(Str::IconsExportedPack, {{L"n", std::to_wstring(*written)}}), folder->wstring());
}

void Shell::scanLanguageFolder() {
    if (!requireMount(Str::LanguagesNoMountTitle, Str::LanguagesNoMountBody)) {
        return;
    }
    const auto folder = ui::pickFolder(owner(), m_strings.get(Str::LanguagesScan));
    if (!folder) {
        return;
    }
    auto found = std::make_shared<std::vector<core::LanguagePackFile>>();
    readThenUi<bool>(
        [path = *folder, found](const core::TaskContext&) -> Result<bool> {
            *found = core::scanLanguageFiles(path);
            return true;
        },
        [this, found, path = *folder](Result<bool>) {
            if (!host()) {
                return;
            }
            auto files = std::make_shared<std::vector<core::LanguagePackFile>>(m_languages->fitting(*found));
            if (files->empty()) {
                showToast(ui::InfoKind::Warning, m_strings.get(Str::LanguagesScanNone), path.wstring());
                return;
            }
            auto page = LanguagesPage::kindLabel; // kind names as the page shows them
            std::vector<CatalogRow> rows;
            for (const auto& f : *files) {
                rows.push_back({f.language.empty() ? std::wstring(L"—")
                                                   : LanguageController::localeName(f.language) + L"  \u00b7  " + f.language,
                                page(m_strings, f.kind), f.path.filename().wstring()});
            }
            const ModalSlot slot = modalSlot();
            AppsDialogActions actions;
            actions.present = [this, files](std::size_t i) { return m_languages->queued((*files)[i]); };
            actions.close = slot.close;
            actions.accept = [this, files](std::vector<std::size_t> picked) {
                std::vector<core::LanguagePackFile> chosen;
                for (const auto i : picked) {
                    chosen.push_back((*files)[i]);
                }
                chosen = m_languages->withDependencies(std::move(chosen), *files); // Speech takes Basic + TTS, …
                m_languages->queuePacks(chosen);
                showToast(ui::InfoKind::Success, m_strings.format(Str::LanguagesAdded, {{L"n", std::to_wstring(chosen.size())}}), L"");
            };
            CatalogDialogSpec spec{m_strings.get(Str::LanguagesPickTitle),
                                   m_strings.format(Str::LanguagesPickBody, {{L"arch", m_languages->imageArchitecture()}}),
                                   m_strings.get(Str::LanguagesPickFile), Str::LanguagesPickAdd, std::move(rows),
                                   m_strings.get(Str::LanguagesPickName)};
            AppsDialog built = makeCatalogDialog(m_strings, std::move(spec), std::move(actions));
            showModal(slot, std::move(built.dialog), built.initialFocus);
        });
}

AppsPage* Shell::appsPage() const {
    return m_page == PageId::Apps ? dynamic_cast<AppsPage*>(m_pageBody) : nullptr;
}

void Shell::pickAppPackages() {
    if (!requireMount(Str::AppsNoMountTitle, Str::AppsNoMountBody)) {
        return;
    }
    auto files = ui::pickFiles(owner(), m_strings.get(Str::AppsAddPackage),
                               {{m_strings.get(Str::AppsPackageFiles), L"*.appx;*.msix;*.appxbundle;*.msixbundle"}});
    if (!files.empty()) {
        m_apps->addPackages(std::move(files));
    }
}

FilesPage* Shell::filesPage() const {
    return m_page == PageId::Files ? dynamic_cast<FilesPage*>(m_pageBody) : nullptr;
}

void Shell::addFilesTo(std::vector<std::filesystem::path> sources) {
    if (!host() || sources.empty()) {
        return;
    }
    if (!requireMount(Str::FilesNoMountTitle, Str::FilesNoMountBody)) {
        return;
    }
    std::wstring what = sources.size() == 1 ? sources.front().filename().wstring()
                                            : m_strings.format(Str::FilesItemsN, {{L"n", std::to_wstring(sources.size())}});
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::FilesWhereTitle),
                                               m_strings.format(Str::FilesWhereBody, {{L"what", what}}),
                                               ui::icons::Icon::Folder, ui::tokens::Color::TextSecondary, 560.0f);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * 2, 96.0f);
    std::vector<std::wstring> places;
    for (const auto& place : FilesController::places()) {
        places.push_back(FilesController::displayPath(place.folder) + L"  \u00b7  " + m_strings.get(place.label));
    }
    auto& preset = form.addRow<ui::Dropdown>(m_strings.get(Str::FilesPlace), std::wstring(), 420.0f, std::wstring(),
                                             std::move(places), 0);
    auto& path = form.addRow<ui::SearchBox>(m_strings.get(Str::FilesTarget), std::wstring(), 420.0f, std::wstring());
    path.setPlain(true);
    path.setText(FilesController::displayPath(FilesController::places().front().folder));
    preset.onChange = [&path](int index) {
        const auto& all = FilesController::places();
        if (index >= 0 && index < static_cast<int>(all.size())) {
            path.setText(FilesController::displayPath(all[static_cast<std::size_t>(index)].folder));
        }
    };
    auto sourcesPtr = std::make_shared<std::vector<std::filesystem::path>>(std::move(sources));
    auto add = [this, raw, &path, sourcesPtr] {
        const std::wstring folder = FilesController::imageFolderFrom(path.text());
        if (folder == L"?") {
            showToast(ui::InfoKind::Warning, m_strings.get(Str::FilesNotSystemDrive), path.text());
            return;
        }
        const auto [n, errors] = m_files->add(*sourcesPtr, folder);
        host()->popModal(raw); // `path` is gone from here on
        if (!errors.empty()) {
            showToast(ui::InfoKind::Warning, m_strings.get(Str::FilesSomeRefused), errors.front().message + L" — " + errors.front().context);
        } else {
            showToast(ui::InfoKind::Success, m_strings.format(Str::FilesAdded, {{L"n", std::to_wstring(n)}}),
                      FilesController::displayPath(folder));
        }
    };
    path.onSubmit = add;
    raw->onCancel = closer(raw);
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonAdd), add, /*primary=*/true);
    host()->pushModal(std::move(dialog), &path);
}

void Shell::newPreset() {
    if (!host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::PresetsNew), m_strings.get(Str::PresetsNewBody),
                                               ui::icons::Icon::PresetBookmark, ui::tokens::Color::TextSecondary);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow, 96.0f);
    auto& name = form.addRow<ui::SearchBox>(m_strings.get(Str::PresetsName), std::wstring(), 300.0f, std::wstring());
    name.setPlain(true);
    // A name to start from: the mounted edition, else the source file.
    if (const auto& mounted = m_state.mounted()) {
        name.setText(mounted->edition);
    } else if (const auto& source = m_state.source()) {
        name.setText(source->path.stem().wstring());
    }
    auto save = [this, raw, &name] {
        const std::wstring chosen = name.text();
        if (chosen.empty()) {
            return;
        }
        host()->popModal(raw); // `name` is gone from here on
        if (auto r = m_presets->saveCurrent(chosen); !r) {
            log::error("app", describe(r.error()));
            showToast(ui::InfoKind::Error, m_strings.get(Str::PresetsFailed), errorText(r.error()));
            return;
        }
        showToast(ui::InfoKind::Success, m_strings.get(Str::PresetsSaved), chosen);
        if (auto* page = presetsPage()) {
            page->reloadList();
        }
    };
    name.onSubmit = save;
    raw->onCancel = closer(raw);
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonSave), save, /*primary=*/true);
    host()->pushModal(std::move(dialog), &name);
}

void Shell::updateIsoChrome() {
    if (!m_actionIso) {
        return;
    }
    const bool running = m_iso->running();
    const auto* page = isoPage();
    m_actionIso->setText(m_strings.get(running                   ? Str::IsoCancel
                                       : page && page->usbTab() ? Str::IsoWriteUsb
                                                                : Str::IsoBuild));
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
    if (request.usb) {
        confirmUsbWrite(request);
        return;
    }
    m_state.setIsoFolder(request.output.parent_path());
    m_iso->start(request);
}

void Shell::confirmUsbWrite(IsoController::Request request) {
    // D-047: the drive is named in full before anything on it is erased.
    const auto* page = isoPage();
    const auto* disk = page ? page->selectedDisk() : nullptr;
    if (!disk || !host()) {
        return;
    }
    std::wstring letters;
    for (const auto& l : disk->letters) {
        letters += L", " + l.substr(0, 2);
    }
    const std::wstring body = m_strings.format(
        Str::IsoUsbConfirmBody,
        {{L"disk", disk->name()}, {L"size", formatBytes(disk->size, m_language)}, {L"letters", letters}});
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::IsoUsbConfirmTitle), body, ui::icons::Icon::UsbDrive,
                                               ui::tokens::Color::StatusWarning, 460.0f);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    // Not the Enter button: erasing a drive takes a click on its own name.
    raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::IsoUsbConfirmAction),
                   [this, raw, request = std::move(request)] {
                       host()->popModal(raw);
                       m_iso->start(request);
                   });
    pushDialog(std::move(dialog));
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
    const auto risky = m_apply->highRisk();
    std::vector<RiskConfirm::Item> items = RiskConfirm::itemsFor(m_state, m_imageSettings->catalog(), m_strings, m_language, risky);
    // Irreversible removals, high-risk settings, or both: the title, text and button say which.
    const bool removals = std::ranges::any_of(items, &RiskConfirm::Item::removal);
    const bool settings = std::ranges::any_of(items, [](const RiskConfirm::Item& i) { return !i.removal; });
    auto dialog = std::make_unique<ui::Dialog>(
        m_strings.get(settings ? Str::ApplyConfirmTitleRisky : Str::ApplyConfirmTitle),
        m_strings.get(!settings ? Str::ApplyConfirmBody : removals ? Str::ApplyConfirmBodyMixed : Str::ApplyConfirmBodySettings),
        ui::icons::Icon::ErrorOctagon, ui::tokens::Color::StatusError);
    ui::Dialog* raw = dialog.get();
    const std::size_t count = items.size();
    auto& content = raw->setContent<RiskConfirm>(RiskConfirm::heightFor(count), std::move(items),
                                                 m_strings.get(Str::ApplyConfirmAck));
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    ui::Button& go = raw->addButton(ui::ButtonKind::Danger, m_strings.get(removals ? Str::ApplyConfirmGo : Str::ApplyConfirmGoSettings),
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
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ApplyPresetTitle),
                                         {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}},
                                         L"WinLove.wlpreset", L"wlpreset");
    if (!target) {
        return;
    }
    // A whole preset, like the library's: the answer file and the boot.wim drivers go with the
    // queue (a bare ChangeSet file lost them). `changes`: the queue, or the finished run's.
    Preset preset = presetFromState(m_state, target->stem().wstring());
    preset.changes = changes;
    const auto written = writePreset(*target, preset);
    showToast(written ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(written ? Str::ApplyPresetSaved : Str::ApplySaveFailed),
              written ? target->wstring() : written.error().message);
}

void Shell::saveApplyLog() {
    const auto& run = m_state.applyRun();
    const auto buffer = m_state.logBuffer();
    if (!run || !buffer) {
        return;
    }
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ApplySaveLog),
                                         {{m_strings.get(Str::LogsLogFiles), L"*.log;*.txt"}}, L"WinLove-apply.log", L"log");
    if (!target) {
        return;
    }
    std::uint64_t version = run->logVersion;
    std::wstring text;
    for (const auto& e : buffer->since(version)) {
        text += log::formatLine(e) + L"\r\n";
    }
    const bool saved = writeFileAtomic(*target, utf8::fromWide(text)).has_value();
    showToast(saved ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(saved ? Str::ApplyLogSaved : Str::ApplySaveFailed), target->wstring());
}

void Shell::saveApplyReport() {
    const auto& run = m_state.applyRun();
    if (!run) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ApplySaveReport),
                                         {{m_strings.get(Str::ApplyReportFiles), L"*.html"}}, applyReportFileName(now), L"html");
    if (!target) {
        return;
    }
    const bool saved = writeFileAtomic(*target, applyReportHtml(m_state, *run, m_strings, m_language, now)).has_value();
    showToast(saved ? ui::InfoKind::Success : ui::InfoKind::Error,
              m_strings.get(saved ? Str::ApplyReportSaved : Str::ApplySaveFailed), target->wstring());
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
    // Updates: packages that are not language packs (those count on Diller).
    m_nav->setBadge(PageId::Updates, static_cast<int>(std::ranges::count_if(changes.operations(), [](const core::ops::Operation& op) {
                        return op.kind == core::ops::OpKind::AddPackage && op.value != L"language";
                    })));
    m_nav->setBadge(PageId::Drivers, static_cast<int>(changes.count(core::ops::OpKind::AddDriver) +
                                                      changes.count(core::ops::OpKind::RemoveDriver)));
    m_nav->setBadge(PageId::Services, static_cast<int>(changes.count(core::ops::OpKind::SetServiceStart)));
    m_nav->setBadge(PageId::Registry, m_registry->checkedCount());
    m_nav->setBadge(PageId::Tweaks, m_imageSettings->changedCount());
    m_nav->setBadge(PageId::StartMenu, m_startPins->changedCount());
    m_nav->setBadge(PageId::PostSetup, static_cast<int>(m_postSetup->stepCount()));
    m_nav->setBadge(PageId::Programs, static_cast<int>(m_programs->pickCount()));
    m_nav->setBadge(PageId::Tasks, m_tasks->changedCount());
    m_nav->setBadge(PageId::Hosts, m_hosts->changedCount());
    m_nav->setBadge(PageId::Branding, m_branding->changedCount());
    m_nav->setBadge(PageId::Icons, m_icons->changedCount() + m_iconPatch->changedCount());
    m_nav->setBadge(PageId::Files, m_files->count());
    m_nav->setBadge(PageId::Languages, m_languages->changedCount());
    m_nav->setBadge(PageId::Apps, m_apps->appCount() + static_cast<int>(changes.count(core::ops::OpKind::SetDefaultApps)));
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
        const Str label = op->kind == EngineOperation::Kind::Mounting    ? Str::StatusMounting
                          : op->kind == EngineOperation::Kind::Reading   ? Str::StatusReading
                          : op->kind == EngineOperation::Kind::Verifying ? Str::StatusVerifying
                                                                         : Str::StatusWorking;
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
        m_actionMount->setTooltip(m_images->isPackedSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{});
    }
    if (m_actionExport) {
        m_actionExport->setEnabled(!busy && image != nullptr);
    }
    if (m_actionVerify) {
        const auto refusal = m_images->verifyRefusal();
        m_actionVerify->setEnabled(!refusal);
        m_actionVerify->setTooltip(m_strings.get(refusal && !busy ? *refusal : Str::ImagesVerifyHint));
    }
    if (m_actionTools) {
        const auto refusal = m_images->toolsRefusal();
        m_actionTools->setEnabled(!refusal);
        m_actionTools->setTooltip(refusal && !busy ? m_strings.get(*refusal) : m_strings.get(Str::ImagesToolsHint));
    }
    if (m_actionEsd) {
        m_actionEsd->setEnabled(!busy && m_images->isPackedSource());
        m_actionEsd->setTooltip(m_images->isPackedSource() ? std::wstring{} : m_strings.get(Str::ImagesEsdOnly));
    }
    if (m_inspector) {
        // A disabled button's tooltip says why.
        const auto deleteRefusal = m_images->deleteRefusal();
        const auto editRefusal = m_images->editRefusal();
        const int marked = static_cast<int>(m_state.selection().size());
        ImageInspector::State inspector;
        inspector.source = m_state.source() ? &*m_state.source() : nullptr;
        inspector.image = image;
        inspector.mountedHere = mountedHere;
        inspector.canMount = image && m_images->canMount();
        inspector.canDelete = image && !deleteRefusal;
        inspector.canRename = image && !editRefusal && marked == 1;
        inspector.marked = marked;
        inspector.mountTooltip = m_images->isPackedSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{};
        inspector.deleteTooltip = deleteRefusal ? m_strings.get(*deleteRefusal) : std::wstring{};
        inspector.renameTooltip = editRefusal ? m_strings.get(*editRefusal) : std::wstring{};
        inspector.canUpgrade = mountedHere && !busy;
        inspector.upgradeTooltip = m_strings.get(Str::ImagesUpgradeHint);
        if (const auto* queued = m_state.changes().find(core::ops::OpKind::SetEdition, L"edition"); queued && image) {
            inspector.upgradeQueued = m_strings.format(
                Str::ImagesUpgradeQueued, {{L"edition", core::editionDisplayName(queued->value, image->build)}});
        }
        m_inspector->set(std::move(inspector));
        m_inspector->setVisible(inspectorVisible());
    }
    layout();
    invalidate();
}

void Shell::showPage(PageId page) {
    // Every rebuild stops it; the Logs page and a running Apply page start it again below. (An Apply
    // page rebuilt from Running to Done is the same page and must stop polling too.)
    if (m_services.stopTimer) {
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
    m_actionMount = m_actionExport = m_actionEsd = m_actionVerify = m_actionTools = nullptr;
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
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceCaptureAction), ui::icons::Icon::ImageWim)
                .onInvoke = [this] { askCapture(); };
            m_pageBody = &m_pageView->setBody<SourcePage>(
                m_state, m_strings, m_language,
                SourcePage::Intents{[this] { pickSourceFile(); },
                                    [this](const std::filesystem::path& p) { openSource(p); },
                                    [this](const std::filesystem::path& p) { removeSource(p); },
                                    [](const std::filesystem::path& p) {
                                        revealInExplorer(p);
                                    },
                                    [this](const std::filesystem::path& p) { verifyHash(p); },
                                    [this](const std::filesystem::path& folder) { m_images->adoptMount(folder); },
                                    [this](const std::filesystem::path& folder, const std::wstring& edition) {
                                        askDiscardMount(folder, edition);
                                    }});
            m_images->scanSystemMounts(); // D-064: what other tools mounted, read again each visit
        } else if (page == PageId::Images) {
            if (m_state.source()) {
                m_actionTools = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesTools),
                                                       ui::icons::Icon::MoreHorizontal);
                m_actionTools->onInvoke = [this] { showImageTools(); };
                m_actionEsd = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesEsdToWim));
                m_actionEsd->onInvoke = [this] { convertEsd(); };
                m_actionVerify = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesVerify),
                                                        ui::icons::Icon::ShieldCheck);
                m_actionVerify->onInvoke = [this] { m_images->verify(); };
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
            auto& images = *static_cast<ImagesPage*>(m_pageBody);
            images.onContinueMount = [this] { continueFolderMount(); };
            images.onExport = [this] { exportSelected(); };
            images.onDelete = [this] { askDeleteSelected(); };
            images.onKeepOnly = [this] { askDeleteSelected(/*keepOnly=*/true); };
            images.onRename = [this] { askRenameSelected(); };
            images.onUpgrade = [this] { askUpgradeEdition(); };
            images.onDuplicate = [this] { askDuplicateEdition(); };
            images.onExploreMount = [this] { exploreMount(); };
            images.onTerminal = [this] { openTerminalAtMount(); };
            images.onReveal = [this] { revealImageFile(); };
            images.onCopyInfo = [this] { copyEditionInfo(); };
            m_inspector = &add<ImageInspector>(m_strings, m_language);
            m_inspector->onMount = [this] {
                if (const auto index = m_state.selectedIndex()) {
                    m_images->mount(*index);
                }
            };
            m_inspector->onUnmount = [this] { askUnmount(); };
            m_inspector->onDelete = [this] { askDeleteSelected(); };
            m_inspector->onRename = [this] { askRenameSelected(); };
            m_inspector->onUpgrade = [this] { askUpgradeEdition(); };
            m_inspector->onExplore = [this] { exploreMount(); };
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
        } else if (page == PageId::Programs) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ProgramsPreview), ui::icons::Icon::Eye).onInvoke = [this] {
                if (auto shown = m_programs->preview(); !shown) {
                    showToast(ui::InfoKind::Error, m_strings.get(Str::ProgramsPreviewFailed), errorText(shown.error()));
                }
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ProgramsRefresh), ui::icons::Icon::Refresh)
                .onInvoke = [this] { m_programs->load(/*refresh=*/true); };
            auto& body = m_pageView->setBody<ProgramsPage>(m_state, *m_programs, m_strings, m_language,
                                                           [this] { showPage(PageId::Images); });
            m_pageBody = &body;
            auto& inspector = add<ProgramInspector>(m_strings, m_language);
            m_sideInspector = &inspector;
            inspector.setVisible(false);
            inspector.onToggle = [this] {
                if (auto* p = programsPage()) {
                    if (const auto program = p->selectedProgram()) {
                        m_programs->toggle(*program);
                    }
                }
            };
            inspector.onOpenUrl = [](const std::wstring& url) {
                if (url.starts_with(L"https://")) { // a manifest's address: only web pages open
                    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            };
            body.onSelectionChanged = [this] { updateProgramInspector(); };
        } else if (page == PageId::Registry) {
            auto pick = [this] {
                importRegFiles(ui::pickFiles(owner(), m_strings.get(Str::RegistryImportReg),
                                             {{m_strings.get(Str::RegistryFilter), L"*.reg"}}));
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::RegistryImportReg), ui::icons::Icon::RegFile)
                .onInvoke = std::move(pick);
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::RegistryAddValue), ui::icons::Icon::Add)
                .onInvoke = [this] { editRegistryValue(std::nullopt); };
            m_pageBody = &m_pageView->setBody<RegistryPage>(
                m_state, *m_registry, m_strings, m_language,
                RegistryPage::Intents{[this] { showPage(PageId::Images); },
                                      [this](std::size_t entry) { editRegistryValue(entry); }});
        } else if (page == PageId::About) {
            m_pageBody = &m_pageView->setBody<AboutPage>(
                m_state, m_strings,
                AboutPage::Intents{[this] {
                                       if (!host()) {
                                           return;
                                       }
                                       auto dialog = std::make_unique<ui::Dialog>(
                                           m_strings.get(Str::AboutLicenses), m_strings.get(Str::AboutLicensesBody),
                                           ui::icons::Icon::InfoCircle, ui::tokens::Color::TextSecondary);
                                       ui::Dialog* raw = dialog.get();
                                       raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonOk),
                                                      closer(raw), /*primary=*/true);
                                       pushDialog(std::move(dialog));
                                   },
                                   [this] { openLogFolder(); },
                                   [](const std::wstring& url) {
                                       // Only the page's own https addresses reach here.
                                       if (url.starts_with(L"https://")) {
                                           ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                                       }
                                   }});
        } else if (page == PageId::Settings) {
            auto& body = m_pageView->setBody<SettingsPage>(
                m_state, m_strings,
                SettingsPage::Intents{[this]() -> std::optional<std::filesystem::path> {
                                          return ui::pickFolder(owner(), m_strings.get(Str::SettingsWorkDir));
                                      },
                                      [this] { return m_images->busy() || m_apply->running() || m_iso->running(); }});
            m_pageBody = &body;
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::CommonReset)).onInvoke = [this, &body] {
                const bool locked = body.foldersLocked();
                body.resetToDefaults();
                showToast(ui::InfoKind::Success, m_strings.get(Str::SettingsResetDone),
                          locked ? m_strings.get(Str::SettingsLockedHint) : std::wstring());
            };
        } else if (page == PageId::Presets) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::CommonImport), ui::icons::Icon::Import)
                .onInvoke = [this] { importPreset(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PresetsNew), ui::icons::Icon::Add)
                .onInvoke = [this] { newPreset(); };
            m_presets->reload();
            auto byIndex = [this](std::size_t index) -> const Preset* {
                return index < m_presets->presets().size() ? &m_presets->presets()[index] : nullptr;
            };
            m_pageBody = &m_pageView->setBody<PresetsPage>(
                m_state, *m_presets, m_strings,
                PresetsPage::Intents{
                    [this, byIndex](std::size_t index) {
                        if (const Preset* preset = byIndex(index)) {
                            applyPreset(*preset);
                        }
                    },
                    [this, byIndex](std::size_t index) {
                        const Preset* preset = byIndex(index);
                        if (!preset) {
                            return;
                        }
                        const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::CommonExport),
                                                             {{m_strings.get(Str::ApplyPresetFiles), L"*.wlpreset;*.json"}},
                                                             PresetController::fileNameFor(preset->name) + L".wlpreset",
                                                             L"wlpreset");
                        if (!target) {
                            return;
                        }
                        const auto saved = m_presets->exportTo(index, *target);
                        showToast(saved ? ui::InfoKind::Success : ui::InfoKind::Error,
                                  m_strings.get(saved ? Str::PresetsExported : Str::PresetsFailed), target->wstring());
                    },
                    [this, byIndex](std::size_t index) {
                        const Preset* preset = byIndex(index);
                        if (!preset || !host()) {
                            return;
                        }
                        auto dialog = std::make_unique<ui::Dialog>(
                            m_strings.get(Str::PresetsDeleteTitle),
                            m_strings.format(Str::PresetsDeleteBody, {{L"name", preset->name}}), ui::icons::Icon::ErrorOctagon,
                            ui::tokens::Color::StatusError);
                        ui::Dialog* raw = dialog.get();
                        raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel),
                                       closer(raw));
                        raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::CommonDelete), [this, raw, index] {
                            host()->popModal(raw);
                            const auto removed = m_presets->remove(index);
                            showToast(removed ? ui::InfoKind::Success : ui::InfoKind::Error,
                                      m_strings.get(removed ? Str::PresetsDeleted : Str::PresetsFailed), L"");
                            if (auto* page = presetsPage()) {
                                page->reloadList();
                            }
                        });
                        pushDialog(std::move(dialog));
                    },
                });
        } else if (page == PageId::PostSetup) {
            using Type = core::PostSetupStep::Type;
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupAddCommand), ui::icons::Icon::LogTerminal)
                .onInvoke = [this] { editPostSetupStep(Type::Command, std::nullopt); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupAddFile), ui::icons::Icon::File)
                .onInvoke = [this] { editPostSetupStep(Type::Copy, std::nullopt); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupAddApp), ui::icons::Icon::AppxPackage)
                .onInvoke = [this] { editPostSetupStep(Type::Winget, std::nullopt); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupAddWifi), ui::icons::Icon::Network)
                .onInvoke = [this] { editPostSetupStep(Type::Wifi, std::nullopt); };
            // D-078: the programs have a page of their own, over winget's whole repository.
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupCatalog), ui::icons::Icon::Download)
                .onInvoke = [this] { showPage(PageId::Programs); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::PostsetupCommands), ui::icons::Icon::LogTerminal)
                .onInvoke = [this] { pickPostSetupCommands(); };
            m_pageBody = &m_pageView->setBody<PostSetupPage>(
                m_state, *m_postSetup, m_strings, m_language,
                PostSetupPage::Intents{[this](std::size_t index) {
                                           if (index < m_postSetup->plan().steps.size()) {
                                               editPostSetupStep(m_postSetup->plan().steps[index].type, index);
                                           }
                                       },
                                       [this] { showPage(PageId::Images); }});
        } else if (page == PageId::Unattended) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UnattendedImportXml), ui::icons::Icon::Import)
                .onInvoke = [this] { importAnswerFile(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UnattendedSaveXml), ui::icons::Icon::Save)
                .onInvoke = [this] { saveAnswerFile(); };
            m_pageBody = &m_pageView->setBody<UnattendedPage>(m_state, *m_unattend, m_strings);
        } else if (page == PageId::StartMenu) {
            m_pageBody = &m_pageView->setBody<StartMenuPage>(m_state, *m_startPins, *m_imageSettings, m_strings, m_language,
                                                             [this] { showPage(PageId::Images); });
        } else if (page == PageId::Tweaks) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::TweaksApplyRecommended)).onInvoke = [this] {
                if (!requireMount(Str::TweaksNoMountTitle, Str::TweaksNoMountBody)) {
                    return;
                }
                const int changed = m_imageSettings->applyRecommended();
                showToast(ui::InfoKind::Success,
                          changed > 0 ? m_strings.format(Str::TweaksRecommendedApplied, {{L"n", std::to_wstring(changed)}})
                                      : m_strings.get(Str::TweaksRecommendedNone),
                          L"");
            };
            m_pageBody = &m_pageView->setBody<TweaksPage>(
                m_state, *m_imageSettings, m_strings, m_language, [this] { showPage(PageId::Images); },
                [this]() -> std::optional<std::filesystem::path> {
                    return ui::pickFile(owner(), m_strings.get(Str::TweaksPickImage),
                                        {{m_strings.get(Str::TweaksJpegFiles), L"*.jpg;*.jpeg"}});
                });
        } else if (page == PageId::Tasks) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::TasksAdd), ui::icons::Icon::Add).onInvoke =
                [this] { addTaskDialog(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::TasksApplyRecommended)).onInvoke = [this] {
                if (!requireMount(Str::TasksNoMountTitle, Str::TasksNoMountBody)) {
                    return;
                }
                const int n = m_tasks->applyRecommended();
                showToast(ui::InfoKind::Success,
                          n > 0 ? m_strings.format(Str::TasksRecommendedDone, {{L"n", std::to_wstring(n)}})
                                : m_strings.get(Str::TasksRecommendedNone),
                          L"");
            };
            m_pageBody = &m_pageView->setBody<TasksPage>(m_state, *m_tasks, m_strings, m_language,
                                                         [this] { showPage(PageId::Images); });
        } else if (page == PageId::Languages) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::LanguagesScan), ui::icons::Icon::OpenFolder).onInvoke =
                [this] { scanLanguageFolder(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::LanguagesAdd), ui::icons::Icon::Download).onInvoke =
                [this] { addLanguages(); };
            m_pageBody = &m_pageView->setBody<LanguagesPage>(
                m_state, *m_languages, m_strings, m_language,
                LanguagesPage::Intents{[this] { showPage(PageId::Images); }, [this] { m_languageFetch->cancel(); },
                                       [this] { findUpdates(); }});
        } else if (page == PageId::Apps) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::AppsStoreAction), ui::icons::Icon::Download).onInvoke =
                [this] { showStoreDialog(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::AppsFromHost), ui::icons::Icon::Import).onInvoke =
                [this] {
                    if (!m_state.mounted() || m_apps->importingHost()) {
                        return;
                    }
                    if (!core::isElevated()) {
                        showAdminRequired(L"--page=apps");
                        return;
                    }
                    showToast(ui::InfoKind::Info, m_strings.get(Str::AppsHostImporting), L"");
                    m_apps->importHostAssociations();
                    if (auto* page = appsPage()) {
                        page->showDefaultsTab();
                    }
                };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::AppsImportXml), ui::icons::Icon::XmlFile).onInvoke =
                [this] {
                    if (!m_state.mounted()) {
                        return;
                    }
                    const auto file = ui::pickFile(owner(), m_strings.get(Str::AppsImportXml), {{m_strings.get(Str::AppsXmlFiles), L"*.xml"}});
                    if (!file) {
                        return;
                    }
                    auto n = m_apps->importAssociationsFile(*file);
                    if (!n) {
                        showToast(ui::InfoKind::Error, m_strings.get(Str::AppsImportFailed), errorText(n.error()));
                        return;
                    }
                    showToast(ui::InfoKind::Success, m_strings.format(Str::AppsImported, {{L"n", std::to_wstring(*n)}}),
                              file->filename().wstring());
                    if (auto* page = appsPage()) {
                        page->showDefaultsTab();
                    }
                };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::AppsAddPackage), ui::icons::Icon::Add).onInvoke =
                [this] { pickAppPackages(); };
            m_pageBody = &m_pageView->setBody<AppsPage>(m_state, *m_apps, m_strings, m_language, [this] { pickAppPackages(); },
                                                        [this] { showPage(PageId::Images); }, [this] { m_store->cancel(); });
        } else if (page == PageId::Files) {
            auto pickFiles = [this] {
                auto files = ui::pickFiles(owner(), m_strings.get(Str::FilesAddFiles), {{m_strings.get(Str::FilesAllFiles), L"*.*"}});
                if (!files.empty()) {
                    addFilesTo(std::move(files));
                }
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::FilesAddFolder), ui::icons::Icon::Folder).onInvoke =
                [this] {
                    if (const auto folder = ui::pickFolder(owner(), m_strings.get(Str::FilesAddFolder))) {
                        addFilesTo({*folder});
                    }
                };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::FilesAddFiles), ui::icons::Icon::Add).onInvoke =
                pickFiles;
            m_pageBody = &m_pageView->setBody<FilesPage>(m_state, *m_files, m_strings, m_language, pickFiles,
                                                         [this] { showPage(PageId::Images); });
        } else if (page == PageId::Hosts) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::HostsImport), ui::icons::Icon::Import).onInvoke =
                [this] { importHostsFile(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::HostsApplyRecommended)).onInvoke = [this] {
                if (m_state.mounted()) {
                    m_hosts->applyRecommended();
                }
            };
            m_pageBody = &m_pageView->setBody<HostsPage>(m_state, *m_hosts, m_strings, m_language,
                                                         [this] { showPage(PageId::Images); }, [this] { importHostsFile(); });
        } else if (page == PageId::Branding) {
            auto pickFonts = [this] {
                addFonts(ui::pickFiles(owner(), m_strings.get(Str::BrandingAddFonts),
                                       {{m_strings.get(Str::BrandingFontFiles), L"*.ttf;*.otf;*.ttc"}}));
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::BrandingAddFonts), ui::icons::Icon::Add).onInvoke =
                std::move(pickFonts);
            BrandingPage::Intents intents;
            intents.pickPicture = [this]() -> std::optional<std::filesystem::path> {
                return ui::pickFile(owner(), m_strings.get(Str::BrandingChoosePicture),
                                    {{m_strings.get(Str::BrandingPictureFiles), L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.heic;*.webp"}});
            };
            intents.refused = [this](const std::wstring& file) {
                showToast(ui::InfoKind::Warning, m_strings.get(Str::BrandingNotPicture), file);
            };
            intents.goImages = [this] { showPage(PageId::Images); };
            m_pageBody = &m_pageView->setBody<BrandingPage>(m_state, *m_branding, m_strings, m_language, std::move(intents));
        } else if (page == PageId::Icons) {
            ui::Button& loadPack = m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::IconsLoadPack), ui::icons::Icon::OpenFolder);
            loadPack.onInvoke = [this, &loadPack] { showIconPackMenu(loadPack.bounds()); };
            m_pageView->addAction(ui::ButtonKind::Secondary,
                                  m_strings.get(m_icons->shortcutArrowRemoved() ? Str::IconsArrowBack : Str::IconsArrowRemove))
                .onInvoke = [this] {
                if (!requireMount(Str::IconsNoMountTitle, Str::IconsNoMountBody)) {
                    return;
                }
                if (auto ok = m_icons->setShortcutArrowRemoved(!m_icons->shortcutArrowRemoved()); !ok) {
                    showToast(ui::InfoKind::Error, m_strings.get(Str::IconsArrowRemove), errorText(ok.error()));
                }
                showPage(PageId::Icons); // the action's text follows
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::IconsExportPack), ui::icons::Icon::Save).onInvoke =
                [this] { exportIconPack(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::IconsResetAll)).onInvoke = [this] {
                m_icons->resetAll();
                m_iconPatch->resetAll();
            };
            IconsPage::Intents intents;
            intents.goImages = [this] { showPage(PageId::Images); };
            intents.pickIcon = [this]() -> std::optional<std::filesystem::path> {
                return ui::pickFile(owner(), m_strings.get(Str::IconsPick), {{m_strings.get(Str::IconsFiles), L"*.ico"}});
            };
            intents.refused = [this](const std::wstring& file) { showToast(ui::InfoKind::Warning, m_strings.get(Str::IconsNotIcon), file); };
            intents.files.pickSource = [this]() -> std::optional<std::filesystem::path> {
                return ui::pickFile(owner(), m_strings.get(Str::IconsPickTitle),
                                    {{m_strings.get(Str::IconsFilterIcons), L"*.ico;*.png;*.jpg;*.jpeg;*.bmp"}});
            };
            intents.files.saveIco = [this](std::wstring name) -> std::optional<std::filesystem::path> {
                return ui::pickSaveFile(owner(), m_strings.get(Str::IconsSaveOriginal), {{m_strings.get(Str::IconsFiles), L"*.ico"}},
                                        name, L"ico");
            };
            intents.files.toast = [this](Str title, std::wstring detail, bool error) {
                showToast(error ? ui::InfoKind::Warning : ui::InfoKind::Success, m_strings.get(title), std::move(detail));
            };
            m_pageBody =
                &m_pageView->setBody<IconsPage>(m_state, *m_icons, *m_iconPatch, m_strings, m_language, std::move(intents));
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
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::DriversFromHost), ui::icons::Icon::Import)
                .onInvoke = [this] {
                    if (m_imageDriverCtl->exporting()) {
                        return;
                    }
                    if (!core::isElevated()) {
                        showAdminRequired(L"--page=drivers");
                        return;
                    }
                    showToast(ui::InfoKind::Info, m_strings.get(Str::DriversHostExporting), m_imageDriverCtl->hostFolder().wstring());
                    m_imageDriverCtl->exportHost();
                };
            m_pageBody = &m_pageView->setBody<DriversPage>(
                m_state, *m_imageDriverCtl, m_strings, m_language,
                DriversPage::Intents{[this] { scanDriverFolder(); }, [this] { showPage(PageId::Images); }});
        } else if (page == PageId::Updates) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesFind), ui::icons::Icon::Download)
                .onInvoke = [this] { findUpdates(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesScanFolder), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] {
                    if (const auto folder = ui::pickFolder(owner(), m_strings.get(Str::UpdatesScanFolder))) {
                        const auto files = core::scanUpdates(*folder);
                        if (files.empty()) {
                            showToast(ui::InfoKind::Warning, m_strings.get(Str::UpdatesNoneFound), folder->wstring());
                        } else {
                            addUpdates(files);
                        }
                    }
                };
            auto pick = [this] {
                addUpdates(ui::pickFiles(owner(), m_strings.get(Str::UpdatesPickTitle),
                                         {{m_strings.get(Str::UpdatesFilter), L"*.msu;*.cab"}}));
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::UpdatesAddPackage), ui::icons::Icon::Add)
                .onInvoke = pick;
            m_pageBody = &m_pageView->setBody<UpdatesPage>(m_state, m_strings, m_language,
                                                           UpdatesPage::Intents{pick, [this] { showPage(PageId::Images); },
                                                                                [this] { m_updateCatalog->cancel(); }});
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
                m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ApplySaveReport), ui::icons::Icon::File)
                    .onInvoke = [this] { saveApplyReport(); };
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
                m_state, *m_apply, m_imageSettings->catalog(), m_strings, m_language,
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
                                     return ui::pickFolder(owner(), m_strings.get(Str::IsoFolder));
                                 },
                                 [](const std::filesystem::path& file) {
                                     revealInExplorer(file);
                                 },
                                 m_services.postToUi,
                                 [this] { updateIsoChrome(); },
                                 [this] {
                                     // D-077: the setup files from Windows 10 media.
                                     const auto file = ui::pickFile(owner(), m_strings.get(Str::IsoAioPick),
                                                                    {{m_strings.get(Str::IsoAioPick), L"*.iso;*.esd;*.wim"}});
                                     if (file) {
                                         m_images->replaceSetupMedia(*file);
                                     }
                                 }});
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
    readThenUi<core::SourceInfo>(
        [path](const core::TaskContext&) { return core::openSource(path); },
        [this, serial, then = std::move(then)](Result<core::SourceInfo> result) mutable {
            if (serial != m_openSerial) {
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
                    showToast(ui::InfoKind::Error, m_strings.get(Str::SourceOpenFailed), errorText(result.error()));
                }
                return;
            }
            m_state.setSource(std::move(*result));
            showPage(PageId::Images);
            if (then) {
                then();
            }
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
        startPreload();
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
        startPreload();
    });
}

void Shell::checkHost() {
    readThenUi<core::HostDismReport>([](const core::TaskContext&) -> Result<core::HostDismReport> { return core::checkHostDism(); },
                                     [this](Result<core::HostDismReport> report) {
                                         if (report) {
                                             m_state.setHostDism(std::move(*report));
                                         }
                                     });
}

void Shell::startPreload() {
    m_imageValues->load(); // queued first on the engine thread: ~0.4 s, the lists take longer
    m_preload->start();
}

void Shell::pickSourceFile() {
    const auto file = ui::pickFile(owner(), m_strings.get(Str::SourceOpenFile),
                                   {{m_strings.get(Str::SourceFilterImages), L"*.iso;*.wim;*.esd;*.swm"},
                                    {m_strings.get(Str::SourceFilterAll), L"*.*"}});
    if (file) {
        openSource(*file);
    }
}

void Shell::removeSource(const std::filesystem::path& path) {
    auto same = [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
    };
    auto inside = [](const std::filesystem::path& file, const std::filesystem::path& folder) {
        const std::wstring f = file.lexically_normal().wstring();
        const std::wstring d = folder.lexically_normal().wstring();
        return f.size() > d.size() && _wcsnicmp(f.c_str(), d.c_str(), d.size()) == 0 &&
               (f[d.size()] == L'\\' || f[d.size()] == L'/');
    };
    const bool open = m_state.source() && same(m_state.source()->path, path);
    const bool mountedFromIt = m_state.mounted() && (open || inside(m_state.mounted()->imagePath, path));
    if (mountedFromIt || (open && (m_images->busy() || m_state.operation()))) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesUnmountFirst), L"");
        return;
    }
    auto forget = [this, path, open] {
        if (open) {
            m_state.clearSource();
        }
        m_state.forgetRecent(path);
    };
    std::error_code ec;
    const bool copy = m_state.settings().isWorkCopy(path) && std::filesystem::is_directory(path, ec) &&
                      !std::filesystem::is_symlink(path, ec);
    if (!copy || !host()) {
        forget(); // the user's own file or folder: only the list entry goes
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(
        m_strings.get(Str::SourceRemoveTitle),
        m_strings.format(Str::SourceRemoveBody, {{L"name", path.filename().wstring()}, {L"path", path.wstring()}}),
        ui::icons::Icon::Delete, ui::tokens::Color::TextSecondary);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::SourceRemoveKeep), [this, raw, forget] {
        host()->popModal(raw);
        forget();
    });
    raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::SourceRemoveDelete), [this, raw, forget, path] {
        host()->popModal(raw);
        forget();
        std::error_code failed;
        std::filesystem::remove_all(path, failed);
        if (failed) {
            log::error("app", L"could not delete the work copy " + path.wstring() + L": " +
                                  utf8::toWide(failed.message()));
            showToast(ui::InfoKind::Error, m_strings.get(Str::SourceDeleteFailed), path.wstring());
        } else {
            log::info("app", L"work copy deleted: " + path.wstring());
            showToast(ui::InfoKind::Success, m_strings.get(Str::SourceCopyDeleted), path.filename().wstring());
        }
    });
    pushDialog(std::move(dialog));
}

void Shell::pickSourceFolder() {
    if (const auto folder = ui::pickFolder(owner(), m_strings.get(Str::SourcePickFolder))) {
        openSource(*folder);
    }
}

ui::Dialog& Shell::pushDialog(std::unique_ptr<ui::Dialog> dialog) {
    // Push after the buttons exist: the modal focuses its first focusable (the cancel button).
    ui::Dialog* raw = dialog.get();
    raw->onCancel = closer(raw);
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
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
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
    if (auto* page = filesPage(); page && m_state.mounted()) {
        page->setDragState(ui::DropZone::DragState::Valid); // any file or folder can go into the image
        return true;
    }
    if (auto* page = appsPage(); page && m_state.mounted()) {
        const bool any = std::ranges::any_of(files, [](const auto& f) { return core::isAppxFile(f); });
        page->setDragState(any ? ui::DropZone::DragState::Valid : ui::DropZone::DragState::Invalid);
        return any;
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
    if (auto* page = appsPage()) {
        page->setDragState(ui::DropZone::DragState::None);
    }
    if (auto* files = filesPage()) {
        files->setDragState(ui::DropZone::DragState::None);
    }
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
    if (auto* page = filesPage(); page && m_state.mounted()) {
        page->setDragState(ui::DropZone::DragState::None);
        addFilesTo(files);
        return;
    }
    if (auto* page = appsPage(); page && m_state.mounted()) {
        page->setDragState(ui::DropZone::DragState::None);
        m_apps->addPackages(files);
        return;
    }
    // Several files dropped: the first source opens, the others go into the recent list (D-058).
    std::vector<std::filesystem::path> sources;
    std::ranges::copy_if(files, std::back_inserter(sources), [](const auto& f) { return isSourceCandidate(f); });
    if (sources.empty()) {
        return;
    }
    if (m_page != PageId::Source) {
        showPage(PageId::Source);
    }
    for (std::size_t i = 1; i < sources.size(); ++i) {
        readThenUi<core::SourceInfo>(
            [path = sources[i]](const core::TaskContext&) { return core::openSource(path); },
            [this](Result<core::SourceInfo> result) {
                if (result) {
                    m_state.rememberSource(*result);
                } else {
                    log::warn("app", L"dropped file not added: " + describe(result.error()));
                }
            });
    }
    if (sources.size() > 1) {
        showToast(ui::InfoKind::Info, m_strings.format(Str::SourceDroppedMany, {{L"n", std::to_wstring(sources.size() - 1)}}), L"");
    }
    openSource(sources.front());
}

// ---- images (P02) ----------------------------------------------------------------------------

void Shell::onImageFailure(ImageController::Failure failure, const Error& error) {
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
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::CommonOk), closer(raw),
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
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
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

void Shell::askDiscardMount(const std::filesystem::path& folder, const std::wstring& edition) {
    if (!host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::SourceDiscardTitle),
                                               m_strings.format(Str::SourceDiscardBody, {{L"folder", folder.wstring()}}),
                                               ui::icons::Icon::Unmount, ui::tokens::Color::StatusWarning);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::SourceDiscardAction),
                   [this, raw, folder, edition] {
                       host()->popModal(raw);
                       m_images->discardMount(folder, edition);
                   },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
}

void Shell::askDeleteSelected(bool keepOnly) {
    const auto* image = m_state.selectedImage();
    if (!host() || !image) {
        return;
    }
    if (const auto refusal = m_images->deleteRefusal()) {
        showToast(ui::InfoKind::Warning, m_strings.get(*refusal), L"");
        return;
    }
    // Delete: the marked editions go. Keep only: the marked ones stay, every other one goes.
    const auto& images = m_state.source()->install.images;
    const std::vector<int>& marked = m_state.selection();
    std::vector<const core::ImageInfo*> doomed;
    for (const auto& edition : images) {
        if (std::ranges::binary_search(marked, edition.index) != keepOnly) {
            doomed.push_back(&edition);
        }
    }
    if (doomed.empty()) {
        return;
    }
    if (doomed.size() >= images.size()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesKeepOne), L"");
        return;
    }
    auto named = [](const core::ImageInfo& edition) { return std::format(L"{} · {}", edition.index, edition.name); };
    const std::wstring count = std::to_wstring(doomed.size());
    // What the strip and the toast call them.
    std::wstring label = doomed.size() == 1 ? doomed.front()->name : m_strings.format(Str::ImagesEditionCount, {{L"n", count}});
    std::wstring title;
    std::wstring body;
    if (keepOnly) {
        title = m_strings.get(Str::DialogsKeepOnlyTitle);
        body = marked.size() == 1
                   ? m_strings.format(Str::DialogsKeepOnlyBody, {{L"name", named(*image)}, {L"n", count}})
                   : m_strings.format(Str::DialogsKeepManyBody, {{L"k", std::to_wstring(marked.size())}, {L"n", count}});
    } else if (doomed.size() == 1) {
        title = m_strings.get(Str::DialogsDeleteIndexTitle);
        body = m_strings.format(Str::DialogsDeleteIndexBody, {{L"name", named(*doomed.front())}});
    } else {
        std::wstring names;
        for (const auto* edition : doomed) {
            names += (names.empty() ? L"" : L", ") + named(*edition);
        }
        title = m_strings.format(Str::DialogsDeleteManyTitle, {{L"n", count}});
        body = m_strings.format(Str::DialogsDeleteManyBody, {{L"names", names}});
    }
    if (m_state.source()->format == core::ImageFormat::Iso) {
        body += L" " + m_strings.get(Str::DialogsDeleteIsoNote);
    }
    std::vector<int> indexes;
    for (const auto* edition : doomed) {
        indexes.push_back(edition->index);
    }
    auto dialog = std::make_unique<ui::Dialog>(title, body, ui::icons::Icon::ErrorOctagon, ui::tokens::Color::StatusError);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), closer(raw));
    raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::CommonDelete),
                   [this, raw, indexes = std::move(indexes), label = std::move(label)] {
                       host()->popModal(raw);
                       m_images->removeEditions(indexes, label);
                   });
    pushDialog(std::move(dialog));
}

void Shell::askRenameSelected() {
    const auto* image = m_state.selectedImage();
    if (!host() || !image) {
        return;
    }
    if (const auto refusal = m_images->editRefusal()) {
        showToast(ui::InfoKind::Warning, m_strings.get(*refusal), L"");
        return;
    }
    std::wstring note = m_strings.get(Str::DialogsRenameHint);
    if (m_state.source()->format == core::ImageFormat::Iso) {
        note += L" " + m_strings.get(Str::DialogsDeleteIsoNote);
    }
    const int index = image->index;
    const ModalSlot slot = modalSlot();
    RenameDialogActions actions;
    actions.close = slot.close;
    actions.accept = [this, index](std::wstring name, std::wstring description) {
        m_images->renameEdition(index, std::move(name), std::move(description));
    };
    // Setup shows DISPLAYNAME; images without one fall back to the name.
    RenameDialog built = makeRenameDialog(m_strings, image->displayName.empty() ? image->name : image->displayName,
                                          image->displayDescription.empty() ? image->description : image->displayDescription,
                                          std::move(note), std::move(actions));
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::exploreMount() {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesNoMountToExplore), L"");
        return;
    }
    // An Explorer window inside the mount folder does not block the unmount: unmountSafely moves
    // such windows away first.
    ShellExecuteW(nullptr, L"explore", mounted->mountDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Shell::openTerminalAtMount() {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesNoMountToExplore), L"");
        return;
    }
    // Inherits this process' elevation: dism /Image:. works right away.
    ShellExecuteW(nullptr, L"open", L"cmd.exe", L"/k title WinLove mount", mounted->mountDir.c_str(), SW_SHOWNORMAL);
}

void Shell::revealImageFile() {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    // The install image itself where it is a file on disk; for an ISO, the ISO.
    const std::filesystem::path file =
        source->format == core::ImageFormat::Folder ? source->path / source->installImage : source->path;
    revealInExplorer(file.lexically_normal());
}

void Shell::copyEditionInfo() {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    const std::filesystem::path file =
        source->format == core::ImageFormat::Folder ? source->path / source->installImage : source->path;
    std::wstring text;
    for (const auto& image : source->install.images) {
        if (!std::ranges::binary_search(m_state.selection(), image.index)) {
            continue;
        }
        if (!text.empty()) {
            text += L"\r\n";
        }
        auto line = [&](Str label, const std::wstring& value) {
            if (!value.empty()) {
                text += std::format(L"{}: {}\r\n", m_strings.get(label), value);
            }
        };
        text += std::format(L"{} · {}\r\n", image.index, image.name);
        line(Str::ImagesEditionId, image.editionId);
        line(Str::ImagesBuild, std::format(L"{} ({})", image.versionString(), core::releaseLabel(image.build)));
        line(Str::ImagesArch, core::architectureName(image.architecture));
        line(Str::ImagesLang, image.defaultLanguage);
        line(Str::CommonSize, formatBytes(image.totalBytes, m_language));
        line(Str::ImagesModified, formatDate(image.modifiedTime ? image.modifiedTime : image.creationTime, m_language));
        line(Str::ImagesWimFile, file.lexically_normal().wstring());
    }
    if (!text.empty() && ui::setClipboardText(text)) {
        showToast(ui::InfoKind::Success, m_strings.get(Str::ImagesCopiedToast), L"");
    }
}

void Shell::refreshSource() {
    const auto& source = m_state.source();
    if (!source || m_state.mounted() || m_images->busy()) {
        return; // a mounted image's list is the one on screen; nothing to re-read mid-operation
    }
    // Keeps what was marked: the same file, read again.
    const std::vector<int> marked = m_state.selection();
    const auto primary = m_state.selectedIndex();
    openSource(source->path, [this, marked, primary] {
        if (!primary) {
            return;
        }
        std::vector<int> still;
        for (const auto& image : m_state.source()->install.images) {
            if (std::ranges::binary_search(marked, image.index)) {
                still.push_back(image.index);
            }
        }
        if (std::ranges::binary_search(still, *primary)) {
            m_state.selectMany(still, *primary);
        }
    });
}

void Shell::askUpgradeEdition() {
    const auto mounted = m_state.mounted();
    if (!host() || !mounted) {
        return;
    }
    m_images->readEditions([this](const core::ImageEditions& editions) {
        const auto now = m_state.mounted();
        if (!host() || !now) {
            return;
        }
        if (editions.targets.empty()) {
            showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesUpgradeNone), L"");
            return;
        }
        int build = 26100;
        std::wstring name = now->edition;
        if (const auto& source = m_state.source()) {
            for (const auto& image : source->install.images) {
                if (image.index == now->index) {
                    build = image.build;
                    name = image.name;
                }
            }
        }
        std::vector<EditionChoice> choices;
        for (const auto& id : editions.targets) {
            choices.push_back({id, core::editionDisplayName(id, build)});
        }
        const auto* queued = m_state.changes().find(core::ops::OpKind::SetEdition, L"edition");
        const ModalSlot slot = modalSlot();
        EditionDialogActions actions;
        actions.close = slot.close;
        actions.accept = [this, build](std::wstring id) {
            // Medium, not High: the Apply summary words its high-risk warning for removals, and
            // the dialog this comes from has just said that the change is one-way.
            core::ops::Operation op{core::ops::OpKind::SetEdition, L"edition", id, core::ops::Risk::Medium};
            m_state.queue(std::move(op));
            showToast(ui::InfoKind::Success,
                      m_strings.format(Str::ImagesUpgradeQueuedToast, {{L"edition", core::editionDisplayName(id, build)}}), L"");
        };
        actions.remove = [this] {
            m_state.unqueue(core::ops::OpKind::SetEdition, L"edition");
            showToast(ui::InfoKind::Success, m_strings.get(Str::ImagesUpgradeRemovedToast), L"");
        };
        EditionDialog built = makeEditionDialog(
            m_strings,
            m_strings.format(Str::DialogsUpgradeBody, {{L"name", name}}),
            std::move(choices), queued ? queued->value : std::wstring(), std::move(actions));
        showModal(slot, std::move(built.dialog), built.initialFocus);
    });
}

void Shell::onImageVerified(const core::WimVerifyReport& report, const std::wstring& file) {
    const std::wstring streams = formatCount(report.streams, m_language);
    const std::wstring title = m_strings.format(report.sound() ? Str::ImagesVerifySound : Str::ImagesVerifyDamaged, {{L"file", file}});
    const std::wstring body =
        report.sound() ? m_strings.format(Str::ImagesVerifySoundBody, {{L"n", streams}, {L"size", formatBytes(report.bytes, m_language)}})
                       : m_strings.format(Str::ImagesVerifyDamagedBody,
                                          {{L"n", formatCount(report.damaged, m_language)}, {L"total", streams}});
    if (auto* page = imagesPage()) {
        page->showNotice(report.sound() ? ui::InfoKind::Success : ui::InfoKind::Error, title, body);
    }
    showToast(report.sound() ? ui::InfoKind::Success : ui::InfoKind::Error, title, report.sound() ? std::wstring() : body);
}

void Shell::exportSelected() {
    const auto* image = m_state.selectedImage();
    if (!image) {
        return;
    }
    const std::vector<int> marked = m_state.selection();
    // Several editions go into one file, named after the image they come from.
    const std::wstring suggested = marked.size() > 1 ? std::filesystem::path(m_state.source()->installImage).stem().wstring() + L".wim"
                                                     : image->name + L".wim";
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ImagesExport), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         suggested, L"wim");
    if (!target) {
        return;
    }
    if (marked.size() > 1) {
        m_images->exportEditions(marked, *target);
    } else {
        m_images->exportIndex(image->index, *target);
    }
}

void Shell::exportLog() {
    auto* logs = logsPage();
    if (!logs) {
        return;
    }
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::LogsExportTitle),
                                         {{m_strings.get(Str::LogsLogFiles), L"*.log;*.txt"}}, L"WinLove.log", L"log");
    if (!target) {
        return;
    }
    if (writeFileAtomic(*target, utf8::fromWide(logs->exportText()))) {
        showToast(ui::InfoKind::Success, m_strings.get(Str::LogsExported), target->wstring());
    } else {
        showToast(ui::InfoKind::Error, m_strings.get(Str::LogsExportFailed), target->wstring());
    }
}

// ---- D-058 tools ------------------------------------------------------------------------------

Shell::ModalSlot Shell::modalSlot() {
    ModalSlot slot;
    slot.close = [this, dialog = slot.dialog] {
        if (*dialog) {
            host()->popModal(std::exchange(*dialog, nullptr)); // once, whichever handler gets there first
        }
    };
    return slot;
}

void Shell::showModal(const ModalSlot& slot, std::unique_ptr<ui::Dialog> dialog, ui::Widget* focus) {
    *slot.dialog = dialog.get();
    host()->pushModal(std::move(dialog), focus);
}

std::wstring Shell::errorText(const Error& error) const {
    // The engine's messages are English and technical; the common kinds get a localized line
    // (the full text stays in the log).
    const std::wstring what = error.context.empty() ? error.message : error.context;
    switch (error.code) {
    case ErrorCode::AccessDenied: return m_strings.format(Str::ErrorsAccessDenied, {{L"what", what}});
    case ErrorCode::NotFound: return m_strings.format(Str::ErrorsNotFound, {{L"what", what}});
    case ErrorCode::IoError: return m_strings.format(Str::ErrorsIoError, {{L"what", what}});
    default: return error.message;
    }
}

std::function<void()> Shell::closer(ui::Widget* modal) {
    return [this, modal] { host()->popModal(modal); };
}

HWND Shell::owner() const {
    return m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
}

void Shell::showImageTools() {
    if (!host() || !m_actionTools) {
        return;
    }
    std::vector<std::wstring> labels{m_strings.get(Str::ImagesRecompress)};
    std::vector<std::function<void()>> actions{[this] { askRecompress(); }};
    if (m_images->isSwmSource()) {
        labels.push_back(m_strings.get(Str::ImagesSwmToWim));
        actions.push_back([this] { askMergeSwm(); });
    } else {
        labels.push_back(m_strings.get(Str::ImagesSplitSwm));
        actions.push_back([this] { askSplitSwm(); });
    }
    labels.push_back(m_strings.get(Str::ImagesAppend));
    actions.push_back([this] { askAppendEditions(); });
    auto popup = std::make_unique<ui::MenuPopup>(
        m_actionTools->bounds(), std::move(labels), -1,
        [actions = std::move(actions)](int picked) {
            if (picked >= 0 && picked < static_cast<int>(actions.size())) {
                actions[static_cast<std::size_t>(picked)]();
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false);
}

void Shell::toolDialogForDemo(const std::wstring& which, const std::filesystem::path& file) {
    if (which == L"recompress") {
        askRecompress();
    } else if (which == L"split") {
        askSplitSwm();
    } else if (which == L"duplicate") {
        askDuplicateEdition();
    } else if (which == L"hash") {
        verifyHash(file);
    } else if (which == L"capture") {
        const ModalSlot slot = modalSlot();
        ToolDialog built = makeCaptureDialog(
            m_strings, m_strings.format(Str::SourceCaptureNote, {{L"folder", L"D:\\Kurulum\\Ofis"}, {L"wim", L"Ofis.wim"}}),
            L"Ofis", [](CaptureAnswers) {}, [] {});
        showModal(slot, std::move(built.dialog), built.initialFocus);
    } else if (which == L"append" && m_state.source()) {
        const ModalSlot slot = modalSlot();
        ToolDialog built = makeEditionsDialog(m_strings, m_strings.get(Str::ImagesAppend),
                                              m_strings.format(Str::ImagesAppendNote, {{L"file", file.filename().wstring()}}),
                                              m_state.source()->install.images, m_strings.get(Str::CommonAdd), [](std::vector<int>) {}, [] {});
        showModal(slot, std::move(built.dialog), built.initialFocus);
    }
}

void Shell::askRecompress() {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    using C = core::WimCompression;
    // Every format but the one the image has.
    const std::vector<std::pair<C, Str>> all{{C::Lzx, Str::ImagesCompressLzx},
                                             {C::Xpress, Str::ImagesCompressXpress},
                                             {C::None, Str::ImagesCompressNone},
                                             {C::Lzms, Str::ImagesCompressEsd}};
    const auto& header = source->install.header;
    const C current = header.solid ? C::Lzms : header.compression;
    auto targets = std::make_shared<std::vector<C>>();
    std::vector<std::wstring> labels;
    for (const auto& [c, label] : all) {
        if (c != current) {
            targets->push_back(c);
            labels.push_back(m_strings.get(label));
        }
    }
    std::wstring note = m_strings.format(Str::ImagesRecompressNote, {{L"now", m_strings.get(std::ranges::find_if(all, [&](const auto& p) {
                                                                                               return p.first == current;
                                                                                           })->second)}});
    if (source->format == core::ImageFormat::Iso) {
        note += L" " + m_strings.get(Str::DialogsDeleteIsoNote);
    }
    const ModalSlot slot = modalSlot();
    auto close = slot.close;
    ToolDialog built = makeChoiceDialog(m_strings, m_strings.get(Str::ImagesRecompress), note, m_strings.get(Str::ImagesCompression),
                                        std::move(labels), 0, m_strings.get(Str::ImagesConvert),
                                        [this, targets](int picked) { m_images->recompress((*targets)[static_cast<std::size_t>(picked)]); },
                                        close);
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::askSplitSwm() {
    static constexpr std::uint64_t kSizes[] = {3800, 2000, 1000, 650};
    const ModalSlot slot = modalSlot();
    auto close = slot.close;
    ToolDialog built = makeChoiceDialog(
        m_strings, m_strings.get(Str::ImagesSplitSwm), m_strings.get(Str::ImagesSplitNote), m_strings.get(Str::ImagesPartSize),
        {m_strings.get(Str::ImagesPartFat32), L"2000 MB", L"1000 MB", m_strings.get(Str::ImagesPartCd)}, 0,
        m_strings.get(Str::ImagesSplitGo),
        [this](int picked) {
            const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ImagesSplitSwm), {{L"SWM", L"*.swm"}}, L"install.swm", L"swm");
            if (target) {
                m_images->splitSwm(*target, kSizes[std::clamp(picked, 0, 3)]);
            }
        },
        close);
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::askMergeSwm() {
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ImagesSwmToWim), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         L"install.wim", L"wim");
    if (target) {
        m_images->mergeSwm(*target);
    }
}

void Shell::askAppendEditions() {
    const auto file = ui::pickFile(owner(), m_strings.get(Str::ImagesAppend),
                                   {{m_strings.get(Str::SourceFilterImages), L"*.iso;*.wim;*.esd;*.swm"}});
    if (!file) {
        return;
    }
    // The other image's editions are read on the reader thread, then offered as check boxes.
    readThenUi<core::SourceInfo>(
        [path = *file](const core::TaskContext&) { return core::openSource(path); },
        [this, path = *file](Result<core::SourceInfo> result) {
            if (!host()) {
                return;
            }
            if (!result) {
                showToast(ui::InfoKind::Error, m_strings.get(Str::ImagesAppend), errorText(result.error()));
                return;
            }
            // Only Windows editions: a Media Creation Tool ESD also holds its setup files and two
            // Windows PE images, which would land in install.wim as editions Setup cannot install.
            std::vector<core::ImageInfo> editions;
            std::ranges::copy_if(result->install.images, std::back_inserter(editions), core::isWindowsEdition);
            if (editions.empty()) {
                showToast(ui::InfoKind::Error, m_strings.get(Str::ImagesAppend), m_strings.get(Str::ImagesAppendNone));
                return;
            }
            const ModalSlot slot = modalSlot();
            auto close = slot.close;
            std::wstring note = m_strings.format(Str::ImagesAppendNote, {{L"file", path.filename().wstring()}});
            if (m_state.source() && m_state.source()->format == core::ImageFormat::Iso) {
                note += L" " + m_strings.get(Str::DialogsDeleteIsoNote);
            }
            ToolDialog built = makeEditionsDialog(m_strings, m_strings.get(Str::ImagesAppend), note, editions,
                                                  m_strings.get(Str::CommonAdd),
                                                  [this, path](std::vector<int> picked) { m_images->appendFrom(path, std::move(picked)); },
                                                  close);
            showModal(slot, std::move(built.dialog), built.initialFocus);
        });
}

void Shell::askDuplicateEdition() {
    const auto index = m_state.selectedIndex();
    const auto& source = m_state.source();
    if (!index || !source) {
        return;
    }
    std::wstring name;
    for (const auto& image : source->install.images) {
        if (image.index == *index) {
            name = image.displayName.empty() ? image.name : image.displayName;
        }
    }
    std::wstring note = m_strings.get(Str::ImagesDuplicateNote);
    if (source->format == core::ImageFormat::Iso) {
        note += L" " + m_strings.get(Str::DialogsDeleteIsoNote);
    }
    const ModalSlot slot = modalSlot();
    auto close = slot.close;
    ToolDialog built = makeTextDialog(m_strings, m_strings.get(Str::ImagesDuplicate), note, m_strings.get(Str::CommonName),
                                      m_strings.format(Str::ImagesDuplicateName, {{L"name", name}}), m_strings.get(Str::ImagesDuplicateGo),
                                      [this, i = *index](std::wstring newName) { m_images->duplicateEdition(i, std::move(newName)); },
                                      close);
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::askCapture() {
    const auto folder = ui::pickFolder(owner(), m_strings.get(Str::SourceCaptureFolder));
    if (!folder) {
        return;
    }
    std::wstring stem = folder->filename().wstring();
    if (stem.empty()) {
        stem = folder->root_name().wstring(); // a drive
        std::erase(stem, L':');
    }
    const auto wim = ui::pickSaveFile(owner(), m_strings.get(Str::SourceCaptureTitle), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                      (stem.empty() ? std::wstring(L"capture") : stem) + L".wim", L"wim");
    if (!wim) {
        return;
    }
    const ModalSlot slot = modalSlot();
    auto close = slot.close;
    const std::wstring note = m_strings.format(Str::SourceCaptureNote, {{L"folder", folder->wstring()}, {L"wim", wim->filename().wstring()}});
    ToolDialog built = makeCaptureDialog(
        m_strings, note, stem.empty() ? std::wstring(L"Capture") : stem,
        [this, folder = *folder, wim = *wim](CaptureAnswers a) {
            if (m_page != PageId::Images) {
                showPage(PageId::Images); // the operation strip is there
            }
            m_images->capture(folder, wim, core::ImageText{a.name, a.description, std::nullopt},
                              a.compression == 1 ? core::WimCompression::Xpress : core::WimCompression::Lzx);
        },
        close);
    showModal(slot, std::move(built.dialog), built.initialFocus);
}

void Shell::verifyHash(const std::filesystem::path& file) {
    if (!host()) {
        return;
    }
    const ModalSlot slot = modalSlot();
    auto open = std::make_shared<bool>(true);
    auto cancel = std::make_shared<core::CancelToken>();
    auto close = [pop = slot.close, open, cancel] {
        *open = false;
        cancel->cancel();
        pop();
    };
    HashDialog built = makeHashDialog(m_strings, file.wstring(), [this](std::wstring hash) {
        if (ui::setClipboardText(hash)) {
            showToast(ui::InfoKind::Success, m_strings.get(Str::SourceHashCopied), L"");
        }
    }, close);
    auto progress = built.progress;
    auto done = built.done;
    auto failed = built.failed;
    showModal(slot, std::move(built.dialog), built.initialFocus);
    // Reader thread; progress at whole percents only, and nothing once the dialog (or the shell) is gone.
    // The reader's own cancel (app closing) stops the hash too.
    auto post = m_services.postToUi;
    auto alive = std::weak_ptr<bool>(m_alive);
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    auto showing = [open, alive] {
        const auto stillAlive = alive.lock();
        return stillAlive && *stillAlive && *open;
    };
    m_state.reader().run<std::wstring>(
        [file, cancel, post, progress, showing, lastPercent](const core::TaskContext& outer) {
            const core::TaskContext task{*cancel, [&](double f, std::wstring_view) {
                                             if (outer.cancel.cancelled()) {
                                                 cancel->cancel();
                                             }
                                             const int percent = static_cast<int>(f * 100);
                                             if (lastPercent->exchange(percent) != percent) {
                                                 post([showing, progress, f] {
                                                     if (showing()) {
                                                         progress(f);
                                                     }
                                                 });
                                             }
                                         }};
            return core::sha256File(file, task);
        },
        [post, showing, done, failed](Result<std::wstring> result) {
            post([showing, done, failed, result = std::move(result)] {
                if (!showing()) {
                    return;
                }
                if (result) {
                    done(*result);
                } else {
                    failed(result.error().message);
                }
            });
        });
}

void Shell::convertEsd() {
    const auto target = ui::pickSaveFile(owner(), m_strings.get(Str::ImagesEsdToWim), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
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

// ---- command palette (P18) -------------------------------------------------------------------

void Shell::toggleNav() {
    m_userCollapsed = !navCollapsed();
    setNavCollapsed(m_userCollapsed, /*animated=*/true);
}

void Shell::openLogFolder() {
    const std::wstring folder = log::defaultDirectory().wstring();
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Shell::openPalette(std::wstring query) {
    if (!host() || host()->hasModal()) {
        return; // never on top of a dialog or an open menu
    }
    auto s = [&](Str key) { return m_strings.get(key); };
    auto palette = std::make_unique<CommandPalette>(
        CommandPalette::Labels{s(Str::PalettePlaceholder), s(Str::KbdEsc), s(Str::KbdEnter), s(Str::PaletteResults),
                               s(Str::PaletteCommands), s(Str::PaletteNoResults), s(Str::PaletteNoResultsHint)},
        [this](const std::wstring& text) { return m_palette->search(text); });
    CommandPalette* raw = palette.get();
    raw->onClose = closer(raw);
    raw->onRun = [this](const PaletteItem& item) { runPaletteItem(item); };
    host()->pushModal(std::move(palette), &raw->input());
    if (!query.empty()) {
        raw->setQuery(std::move(query));
    }
}

void Shell::runPaletteItem(const PaletteItem& item) {
    switch (item.kind) {
    case PaletteItem::Kind::Command: runPaletteCommand(item.command); break;
    case PaletteItem::Kind::Page: showPage(item.page); break;
    case PaletteItem::Kind::Setting:
        showPage(PageId::Tweaks);
        if (auto* page = dynamic_cast<TweaksPage*>(m_pageBody)) {
            page->reveal(utf8::fromWide(item.id));
        }
        break;
    case PaletteItem::Kind::Component:
        showPage(PageId::Components);
        if (auto* page = componentsPage()) {
            page->reveal(item.id);
        }
        break;
    case PaletteItem::Kind::Feature:
        showPage(PageId::Features);
        if (auto* page = featuresPage()) {
            page->reveal(item.id);
        }
        break;
    case PaletteItem::Kind::Service:
        showPage(PageId::Services);
        if (auto* page = servicesPage()) {
            page->reveal(item.id);
        }
        break;
    }
}

bool Shell::paletteCommandAvailable(PaletteCommand command) const {
    const bool busy = m_images->busy() || m_state.queueLocked();
    switch (command) {
    case PaletteCommand::ApplyQueue: return m_apply->canStart();
    case PaletteCommand::SavePreset: return !m_state.changes().empty();
    case PaletteCommand::LoadPreset: return !busy; // without a mount it says why (toast)
    case PaletteCommand::OpenSource: return !m_state.mounted() && !busy;
    case PaletteCommand::Unmount: return m_state.mounted().has_value() && !busy;
    case PaletteCommand::ToggleTheme: return static_cast<bool>(m_services.toggleTheme);
    case PaletteCommand::ToggleNav:
    case PaletteCommand::OpenLogFolder: return true;
    }
    return false;
}

void Shell::runPaletteCommand(PaletteCommand command) {
    if (!paletteCommandAvailable(command)) {
        return;
    }
    switch (command) {
    case PaletteCommand::ApplyQueue:
        // Never straight from somewhere else: the Apply page is the review (what, in which order,
        // which risks). From there the same command starts the run.
        if (m_page != PageId::Apply) {
            showPage(PageId::Apply);
        } else {
            requestApply();
        }
        break;
    case PaletteCommand::SavePreset: newPreset(); break;
    case PaletteCommand::LoadPreset: loadPreset(); break;
    case PaletteCommand::OpenSource: pickSourceFile(); break;
    case PaletteCommand::Unmount: askUnmount(); break;
    case PaletteCommand::ToggleTheme: m_services.toggleTheme(); break;
    case PaletteCommand::ToggleNav: toggleNav(); break;
    case PaletteCommand::OpenLogFolder: openLogFolder(); break;
    }
}

bool Shell::handleShortcut(const ui::KeyEvent& key) {
    // A dialog or a menu is open: its keys are its own (Ctrl+B must not move the rail under it).
    if (host() && host()->hasModal()) {
        return false;
    }
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey == 'K') {
        openPalette();
        return true;
    }
    // interaction.md "Kısayollar": Ctrl+Enter applies the queue, Ctrl+S / Ctrl+O save / load a preset.
    if (key.ctrl && !key.shift && !key.alt &&
        (key.virtualKey == VK_RETURN || key.virtualKey == 'S' || key.virtualKey == 'O')) {
        runPaletteCommand(key.virtualKey == VK_RETURN ? PaletteCommand::ApplyQueue
                          : key.virtualKey == 'S'     ? PaletteCommand::SavePreset
                                                      : PaletteCommand::LoadPreset);
        return true;
    }
    // The mount folder is one keystroke away from every page; the image file and a fresh read of
    // the source from the Images page.
    if (key.ctrl && !key.alt && key.virtualKey == 'E') {
        if (key.shift) {
            revealImageFile();
        } else {
            exploreMount();
        }
        return true;
    }
    if (!key.ctrl && !key.shift && !key.alt && key.virtualKey == VK_F5 && m_page == PageId::Images) {
        refreshSource();
        return true;
    }
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
        toggleNav();
        return true;
    }
    if (key.ctrl && key.shift && key.virtualKey == 'T') {
        if (m_services.toggleTheme) {
            m_services.toggleTheme();
        }
        return true;
    }
#ifndef NDEBUG
    if (key.ctrl && key.shift && key.virtualKey == 'G') { // the developer widget gallery
        showPage(PageId::Gallery);
        return true;
    }
#endif
    if (key.ctrl && !key.shift && key.virtualKey == VK_OEM_COMMA) {
        showPage(PageId::Settings);
        return true;
    }
    if (!key.ctrl && !key.shift && !key.alt && key.virtualKey == VK_F1) {
        showPage(PageId::About);
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
