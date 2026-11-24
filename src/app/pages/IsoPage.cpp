#include "app/pages/IsoPage.h"

#include "app/Format.h"
#include "app/controllers/ImageDriverController.h"
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
constexpr float kTop = 0.0f;
constexpr float kLabelWidth = 190.0f; // D-136: the longest label at 13 px with room; the fields keep the rest
constexpr float kRow = ui::tokens::size::control + ui::tokens::spacing::s2; // D-136: a control and a little air
constexpr float kSection = ui::FormView::kSection; // the forms' title block (paintFormSection)
constexpr float kSummaryRow = 26.0f;
constexpr float kFormGap = 24.0f;  // the form ↔ the summary card
constexpr float kButtonGap = ui::tokens::spacing::s2; // a field ↔ its icon button (browse, refresh)
constexpr float kIconButton = ui::tokens::size::control;

// D-136: the summary card is 320 wide, narrower (to 280) on a narrow page: the form's fields (the
// boot mode's three segments) keep their room and nothing reaches into the card.
float summaryWidth(float pageWidth) {
    return std::clamp(std::floor(pageWidth * 0.36f), 280.0f, 320.0f);
}
constexpr float kInfoBar = ui::InfoBar::kHeight;

// Whether the answer file says anything at all (audit PG-19: paint built an empty one to compare
// with on every frame).
bool noAnswers(const core::UnattendOptions& options) {
    static const core::UnattendOptions kNone{};
    return options == kNone;
}

// D-134: why a driver did not go into boot.wim, in the user's words.
std::wstring refusalText(const core::DriverRefusal& refusal, const Localization& strings) {
    using Why = core::DriverRefusal::Why;
    Str key = Str::IsoBootRefusedDism;
    switch (refusal.why) {
    case Why::Missing: key = Str::IsoBootRefusedMissing; break;
    case Why::Architecture: key = Str::IsoBootRefusedArch; break;
    case Why::NoCatalog:
    case Why::CatalogMissing: key = Str::IsoBootRefusedNoCatalog; break;
    case Why::Unsigned: key = Str::IsoBootRefusedUnsigned; break;
    case Why::Dism: key = Str::IsoBootRefusedDism; break;
    }
    return refusal.inf.filename().wstring() + L": " + strings.format(key, {{L"arch", refusal.detail}, {L"why", refusal.detail}});
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
    m_ring = &add<ui::ProgressRing>(ui::tokens::size::ring);
    m_ring->setVisible(false);
    m_bar->onClose = [this] {
        m_bar->setVisible(false);
        layout();
    };

    const auto& source = m_state.source();
    // D-144: the same first values as "Bitince ISO oluştur" (IsoController::defaultRequest).
    m_fileName = &add<ui::SearchBox>(L"");
    m_fileName->setPlain(true);
    m_fileName->setWidth(360);
    m_fileName->setText(IsoController::defaultFileName(m_state));
    m_folder = &add<ui::SearchBox>(L"");
    m_folder->setPlain(true);
    m_folder->setWidth(328);
    m_folder->setText(IsoController::defaultFolder(m_state).wstring());
    m_browse = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::OpenFolder);
    m_browse->setTooltip(strings.get(Str::IsoFolder));
    m_browse->onInvoke = [this] {
        if (m_intents.pickFolder) {
            if (const auto folder = m_intents.pickFolder()) {
                m_folder->setText(folder->wstring());
            }
        }
    };
    // D-144 addendum: the options the Apply page's "ISO seçenekleri…" shares (AppState::isoChoices).
    m_options = std::make_unique<IsoOptionControls>(*this, m_state, strings,
                                                    IsoOptionControls::Hooks{m_intents.postToUi, [this] {
                                                        invalidate();
                                                        notifyChanged(); // the label: the header button follows
                                                    }});
    // The row label says it: the box stands alone (screen 16).
    m_open = &add<ui::CheckField>(L"", m_state.isoChoices().openFolder);
    m_open->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoOpenWhenDone));
    m_open->onChange = [this](bool on) {
        AppState::IsoChoices choices = m_state.isoChoices();
        choices.openFolder = on;
        m_state.setIsoChoices(std::move(choices));
    };

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
    m_usbLabel->setText(source ? core::fatLabel(IsoController::defaultLabel(*source)) : L"WINLOVE");
    m_usbLabel->onChange = [this](const std::wstring&) { notifyChanged(); };
    m_scheme = &add<ui::RadioGroup>(std::vector<std::wstring>{strings.get(Str::IsoUsbMbr), strings.get(Str::IsoUsbGpt)}, 0);
    m_scheme->onChange = [this](int) { invalidate(); };
    setAccessible(ui::AccessRole::Group, strings.get(Str::IsoTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Source) {
            computeSize();           // a re-read source (after a commit) is another size: USB check, estimate
            m_options->checkWinre(); // and maybe WinRE is gone now
        }
        if (change == AppState::Change::Iso || change == AppState::Change::Mount || change == AppState::Change::Source ||
            change == AppState::Change::Apply || change == AppState::Change::Operation ||
            change == AppState::Change::Unattend || change == AppState::Change::Drivers || change == AppState::Change::Queue) {
            refresh(); // D-134: Drivers / Queue — the boot.wim drivers and whether the image gets them too
        }
    });
    computeSize();
    m_options->checkWinre();
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

void IsoPage::setEditionsWithoutWinre(std::vector<int> editions) {
    m_options->setEditionsWithoutWinre(std::move(editions));
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
    const IsoOptionControls& o = *m_options;
    r.label = o.label->text();
    r.boot = static_cast<core::BootMode>(o.boot->selected());
    r.noPrompt = o.noPrompt->checked();
    r.repack = static_cast<IsoController::Repack>(m_controller.canRepack() ? o.repack->selected() : 0);
    r.sha256 = o.sha->checked();
    r.openFolder = m_open->checked();
    r.bootBypass = o.bootBypass->checked();
    r.legacySetup = o.legacySetup->checked();
    r.mediaUpdate = o.mediaUpdate->checked() && m_state.mediaUpdate().any();
    r.secureBoot2023 = o.secureBoot->checked();
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
    return !m_folder->text().empty() && std::filesystem::path(m_folder->text()).is_absolute() && !m_options->label->text().empty();
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
    if (m_options->repack->selected() != 0 && m_controller.canRepack()) {
        seconds += static_cast<double>(source->installImageSize) / (1024.0 * 1024.0) / 25.0;
    }
    if (m_options->sha->checked() && !usbTab()) {
        seconds += mb / 800.0;
    }
    if (!IsoController::bootPatch(m_state, m_options->bootBypass->checked()).empty() || m_options->legacySetup->checked()) {
        seconds += 30 + 3.0 * static_cast<double>(m_state.bootDrivers().size()); // mount + commit; a driver ~1–3 s (lab)
    }
    if (m_options->mediaUpdate->checked() && m_state.mediaUpdate().any()) {
        // Lab, 2026-10-07: the cumulative update into both images of boot.wim took 430 s.
        seconds += (m_state.mediaUpdate().lcu.empty() ? 0 : 450) + (m_state.mediaUpdate().setupDu.empty() ? 0 : 10);
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
    m_options->setIgnoreMediaSetup(true);
    refresh();
}

void IsoPage::updateBlocker() {
    const auto& run = m_state.isoRun();
    m_bar->setTooltip(std::wstring()); // set below where the line runs past the bar (D-134)
    if (run && run->running) {
        m_bar->setVisible(false);
        return;
    }
    // D-134: a driver boot.wim did not take does not fail the build — the result says which and why.
    std::wstring refused; // first in the line: the path is one click away ("Klasörü aç")
    if (run && run->result) {
        for (const auto& r : run->bootDriversRefused) {
            refused += refusalText(r, m_strings) + L" · ";
        }
    }
    const bool anyRefused = run && run->result && !run->bootDriversRefused.empty();
    const std::wstring refusedN = anyRefused ? std::to_wstring(run->bootDriversRefused.size()) : std::wstring();
    if (run && run->result && run->usb) {
        const std::wstring body = refused + m_strings.format(Str::IsoDoneBody, {{L"path", run->output.wstring()},
                                                                                {L"size", formatBytes(run->result->bytes, m_language)}});
        m_bar->set(anyRefused ? ui::InfoKind::Warning : ui::InfoKind::Success,
                   anyRefused ? m_strings.format(Str::IsoUsbDoneRefused, {{L"n", refusedN}}) : m_strings.get(Str::IsoUsbDone), body);
        if (anyRefused) {
            m_bar->setTooltip(body);
        }
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
        const std::wstring body = refused + m_strings.format(Str::IsoDoneBody, {{L"path", run->output.wstring()},
                                                                                {L"size", formatBytes(run->result->bytes, m_language)}});
        m_bar->set(anyRefused ? ui::InfoKind::Warning : ui::InfoKind::Success,
                   anyRefused ? m_strings.format(Str::IsoDoneRefused, {{L"n", refusedN}}) : m_strings.get(Str::IsoDone), body);
        if (anyRefused) {
            m_bar->setTooltip(body);
        }
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
        case IsoController::Blocker::VirtualDisk: text = Str::IsoBlockVirtualDisk; break;
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
    // D-134: a storage driver Setup gets but the installed Windows does not — it would boot from the
    // very controller it has no driver for.
    if (!m_storageOnlyInBoot.empty()) {
        std::wstring names;
        for (const auto& p : m_storageOnlyInBoot) {
            names += (names.empty() ? L"" : L", ") + p.filename().wstring();
        }
        const std::wstring body = m_strings.format(Str::IsoBootStorageOnlyBody, {{L"names", names}});
        m_bar->set(ui::InfoKind::Warning, m_strings.get(Str::IsoBootStorageOnlyTitle), body);
        m_bar->setTooltip(body);
        if (m_intents.goDrivers) {
            m_bar->setAction(m_strings.get(Str::IsoBootStorageOnlyAction), [this] { m_intents.goDrivers(); });
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
    syncBootDrivers();
    // The shared options from AppState::isoChoices (values, enabled states, tooltips), then what the tab shows.
    m_options->sync(running);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_fileName, m_folder, m_browse}) {
        w->setVisible(isoTab);
        w->setEnabled(!running);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_options->label, m_options->boot, m_options->noPrompt, m_options->sha}) {
        w->setVisible(isoTab);
    }
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_disk, m_refresh, m_usbLabel, m_scheme}) {
        w->setVisible(!isoTab);
        w->setEnabled(!running && (w != m_disk || !m_disks.empty()));
    }
    m_open->setChecked(m_state.isoChoices().openFolder);
    m_open->setEnabled(!running);
    m_open->setAccessible(ui::AccessRole::CheckBox, m_strings.get(isoTab ? Str::IsoOpenWhenDone : Str::IsoUsbOpenWhenDone));
    m_ring->setRunning(running);
    m_ring->setVisible(running);
    updateBlocker();
    layout();
    invalidate();
}

void IsoPage::layout() {
    const RectF b = bounds();
    const float before = m_contentHeight;
    float y = b.y + kTop;
    m_tabs->setBounds({b.x, y, b.width, ui::tokens::size::control + 2});
    y += ui::tokens::size::control + 2 + 12;
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        // The progress row (painted), its ring a widget of its own (D-142): it turns while it is drawn.
        m_ring->setBounds({b.x, y + 4, ui::tokens::size::ring, ui::tokens::size::ring});
        y += kInfoBar + 12;
    } else if (m_bar->visible()) {
        m_bar->setBounds({b.x, y, b.width - summaryWidth(b.width) - kFormGap, kInfoBar});
        y += kInfoBar + 12;
    }
    const float fieldX = b.x + kLabelWidth;
    // A control ends where the form does, 24 before the summary card (a long check label ran up to
    // the card's edge); its text ends in "…" there.
    const float formRight = b.right() - summaryWidth(b.width) - kFormGap;
    auto place = [&](ui::Widget* w, float width) {
        const ui::SizeF size = w->measure({});
        w->setBounds({fieldX, y + (kRow - ui::tokens::size::control) / 2,
                      std::max(std::min(width > 0 ? width : size.width, formRight - fieldX), 0.0f), ui::tokens::size::control});
    };
    // A field with an icon button after it (browse, refresh): the two end where the form does.
    auto placeWithButton = [&](ui::Widget* w, float width, ui::Widget* button) {
        place(w, std::min(width, std::max(formRight - fieldX - kButtonGap - kIconButton, 0.0f)));
        button->setBounds({w->bounds().right() + kButtonGap, y + (kRow - kIconButton) / 2, kIconButton, kIconButton});
    };
    if (usbTab()) {
        y += kSection; // USB BELLEK
        placeWithButton(m_disk, 360, m_refresh);
        y += kRow;
        place(m_usbLabel, 200);
        y += kRow + kSection; // ÖNYÜKLEME
        place(m_scheme, 0);
        y += kRow;
        place(m_options->repack, 240);
        y += kRow;
        place(m_options->bootBypass, 0);
        y += kRow;
        place(m_options->legacySetup, 0);
        y += kRow;
        place(m_options->mediaUpdate, 0);
        y += kRow;
        place(m_options->secureBoot, 0);
        y += kRow + kSection; // BİTİNCE
        place(m_open, 0);
        m_contentHeight = y + kRow + 12 - b.y;
        contentChanged(*this, before);
        return;
    }
    y += kSection; // ÇIKTI
    place(m_fileName, 360);
    y += kRow;
    placeWithButton(m_folder, 328, m_browse);
    y += kRow;
    place(m_options->label, 200);
    y += kRow + kSection; // ÖNYÜKLEME
    place(m_options->boot, 0);
    y += kRow;
    place(m_options->repack, 240);
    y += kRow;
    place(m_options->noPrompt, 0);
    y += kRow;
    place(m_options->bootBypass, 0);
    y += kRow;
    place(m_options->legacySetup, 0);
    y += kRow;
    place(m_options->mediaUpdate, 0);
    y += kRow;
    place(m_options->secureBoot, 0);
    y += kRow + kSection; // DOĞRULAMA
    place(m_options->sha, 0);
    y += kRow;
    place(m_open, 0);
    m_contentHeight = y + kRow + 12 - b.y;
    contentChanged(*this, before);
}

void IsoPage::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float formRight = b.right() - summaryWidth(b.width) - kFormGap;
    float y = b.y + kTop + ui::tokens::size::control + 2 + 12;
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        static constexpr Str kStages[] = {Str::IsoStageExtract, Str::IsoStageRepack, Str::IsoStageWrite, Str::IsoStageSha,
                                          Str::IsoStageBoot,    Str::IsoStageUsb};
        const std::wstring text = std::format(L"{} · {}%", m_strings.get(kStages[std::clamp(run->stage, 0, 5)]),
                                              static_cast<int>(run->fraction * 100));
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
    m_options->paintLegacyHint(canvas, formRight);
    label(Str::IsoMediaUpdate);
    m_options->paintMediaHint(canvas, formRight);
    label(Str::IsoSecureBoot);
    m_options->paintSecureBootHint(canvas, formRight);
    section(Str::IsoVerify);
    label(Str::IsoSha);
    label(Str::IsoOpenWhenDone);

    // Summary box.
    // P13: the answer file always has its row — that it is NOT going into the ISO is worth seeing
    // before the build, not at Setup's first question.
    const auto& unattend = m_state.unattend();
    const bool answersUnused = !unattend.includeInIso && !noAnswers(unattend.options);
    const RectF box{b.right() - summaryWidth(b.width), b.y + kTop + ui::tokens::size::control + 2 + 12, summaryWidth(b.width),
                    52 + 10 * kSummaryRow};
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
    row(Str::IsoBoot, m_strings.get(kBootText[std::clamp(m_options->boot->selected(), 0, 2)]), false);
    if (unattend.includeInIso) {
        row(Str::IsoUnattend, L"autounattend.xml", true);
    } else if (answersUnused) {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendOff), false, Color::StatusWarning);
    } else {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendNone), false, Color::TextSecondary);
    }
    // Setup's own image: what the build writes into boot.wim (D-038, D-074).
    {
        const std::wstring text = m_options->setupImageText();
        row(Str::IsoSetupImage, text.empty() ? m_strings.get(Str::IsoBootUntouched) : text, false,
            text.empty() ? Color::TextSecondary : Color::TextPrimary);
    }
    // D-134: which drivers go into it (a storage one only there is the warning above).
    row(Str::IsoBootDrivers, m_bootDriversText.empty() ? m_strings.get(Str::IsoBootDriversNone) : m_bootDriversText, false,
        m_bootDriversText.empty() ? Color::TextSecondary : !m_storageOnlyInBoot.empty() ? Color::StatusWarning : Color::TextPrimary);
    {
        const auto [text, ink] = m_options->setupUiSummary(!mediaCannotInstall().empty());
        row(Str::IsoSetupUi, text, false, ink);
    }
    {
        const auto [text, ink] = m_options->mediaUpdateSummary();
        row(Str::IsoMediaUpdateShort, text, false, ink);
    }
    row(Str::IsoSecureBoot, m_strings.get(m_options->secureBoot->checked() ? Str::IsoSecureBoot2023 : Str::IsoSecureBoot2011),
        false, m_options->secureBoot->checked() ? Color::TextPrimary : Color::TextSecondary);
    row(Str::IsoEstIso, m_sourceBytes ? formatBytes(m_sourceBytes, m_language) : std::wstring(L"…"), true);
    row(Str::IsoDuration, m_sourceBytes ? formatDuration(estimateSeconds(), m_language, true) : std::wstring(L"…"),
        true);
}

void IsoPage::syncBootDrivers() {
    const auto& drivers = m_state.bootDrivers();
    m_bootDriversText.clear();
    for (const auto& p : drivers) {
        m_bootDriversText += (m_bootDriversText.empty() ? L"" : L", ") + p.filename().wstring();
    }
    m_storageOnlyInBoot = ImageDriverController::storageOnlyInBoot(m_state);
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
        const float hx = m_usbLabel->bounds().right() + 8; // after the label field, wherever it ends
        canvas.drawText(m_strings.get(Str::IsoUsbLabelHint), {hx, y, std::max(formRight - hx, 0.0f), kRow},
                        TypeStyle::Caption, Color::TextTertiary);
    }
    label(Str::IsoLabel);
    section(Str::IsoBoot);
    label(Str::IsoUsbScheme);
    label(Str::IsoCompression);
    label(Str::IsoSetupImage);
    label(Str::IsoSetupUi);
    m_options->paintLegacyHint(canvas, formRight);
    label(Str::IsoMediaUpdate);
    m_options->paintMediaHint(canvas, formRight);
    label(Str::IsoSecureBoot);
    m_options->paintSecureBootHint(canvas, formRight);
    section(Str::IsoUsbWhenDone);
    label(Str::IsoUsbOpenWhenDone);

    // Summary box.
    const auto& unattend = m_state.unattend();
    const bool answersUnused = !unattend.includeInIso && !noAnswers(unattend.options);
    const RectF box{b.right() - summaryWidth(b.width), b.y + kTop + ui::tokens::size::control + 2 + 12, summaryWidth(b.width),
                    52 + 10 * kSummaryRow};
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
    const bool split = source && source->installImageSize > core::kFat32FileLimit && m_options->repack->selected() == 0;
    row(Str::IsoUsbInstall, m_strings.get(split ? Str::IsoUsbSplit : Str::IsoUsbAsIs), false);
    if (unattend.includeInIso) {
        row(Str::IsoUnattend, L"autounattend.xml", true);
    } else if (answersUnused) {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendOff), false, Color::StatusWarning);
    } else {
        row(Str::IsoUnattend, m_strings.get(Str::IsoUnattendNone), false, Color::TextSecondary);
    }
    {
        const std::wstring text = m_options->setupImageText();
        row(Str::IsoSetupImage, text.empty() ? m_strings.get(Str::IsoBootUntouched) : text, false,
            text.empty() ? Color::TextSecondary : Color::TextPrimary);
    }
    // D-134: which drivers go into it (a storage one only there is the warning above).
    row(Str::IsoBootDrivers, m_bootDriversText.empty() ? m_strings.get(Str::IsoBootDriversNone) : m_bootDriversText, false,
        m_bootDriversText.empty() ? Color::TextSecondary : !m_storageOnlyInBoot.empty() ? Color::StatusWarning : Color::TextPrimary);
    {
        const auto [text, ink] = m_options->setupUiSummary(!mediaCannotInstall().empty());
        row(Str::IsoSetupUi, text, false, ink);
    }
    {
        const auto [text, ink] = m_options->mediaUpdateSummary();
        row(Str::IsoMediaUpdateShort, text, false, ink);
    }
    row(Str::IsoDuration, m_sourceBytes ? formatDuration(estimateSeconds(), m_language, true) : std::wstring(L"…"),
        true);
}

} // namespace wl::app
