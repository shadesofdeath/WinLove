#include "app/pages/IsoPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "base/Log.h"
#include "core/usb/UsbMedia.h"
#include "ui/anim/Tween.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kLabelWidth = 220.0f;
constexpr float kRow = 32.0f;
constexpr float kSection = 36.0f;
constexpr float kSummaryWidth = 320.0f;
constexpr float kSummaryRow = 24.0f;
constexpr float kInfoBar = 32.0f;

std::filesystem::path desktop() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        result = path;
    }
    CoTaskMemFree(path);
    return result;
}

std::wstring defaultLabel(const core::SourceInfo& source) {
    if (!source.volumeLabel.empty()) {
        return source.volumeLabel.substr(0, 32);
    }
    return L"WINLOVE";
}
} // namespace

IsoPage::IsoPage(AppState& state, IsoController& controller, const Localization& strings, Language language,
                 Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_tabs = &add<ui::TabBar>(std::vector<std::wstring>{strings.get(Str::IsoTabIso), strings.get(Str::IsoTabUsb)}, 0);
    m_tabs->onChange = [this](int) {
        if (usbTab() && !m_disksRead) {
            refreshDisks();
        }
        refresh();
        notifyChanged();
    };
    m_bar = &add<ui::InfoBar>(ui::InfoKind::Info, L"", L"", strings.get(Str::CommonClose));
    m_bar->setVisible(false);
    m_bar->onClose = [this] {
        m_bar->setVisible(false);
        layout();
    };

    const auto& source = m_state.source();
    const std::wstring stem = source ? source->path.stem().wstring() : L"Windows";
    m_fileName = &add<ui::SearchBox>(L"");
    m_fileName->setPlain(true);
    m_fileName->setWidth(360);
    m_fileName->setText(stem + L"_WinLove.iso");
    m_folder = &add<ui::SearchBox>(L"");
    m_folder->setPlain(true);
    m_folder->setWidth(328);
    m_folder->setText((m_state.settings().isoFolder.empty() ? desktop() : m_state.settings().isoFolder).wstring());
    m_browse = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::OpenFolder);
    m_browse->setTooltip(strings.get(Str::IsoFolder));
    m_browse->onInvoke = [this] {
        if (m_intents.pickFolder) {
            if (const auto folder = m_intents.pickFolder()) {
                m_folder->setText(folder->wstring());
            }
        }
    };
    m_label = &add<ui::SearchBox>(L"");
    m_label->setPlain(true);
    m_label->setWidth(200);
    m_label->setText(source ? defaultLabel(*source) : L"WINLOVE");

    m_boot = &add<ui::RadioGroup>(std::vector<std::wstring>{strings.get(Str::IsoBootBoth), strings.get(Str::IsoBootUefi),
                                                              strings.get(Str::IsoBootBios)},
                                  0);
    m_boot->onChange = [this](int) { invalidate(); };
    m_repack = &add<ui::Dropdown>(L"",
                                  std::vector<std::wstring>{strings.get(Str::IsoRepackAsIs), strings.get(Str::IsoRepackLzx),
                                                            strings.get(Str::IsoRepackXpress), strings.get(Str::IsoRepackEsd)},
                                  0);
    m_repack->onChange = [this](int) { invalidate(); };
    if (!m_controller.canRepack()) {
        m_repack->setEnabled(false);
        m_repack->setTooltip(strings.get(Str::IsoRepackOnlyWork));
    }
    m_noPrompt = &add<ui::CheckField>(strings.get(Str::IsoNoPrompt), false);
    m_bootBypass = &add<ui::CheckField>(strings.get(Str::IsoBootBypass), true);
    m_bootBypass->onChange = [this](bool) { invalidate(); };
    m_legacySetup = &add<ui::CheckField>(strings.get(Str::IsoLegacySetup), false);
    m_legacySetup->onChange = [this](bool) {
        m_legacyTouched = true;
        invalidate();
    };
    // The row label says it: the boxes stand alone (screen 16).
    m_sha = &add<ui::CheckField>(L"", true);
    m_sha->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoSha));
    m_open = &add<ui::CheckField>(L"", true);
    m_open->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoOpenWhenDone));

    m_disk = &add<ui::Dropdown>(L"", std::vector<std::wstring>{strings.get(Str::IsoUsbNoDisk)}, 0);
    m_disk->onChange = [this](int) {
        refresh();
        notifyChanged();
    };
    m_refresh = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::Refresh);
    m_refresh->setTooltip(strings.get(Str::IsoUsbRefresh));
    m_refresh->onInvoke = [this] { refreshDisks(); };
    m_usbLabel = &add<ui::SearchBox>(L"");
    m_usbLabel->setPlain(true);
    m_usbLabel->setWidth(200);
    m_usbLabel->setText(source ? core::fatLabel(defaultLabel(*source)) : L"WINLOVE");
    m_usbLabel->onChange = [this](const std::wstring&) { notifyChanged(); };
    m_scheme = &add<ui::RadioGroup>(std::vector<std::wstring>{strings.get(Str::IsoUsbMbr), strings.get(Str::IsoUsbGpt)}, 0);
    m_scheme->onChange = [this](int) { invalidate(); };
    setAccessible(ui::AccessRole::Group, strings.get(Str::IsoTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Source) {
            computeSize(); // a re-read source (after a commit) is another size: USB check, estimate
            checkWinre();  // and maybe WinRE is gone now
        }
        if (change == AppState::Change::Iso || change == AppState::Change::Mount || change == AppState::Change::Source ||
            change == AppState::Change::Apply || change == AppState::Change::Operation ||
            change == AppState::Change::Unattend) {
            refresh();
        }
    });
    computeSize();
    checkWinre();
    refresh();
}

IsoPage::~IsoPage() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

void IsoPage::computeSize() {
    const auto& source = m_state.source();
    if (!source) {
        m_sourceBytes = 0;
        return;
    }
    std::error_code ec;
    if (source->format == core::ImageFormat::Iso) {
        const auto size = std::filesystem::file_size(source->path, ec);
        m_sourceBytes = ec ? 0 : size; // file_size answers -1 on an error
        return;
    }
    // Walking ~1000 files: on the reader thread, then repaint.
    const std::filesystem::path folder = source->path;
    std::weak_ptr<bool> alive = m_alive;
    auto bytes = std::make_shared<std::uint64_t>(0);
    m_state.reader().run<bool>(
        [folder, bytes](const core::TaskContext&) -> Result<bool> {
            *bytes = core::folderSize(folder);
            return true;
        },
        [this, alive, bytes, post = m_intents.postToUi](Result<bool>) {
            if (!post) {
                return;
            }
            post([this, alive, bytes] { // back on the UI thread
                if (const auto a = alive.lock(); a && *a) {
                    m_sourceBytes = *bytes;
                    refresh(); // the USB size check and the estimate read it
                }
            });
        });
}

void IsoPage::checkWinre() {
    const auto& source = m_state.source();
    if (!source) {
        m_noWinre.clear();
        return;
    }
    std::weak_ptr<bool> alive = m_alive;
    auto missing = std::make_shared<std::vector<int>>();
    m_state.reader().run<bool>(
        [info = *source, missing](const core::TaskContext&) -> Result<bool> {
            auto editions = core::editionsWithoutWinre(info);
            if (!editions) {
                // ESD, damaged: nothing known, the box stays as the user leaves it.
                log::warn("iso", L"cannot tell whether the editions have WinRE: " + describe(editions.error()));
                return false;
            }
            *missing = std::move(*editions);
            return true;
        },
        [this, alive, missing, post = m_intents.postToUi](Result<bool>) {
            if (!post) {
                return;
            }
            post([this, alive, missing] {
                if (const auto a = alive.lock(); a && *a) {
                    setEditionsWithoutWinre(std::move(*missing));
                }
            });
        });
}

void IsoPage::setEditionsWithoutWinre(std::vector<int> editions) {
    m_noWinre = std::move(editions);
    const auto& source = m_state.source();
    // Older media (Windows 10, D-077) installs a WinRE-less image with its own, previous Setup.
    if (!m_noWinre.empty() && !m_legacyTouched && !(source && core::mediaOpensPreviousSetup(*source))) {
        m_legacySetup->setChecked(true);
    }
    refresh();
}

IsoController::Request IsoPage::request() const {
    IsoController::Request r;
    std::wstring name = m_fileName->text();
    if (name.empty()) {
        name = L"WinLove.iso";
    }
    if (!name.ends_with(L".iso") && !name.ends_with(L".ISO")) {
        name += L".iso";
    }
    r.output = std::filesystem::path(m_folder->text()) / name;
    r.label = m_label->text();
    r.boot = static_cast<core::BootMode>(m_boot->selected());
    r.noPrompt = m_noPrompt->checked();
    r.repack = static_cast<IsoController::Repack>(m_controller.canRepack() ? m_repack->selected() : 0);
    r.sha256 = m_sha->checked();
    r.openFolder = m_open->checked();
    r.bootBypass = m_bootBypass->checked();
    r.legacySetup = m_legacySetup->checked();
    if (usbTab()) {
        if (const auto* disk = selectedDisk()) {
            r.usb = IsoController::Request::UsbTarget{disk->number, disk->identity(), disk->name(),
                                                      m_scheme->selected() == 1 ? core::UsbScheme::GptUefi
                                                                                : core::UsbScheme::MbrBiosUefi};
        }
        r.label = core::fatLabel(m_usbLabel->text());
        r.sha256 = false;
        r.noPrompt = false;
    }
    return r;
}

const core::UsbDisk* IsoPage::selectedDisk() const {
    const int i = m_disk->selected();
    return i >= 0 && i < static_cast<int>(m_disks.size()) ? &m_disks[static_cast<std::size_t>(i)] : nullptr;
}

void IsoPage::notifyChanged() {
    if (m_intents.changed) {
        m_intents.changed();
    }
}

void IsoPage::refreshDisks() {
    m_disksRead = true;
    std::weak_ptr<bool> alive = m_alive;
    auto disks = std::make_shared<std::vector<core::UsbDisk>>();
    // A slow card reader can hold an IOCTL for a moment: not on the UI thread.
    m_state.reader().run<bool>(
        [disks](const core::TaskContext&) -> Result<bool> {
            *disks = core::listUsbDisks();
            return true;
        },
        [this, alive, disks, post = m_intents.postToUi](Result<bool>) {
            if (!post) {
                return;
            }
            post([this, alive, disks] {
                if (const auto a = alive.lock(); a && *a) {
                    setDisks(std::move(*disks));
                }
            });
        });
}

void IsoPage::showUsbTab() {
    m_tabs->setSelected(1);
    refresh();
    notifyChanged();
}

void IsoPage::setDisks(std::vector<core::UsbDisk> disks) {
    m_disksRead = true;
    const std::wstring picked = selectedDisk() ? selectedDisk()->identity() : std::wstring();
    m_disks = std::move(disks);
    std::vector<std::wstring> items;
    int selected = 0;
    for (std::size_t i = 0; i < m_disks.size(); ++i) {
        const auto& d = m_disks[i];
        std::wstring letters;
        for (const auto& l : d.letters) {
            letters += (letters.empty() ? L" \u00b7 " : L" ") + l.substr(0, 2);
        }
        items.push_back(d.name() + L" \u00b7 " + formatBytes(d.size, m_language) + letters);
        if (d.identity() == picked) {
            selected = static_cast<int>(i);
        }
    }
    if (items.empty()) {
        items.push_back(m_strings.get(Str::IsoUsbNoDisk));
    }
    m_disk->setItems(std::move(items), selected);
    m_disk->setEnabled(!m_disks.empty());
    refresh();
    notifyChanged();
}

bool IsoPage::formValid() const {
    if (usbTab()) {
        const auto* disk = selectedDisk();
        if (!disk || m_usbLabel->text().empty()) {
            return false;
        }
        const std::uint64_t partition = core::usbPartitionMb(disk->size);
        const std::uint64_t room = partition ? partition * 1024 * 1024 : disk->size;
        return m_sourceBytes == 0 || m_sourceBytes + (64ull << 20) <= room;
    }
    return !m_folder->text().empty() && std::filesystem::path(m_folder->text()).is_absolute() && !m_label->text().empty();
}

double IsoPage::estimateSeconds() const {
    const auto& source = m_state.source();
    if (!source) {
        return 0;
    }
    const double mb = static_cast<double>(m_sourceBytes) / (1024.0 * 1024.0);
    double seconds = usbTab() ? mb / 40.0 : mb / 250.0; // a USB 3 stick / an SSD
    if (source->format == core::ImageFormat::Iso) {
        seconds += mb / 200.0;   // extract
    }
    if (m_repack->selected() != 0 && m_controller.canRepack()) {
        seconds += static_cast<double>(source->installImageSize) / (1024.0 * 1024.0) / 25.0;
    }
    if (m_sha->checked() && !usbTab()) {
        seconds += mb / 800.0;
    }
    if ((m_bootBypass->checked() && !IsoController::bootPatch(m_state).empty()) || m_legacySetup->checked()) {
        seconds += 30; // mount + commit of boot.wim's setup image
    }
    return seconds;
}

std::vector<int> IsoPage::mediaCannotInstall() const {
    if (m_cannotInstallDemo) {
        return *m_cannotInstallDemo;
    }
    const auto& source = m_state.source();
    return source ? core::editionsMediaCannotInstall(*source) : std::vector<int>{};
}

void IsoPage::setMediaCannotInstallDemo(std::vector<int> editions) {
    m_cannotInstallDemo = std::move(editions);
    refresh();
}

void IsoPage::updateBlocker() {
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        m_bar->setVisible(false);
        return;
    }
    if (run && run->result && run->usb) {
        m_bar->set(ui::InfoKind::Success, m_strings.get(Str::IsoUsbDone),
                   m_strings.format(Str::IsoDoneBody, {{L"path", run->output.wstring()},
                                                       {L"size", formatBytes(run->result->bytes, m_language)}}));
        const auto output = run->output;
        m_bar->setAction(m_strings.get(Str::IsoOpenFolder), [this, output] {
            if (m_intents.openFolder) {
                m_intents.openFolder(output); // the drive, selected in This PC
            }
        });
        m_bar->setVisible(true);
        return;
    }
    if (run && run->result) {
        m_bar->set(ui::InfoKind::Success, m_strings.get(Str::IsoDone),
                   m_strings.format(Str::IsoDoneBody, {{L"path", run->output.wstring()},
                                                       {L"size", formatBytes(run->result->bytes, m_language)}}));
        const auto output = run->output;
        m_bar->setAction(m_strings.get(Str::IsoOpenFolder), [this, output] {
            if (m_intents.openFolder) {
                m_intents.openFolder(output);
            }
        });
        m_bar->setVisible(true);
        return;
    }
    if (run && run->error) {
        const bool cancelled = run->error->code == ErrorCode::Cancelled;
        const Str title = run->usb ? (cancelled ? Str::IsoUsbCancelled : Str::IsoUsbFailed)
                                   : (cancelled ? Str::IsoCancelled : Str::IsoFailed);
        m_bar->set(cancelled ? ui::InfoKind::Warning : ui::InfoKind::Error, m_strings.get(title),
                   cancelled ? std::wstring() : run->error->message + L" — " + run->error->context);
        m_bar->setAction(L"", nullptr);
        m_bar->setVisible(true);
        return;
    }
    if (const auto blocker = m_controller.blocker()) {
        Str text = Str::IsoBlockNoSource;
        switch (*blocker) {
        case IsoController::Blocker::NoSource: text = Str::IsoBlockNoSource; break;
        case IsoController::Blocker::WimOnly: text = Str::IsoBlockWimOnly; break;
        case IsoController::Blocker::Mounted: text = Str::IsoBlockMounted; break;
        case IsoController::Blocker::Busy: text = Str::IsoBlockBusy; break;
        case IsoController::Blocker::UnattendInvalid: text = Str::IsoBlockUnattend; break;
        }
        m_bar->set(ui::InfoKind::Warning, m_strings.get(text), L"");
        m_bar->setAction(L"", nullptr);
        m_bar->setVisible(true);
        return;
    }
    // D-077: Windows 10 editions under 24H2+ setup media would fail at install time.
    const auto& source = m_state.source();
    const std::vector<int> cannot = mediaCannotInstall();
    if (!cannot.empty() && source) {
        std::wstring list;
        for (const int index : cannot) {
            const auto image = std::ranges::find(source->install.images, index, &core::ImageInfo::index);
            list += (list.empty() ? L"" : L", ") +
                    (image != source->install.images.end() ? image->name : std::format(L"index {}", index));
        }
        m_bar->set(ui::InfoKind::Warning, m_strings.get(Str::IsoAioTitle), m_strings.format(Str::IsoAioBody, {{L"list", list}}));
        if (m_intents.replaceMedia) {
            m_bar->setAction(m_strings.get(Str::IsoAioAction), [this] { m_intents.replaceMedia(); });
        } else {
            m_bar->setAction(L"", nullptr);
        }
        m_bar->setVisible(true);
        return;
    }
    if (usbTab()) {
        // Always in view before the button: what a stick write does to the drive.
        const auto* disk = selectedDisk();
        const std::uint64_t partition = disk ? core::usbPartitionMb(disk->size) : 0;
        const std::uint64_t room = disk ? (partition ? partition * 1024 * 1024 : disk->size) : 0;
        if (disk && m_sourceBytes && m_sourceBytes + (64ull << 20) > room) {
            m_bar->set(ui::InfoKind::Error,
                       m_strings.format(Str::IsoUsbTooSmall, {{L"need", formatBytes(m_sourceBytes, m_language)}}), L"");
        } else if (m_disksRead && m_disks.empty()) {
            m_bar->set(ui::InfoKind::Info, m_strings.get(Str::IsoUsbNoDisk), L"");
        } else {
            m_bar->set(ui::InfoKind::Warning, m_strings.get(Str::IsoUsbErase), L"");
        }
        m_bar->setAction(L"", nullptr);
        m_bar->setVisible(true);
        return;
    }
    m_bar->setVisible(false);
}

void IsoPage::refresh() {
    const bool isoTab = m_tabs->selected() == 0;
    const auto& run = m_state.isoRun();
    const bool running = run && run->running;
    // Nothing to write into Setup's image while the answers switch no check off.
    const bool bypasses = !IsoController::bootPatch(m_state).empty();
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_fileName, m_folder, m_browse, m_label, m_boot, m_noPrompt,
                                                            m_sha}) {
        w->setVisible(isoTab);
        w->setEnabled(!running);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_disk, m_refresh, m_usbLabel, m_scheme}) {
        w->setVisible(!isoTab);
        w->setEnabled(!running && (w != m_disk || !m_disks.empty()));
    }
    // Shared by both tabs. Media older than 24H2 opens the previous Setup already: nothing to switch.
    const auto& source = m_state.source();
    const bool previousMedia = source && core::mediaOpensPreviousSetup(*source) && !m_cannotInstallDemo;
    if (previousMedia && m_legacySetup->checked()) {
        m_legacySetup->setChecked(false);
    }
    m_legacySetup->setEnabled(!running && !previousMedia);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_repack, m_bootBypass, m_open}) {
        w->setEnabled(!running && (w != m_repack || m_controller.canRepack()) && (w != m_bootBypass || bypasses));
    }
    m_bootBypass->setTooltip(bypasses ? std::wstring() : m_strings.get(Str::IsoBootBypassNone));
    m_open->setAccessible(ui::AccessRole::CheckBox, m_strings.get(isoTab ? Str::IsoOpenWhenDone : Str::IsoUsbOpenWhenDone));
    updateBlocker();
    layout();
    invalidate();
}

void IsoPage::layout() {
    const RectF b = bounds();
    float y = b.y + kTop;
    m_tabs->setBounds({b.x, y, b.width, ui::tokens::size::control + 2});
    y += ui::tokens::size::control + 2 + 12;
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        y += kInfoBar + 12; // progress row (painted)
    } else if (m_bar->visible()) {
        m_bar->setBounds({b.x, y, b.width - kSummaryWidth - 24, kInfoBar});
        y += kInfoBar + 12;
    }
    const float fieldX = b.x + kLabelWidth;
    auto place = [&](ui::Widget* w, float width) {
        const ui::SizeF size = w->measure({});
        w->setBounds({fieldX, y + (kRow - ui::tokens::size::control) / 2, width > 0 ? width : size.width,
                      ui::tokens::size::control});
    };
    if (usbTab()) {
        y += kSection; // USB BELLEK
        place(m_disk, 360);
        m_refresh->setBounds({fieldX + 364, y + (kRow - ui::tokens::size::control) / 2, 28, ui::tokens::size::control});
        y += kRow;
        place(m_usbLabel, 200);
        y += kRow + kSection; // ÖNYÜKLEME
        place(m_scheme, 0);
        y += kRow;
        place(m_repack, 240);
        y += kRow;
        place(m_bootBypass, 0);
        y += kRow;
        place(m_legacySetup, 0);
        y += kRow + kSection; // BİTİNCE
        place(m_open, 0);
        return;
    }
    y += kSection; // ÇIKTI
    place(m_fileName, 360);
    y += kRow;
    place(m_folder, 328);
    m_browse->setBounds({fieldX + 332, y + (kRow - ui::tokens::size::control) / 2, 28, ui::tokens::size::control});
    y += kRow;
    place(m_label, 200);
    y += kRow + kSection; // ÖNYÜKLEME
    place(m_boot, 0);
    y += kRow;
    place(m_repack, 240);
    y += kRow;
    place(m_noPrompt, 0);
    y += kRow;
    place(m_bootBypass, 0);
    y += kRow;
    place(m_legacySetup, 0);
    y += kRow + kSection; // DOĞRULAMA
    place(m_sha, 0);
    y += kRow;
    place(m_open, 0);
}

void IsoPage::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float formRight = b.right() - kSummaryWidth - 24;
    float y = b.y + kTop + ui::tokens::size::control + 2 + 12;
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        static constexpr Str kStages[] = {Str::IsoStageExtract, Str::IsoStageRepack, Str::IsoStageWrite, Str::IsoStageSha,
                                          Str::IsoStageBoot,    Str::IsoStageUsb};
        const std::wstring text = std::format(L"{} · {}%", m_strings.get(kStages[std::clamp(run->stage, 0, 5)]),
                                              static_cast<int>(run->fraction * 100));
        canvas.drawIcon(ui::icons::Icon::Spinner, {b.x, y + 4}, Color::AccentBase);
        canvas.drawText(text, {b.x + 24, y, formRight - b.x - 24, 20}, TypeStyle::BodyStrong, Color::TextPrimary);
        canvas.progressBar({b.x, y + 24, formRight - b.x, 2}, static_cast<float>(run->fraction));
        y += kInfoBar + 12;
    } else if (m_bar->visible()) {
        y += kInfoBar + 12;
    }
    if (usbTab()) {
        paintUsbForm(canvas, y, formRight);
    } else {
        paintIsoForm(canvas, y, formRight);
    }
}

void IsoPage::paintIsoForm(ui::Canvas& canvas, float y, float formRight) {
    const RectF b = bounds();
    // Section headers + field labels.
    auto section = [&](Str title) {
        paintFormSection(canvas, {b.x, y, formRight - b.x, kSection}, m_strings.get(title));
        y += kSection;
    };
    auto label = [&](Str text) {
        canvas.drawText(m_strings.get(text), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        y += kRow;
    };
    section(Str::IsoOutput);
    label(Str::IsoFileName);
    label(Str::IsoFolder);
    label(Str::IsoLabel);
    section(Str::IsoBoot);
    label(Str::IsoBootMode);
    label(Str::IsoCompression);
    label(Str::IsoPrompt);
    label(Str::IsoSetupImage);
    label(Str::IsoSetupUi);
    paintLegacyHint(canvas, formRight);
    section(Str::IsoVerify);
    label(Str::IsoSha);
    label(Str::IsoOpenWhenDone);

    // Summary box.
    // P13: the answer file always has its row — that it is NOT going into the ISO is worth seeing
    // before the build, not at Setup's first question.
    const auto& unattend = m_state.unattend();
    const bool answersUnused = !unattend.includeInIso && !(unattend.options == core::UnattendOptions{});
    const RectF box{b.right() - kSummaryWidth, b.y + kTop + ui::tokens::size::control + 2 + 12, kSummaryWidth,
                    136 + 3 * kSummaryRow};
    canvas.fillRoundRect(box, ui::tokens::radius::r3, Color::BgPanel);
    canvas.strokeRoundRect(box, ui::tokens::radius::r3, Color::LineSubtle);
    float sy = box.y + 12;
    canvas.drawText(m_strings.get(Str::IsoSummary), {box.x + 16, sy, box.width - 32, 20}, TypeStyle::Section,
                    Color::TextSecondary);
    sy += 28;
    auto row = [&](Str key, const std::wstring& value, bool mono, Color ink = Color::TextPrimary) {
        canvas.drawText(m_strings.get(key), {box.x + 16, sy, 96, kSummaryRow}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {box.x + 112, sy, box.width - 128, kSummaryRow}, mono ? TypeStyle::Mono : TypeStyle::Caption,
                        ink);
        sy += kSummaryRow;
    };
    const auto& source = m_state.source();
    std::wstring sourceLine = L"—";
    if (source) {
        sourceLine = m_strings.format(Str::IsoSourceLine,
                                      {{L"file", std::filesystem::path(source->installImage).filename().wstring()},
                                       {L"n", std::to_wstring(source->install.images.size())},
                                       {L"size", formatBytes(source->installImageSize, m_language)}});
    }
    row(Str::IsoSource, sourceLine, false);
    static constexpr Str kBootText[] = {Str::IsoBootSummaryBoth, Str::IsoBootSummaryUefi, Str::IsoBootSummaryBios};
    row(Str::IsoBoot, m_strings.get(kBootText[std::clamp(m_boot->selected(), 0, 2)]), false);
    if (unattend.includeInIso) {
        row(Str::IsoUnattend, L"autounattend.xml", true);
    } else if (answersUnused) {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendOff), false, Color::StatusWarning);
    } else {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendNone), false, Color::TextSecondary);
    }
    // Setup's own image: what the build writes into boot.wim (D-038, D-074).
    {
        const std::wstring text = setupImageText();
        row(Str::IsoSetupImage, text.empty() ? m_strings.get(Str::IsoBootUntouched) : text, false,
            text.empty() ? Color::TextSecondary : Color::TextPrimary);
    }
    {
        const auto [text, ink] = setupUiSummary();
        row(Str::IsoSetupUi, text, false, ink);
    }
    row(Str::IsoEstIso, m_sourceBytes ? formatBytes(m_sourceBytes, m_language) : std::wstring(L"…"), true);
    row(Str::IsoDuration, m_sourceBytes ? formatDuration(estimateSeconds(), m_language, true) : std::wstring(L"…"),
        true);
}

// What the build writes into boot.wim; empty: nothing.
std::wstring IsoPage::setupImageText() const {
    const std::size_t checks = m_bootBypass->checked() ? IsoController::bootPatch(m_state).labConfigValues().size() : 0;
    if (checks > 0) {
        return m_strings.format(Str::IsoBootBypassN, {{L"n", std::to_wstring(checks)}});
    }
    // The previous Setup has a row of its own ("Kurulum ekranı").
    return m_legacySetup->checked() ? m_strings.get(Str::IsoBootPatched) : std::wstring();
}

std::pair<std::wstring, Color> IsoPage::setupUiSummary() const {
    const auto& source = m_state.source();
    if (!mediaCannotInstall().empty()) {
        // D-077: neither the new nor the previous Setup of 24H2+ media installs Windows 10.
        return {m_strings.get(Str::IsoAioSummary), Color::StatusWarning};
    }
    if (source && core::mediaOpensPreviousSetup(*source)) {
        return {m_strings.get(Str::IsoLegacySetupMedia), Color::TextSecondary};
    }
    if (m_legacySetup->checked()) {
        return {m_strings.get(Str::IsoLegacySetupPrevious), Color::TextPrimary};
    }
    if (!m_noWinre.empty()) {
        return {m_strings.get(Str::IsoLegacySetupNewFails), Color::StatusWarning};
    }
    return {m_strings.get(Str::IsoLegacySetupNew), Color::TextSecondary};
}

// Beside the box: what it does, or why it is on by itself.
void IsoPage::paintLegacyHint(ui::Canvas& canvas, float formRight) {
    const RectF box = m_legacySetup->bounds();
    const float x = box.right() + 12;
    if (x >= formRight) {
        return;
    }
    const auto& source = m_state.source();
    if (source && core::mediaOpensPreviousSetup(*source) && !m_cannotInstallDemo) {
        canvas.drawText(m_strings.get(Str::IsoLegacySetupClassic), {x, box.y, formRight - x, box.height}, TypeStyle::Caption,
                        Color::TextTertiary);
        return;
    }
    const bool noWinre = !m_noWinre.empty();
    canvas.drawText(m_strings.get(noWinre ? Str::IsoLegacySetupNoWinre : Str::IsoLegacySetupHint),
                    {x, box.y, formRight - x, box.height}, TypeStyle::Caption,
                    noWinre && !m_legacySetup->checked() ? Color::StatusWarning
                                                         : (noWinre ? Color::TextSecondary : Color::TextTertiary));
}

void IsoPage::paintUsbForm(ui::Canvas& canvas, float y, float formRight) {
    const RectF b = bounds();
    auto section = [&](Str title) {
        paintFormSection(canvas, {b.x, y, formRight - b.x, kSection}, m_strings.get(title));
        y += kSection;
    };
    auto label = [&](Str text) {
        canvas.drawText(m_strings.get(text), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        y += kRow;
    };
    section(Str::IsoUsbSection);
    label(Str::IsoUsbDisk);
    {
        const float hx = b.x + kLabelWidth + 208;
        canvas.drawText(m_strings.get(Str::IsoUsbLabelHint), {hx, y, std::max(formRight - hx, 0.0f), kRow},
                        TypeStyle::Caption, Color::TextTertiary);
    }
    label(Str::IsoLabel);
    section(Str::IsoBoot);
    label(Str::IsoUsbScheme);
    label(Str::IsoCompression);
    label(Str::IsoSetupImage);
    label(Str::IsoSetupUi);
    paintLegacyHint(canvas, formRight);
    section(Str::IsoUsbWhenDone);
    label(Str::IsoUsbOpenWhenDone);

    // Summary box.
    const auto& unattend = m_state.unattend();
    const bool answersUnused = !unattend.includeInIso && !(unattend.options == core::UnattendOptions{});
    const RectF box{b.right() - kSummaryWidth, b.y + kTop + ui::tokens::size::control + 2 + 12, kSummaryWidth,
                    136 + 4 * kSummaryRow};
    canvas.fillRoundRect(box, ui::tokens::radius::r3, Color::BgPanel);
    canvas.strokeRoundRect(box, ui::tokens::radius::r3, Color::LineSubtle);
    float sy = box.y + 12;
    canvas.drawText(m_strings.get(Str::IsoSummary), {box.x + 16, sy, box.width - 32, 20}, TypeStyle::Section,
                    Color::TextSecondary);
    sy += 28;
    auto row = [&](Str key, const std::wstring& value, bool mono, Color ink = Color::TextPrimary) {
        canvas.drawText(m_strings.get(key), {box.x + 16, sy, 96, kSummaryRow}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {box.x + 112, sy, box.width - 128, kSummaryRow}, mono ? TypeStyle::Mono : TypeStyle::Caption,
                        ink);
        sy += kSummaryRow;
    };
    const auto& source = m_state.source();
    std::wstring sourceLine = L"—";
    if (source) {
        sourceLine = m_strings.format(Str::IsoSourceLine,
                                      {{L"file", std::filesystem::path(source->installImage).filename().wstring()},
                                       {L"n", std::to_wstring(source->install.images.size())},
                                       {L"size", formatBytes(source->installImageSize, m_language)}});
    }
    row(Str::IsoSource, sourceLine, false);
    const auto* disk = selectedDisk();
    row(Str::IsoUsbDisk, disk ? disk->name() + L" \u00b7 " + formatBytes(disk->size, m_language) : std::wstring(L"—"), false,
        disk ? Color::TextPrimary : Color::TextTertiary);
    row(Str::IsoUsbScheme, m_strings.get(m_scheme->selected() == 1 ? Str::IsoUsbGpt : Str::IsoUsbMbr), false);
    const bool split = source && source->installImageSize > core::kFat32FileLimit && m_repack->selected() == 0;
    row(Str::IsoUsbInstall, m_strings.get(split ? Str::IsoUsbSplit : Str::IsoUsbAsIs), false);
    if (unattend.includeInIso) {
        row(Str::IsoUnattend, L"autounattend.xml", true);
    } else if (answersUnused) {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendOff), false, Color::StatusWarning);
    } else {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendNone), false, Color::TextSecondary);
    }
    {
        const std::wstring text = setupImageText();
        row(Str::IsoSetupImage, text.empty() ? m_strings.get(Str::IsoBootUntouched) : text, false,
            text.empty() ? Color::TextSecondary : Color::TextPrimary);
    }
    {
        const auto [text, ink] = setupUiSummary();
        row(Str::IsoSetupUi, text, false, ink);
    }
    row(Str::IsoDuration, m_sourceBytes ? formatDuration(estimateSeconds(), m_language, true) : std::wstring(L"…"),
        true);
}

} // namespace wl::app
