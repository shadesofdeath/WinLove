#include "app/pages/IsoPage.h"

#include "app/Format.h"
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
        refresh();
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
    // The row label says it: the boxes stand alone (screen 16).
    m_sha = &add<ui::CheckField>(L"", true);
    m_sha->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoSha));
    m_open = &add<ui::CheckField>(L"", true);
    m_open->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoOpenWhenDone));

    m_usb = &add<ui::EmptyState>(ui::icons::Icon::UsbDrive, strings.get(Str::IsoUsbSoonTitle), strings.get(Str::IsoUsbSoonBody));
    setAccessible(ui::AccessRole::Group, strings.get(Str::IsoTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Iso || change == AppState::Change::Mount || change == AppState::Change::Source ||
            change == AppState::Change::Apply || change == AppState::Change::Operation) {
            refresh();
        }
    });
    computeSize();
    refresh();
}

IsoPage::~IsoPage() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

void IsoPage::computeSize() {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    std::error_code ec;
    if (source->format == core::ImageFormat::Iso) {
        m_sourceBytes = std::filesystem::file_size(source->path, ec);
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
                    invalidate();
                }
            });
        });
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
    return r;
}

bool IsoPage::formValid() const {
    return !m_folder->text().empty() && std::filesystem::path(m_folder->text()).is_absolute() && !m_label->text().empty();
}

double IsoPage::estimateSeconds() const {
    const auto& source = m_state.source();
    if (!source) {
        return 0;
    }
    const double mb = static_cast<double>(m_sourceBytes) / (1024.0 * 1024.0);
    double seconds = mb / 250.0; // write
    if (source->format == core::ImageFormat::Iso) {
        seconds += mb / 200.0;   // extract
    }
    if (m_repack->selected() != 0 && m_controller.canRepack()) {
        seconds += static_cast<double>(source->installImageSize) / (1024.0 * 1024.0) / 25.0;
    }
    if (m_sha->checked()) {
        seconds += mb / 800.0;
    }
    return seconds;
}

void IsoPage::updateBlocker() {
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        m_bar->setVisible(false);
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
        m_bar->set(cancelled ? ui::InfoKind::Warning : ui::InfoKind::Error,
                   m_strings.get(cancelled ? Str::IsoCancelled : Str::IsoFailed),
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
        }
        m_bar->set(ui::InfoKind::Warning, m_strings.get(text), L"");
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
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_fileName, m_folder, m_browse, m_label, m_boot, m_repack,
                                                            m_noPrompt, m_sha, m_open}) {
        w->setVisible(isoTab);
        w->setEnabled(!running && (w != m_repack || m_controller.canRepack()));
    }
    m_usb->setVisible(!isoTab);
    if (isoTab) {
        updateBlocker();
    } else {
        m_bar->setVisible(false);
    }
    layout();
    invalidate();
}

void IsoPage::layout() {
    const RectF b = bounds();
    float y = b.y + kTop;
    m_tabs->setBounds({b.x, y, b.width, ui::tokens::size::control + 2});
    y += ui::tokens::size::control + 2 + 12;
    m_usb->setBounds({b.x, y, b.width, std::max(b.bottom() - y, 0.0f)});
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
    y += kRow + kSection; // DOĞRULAMA
    place(m_sha, 0);
    y += kRow;
    place(m_open, 0);
}

void IsoPage::paint(ui::Canvas& canvas) {
    if (m_tabs->selected() != 0) {
        return;
    }
    const RectF b = bounds();
    const float formRight = b.right() - kSummaryWidth - 24;
    float y = b.y + kTop + ui::tokens::size::control + 2 + 12;
    const auto& run = m_state.isoRun();
    if (run && run->running) {
        static constexpr Str kStages[] = {Str::IsoStageExtract, Str::IsoStageRepack, Str::IsoStageWrite, Str::IsoStageSha};
        const std::wstring text = std::format(L"{} · {}%", m_strings.get(kStages[std::clamp(run->stage, 0, 3)]),
                                              static_cast<int>(run->fraction * 100));
        canvas.drawIcon(ui::icons::Icon::Spinner, {b.x, y + 4}, Color::AccentBase);
        canvas.drawText(text, {b.x + 24, y, formRight - b.x - 24, 20}, TypeStyle::BodyStrong, Color::TextPrimary);
        canvas.progressBar({b.x, y + 24, formRight - b.x, 2}, static_cast<float>(run->fraction));
        y += kInfoBar + 12;
    } else if (m_bar->visible()) {
        y += kInfoBar + 12;
    }
    // Section headers + field labels.
    auto section = [&](Str title) {
        canvas.drawText(m_strings.get(title), {b.x, y + 8, 300, 20}, TypeStyle::Section, Color::TextSecondary);
        canvas.hairlineH(b.x, y + kSection - 6, formRight - b.x, Color::LineSubtle);
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
    section(Str::IsoVerify);
    label(Str::IsoSha);
    label(Str::IsoOpenWhenDone);

    // Summary box.
    const RectF box{b.right() - kSummaryWidth, b.y + kTop + ui::tokens::size::control + 2 + 12, kSummaryWidth, 136};
    canvas.fillRoundRect(box, ui::tokens::radius::r3, Color::BgPanel);
    canvas.strokeRoundRect(box, ui::tokens::radius::r3, Color::LineSubtle);
    float sy = box.y + 12;
    canvas.drawText(m_strings.get(Str::IsoSummary), {box.x + 16, sy, box.width - 32, 20}, TypeStyle::Section,
                    Color::TextSecondary);
    sy += 28;
    auto row = [&](Str key, const std::wstring& value, bool mono) {
        canvas.drawText(m_strings.get(key), {box.x + 16, sy, 96, kSummaryRow}, TypeStyle::Caption, Color::TextSecondary);
        canvas.drawText(value, {box.x + 112, sy, box.width - 128, kSummaryRow}, mono ? TypeStyle::Mono : TypeStyle::Caption,
                        Color::TextPrimary);
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
    row(Str::IsoEstIso, m_sourceBytes ? formatBytes(m_sourceBytes, m_language) : std::wstring(L"…"), true);
    row(Str::IsoDuration, m_sourceBytes ? formatDuration(estimateSeconds(), m_language, true) : std::wstring(L"…"),
        true);
}

} // namespace wl::app
