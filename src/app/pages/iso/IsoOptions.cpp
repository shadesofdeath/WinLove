#include "app/pages/iso/IsoOptions.h"

#include "app/controllers/IsoController.h"
#include "base/Log.h"
#include "core/image/SetupMedia.h"
#include "core/image/UpdatePackage.h"
#include "core/iso/SecureBoot2023.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

IsoOptionControls::IsoOptionControls(ui::Widget& host, AppState& state, const Localization& strings, Hooks hooks)
    : m_state(state), m_strings(strings), m_hooks(std::move(hooks)) {
    // The ISO page's order (its keyboard order): label, boot mode, compression, the boxes.
    label = &host.add<ui::SearchBox>(L"");
    label->setPlain(true);
    label->setWidth(200);
    label->setText(IsoController::chosenLabel(m_state));
    label->onChange = [this](const std::wstring& text) { store([&](AppState::IsoChoices& c) { c.label = text; }); };

    boot = &host.add<ui::RadioGroup>(std::vector<std::wstring>{strings.get(Str::IsoBootBoth), strings.get(Str::IsoBootUefi),
                                                                 strings.get(Str::IsoBootBios)},
                                     static_cast<int>(m_state.isoChoices().boot));
    boot->onChange = [this](int index) {
        store([&](AppState::IsoChoices& c) { c.boot = static_cast<core::BootMode>(std::clamp(index, 0, 2)); });
    };
    repack = &host.add<ui::Dropdown>(L"",
                                     std::vector<std::wstring>{strings.get(Str::IsoRepackAsIs), strings.get(Str::IsoRepackLzx),
                                                               strings.get(Str::IsoRepackXpress), strings.get(Str::IsoRepackEsd)},
                                     0);
    repack->onChange = [this](int index) {
        store([&](AppState::IsoChoices& c) { c.repack = static_cast<std::uint8_t>(std::clamp(index, 0, 3)); });
    };
    noPrompt = &host.add<ui::CheckField>(strings.get(Str::IsoNoPrompt), false);
    noPrompt->onChange = [this](bool on) { store([&](AppState::IsoChoices& c) { c.noPrompt = on; }); };
    bootBypass = &host.add<ui::CheckField>(strings.get(Str::IsoBootBypass), true);
    bootBypass->onChange = [this](bool on) { store([&](AppState::IsoChoices& c) { c.bootBypass = on; }); };
    legacySetup = &host.add<ui::CheckField>(strings.get(Str::IsoLegacySetup), false);
    legacySetup->onChange = [this](bool on) { store([&](AppState::IsoChoices& c) { c.legacySetup = on; }); };
    secureBoot = &host.add<ui::CheckField>(strings.get(Str::IsoSecureBootBox), false);
    secureBoot->onChange = [this](bool on) { store([&](AppState::IsoChoices& c) { c.secureBoot2023 = on; }); };
    m_dbTrusts2023 = core::firmwareTrustsCa2023();
    mediaUpdate = &host.add<ui::CheckField>(strings.get(Str::IsoMediaUpdateBox), true);
    mediaUpdate->onChange = [this](bool on) {
        auto media = m_state.mediaUpdate();
        media.enabled = on;
        m_state.setMediaUpdate(std::move(media));
        if (m_hooks.changed) {
            m_hooks.changed();
        }
    };
    sha = &host.add<ui::CheckField>(L"", true);
    sha->setAccessible(ui::AccessRole::CheckBox, strings.get(Str::IsoSha));
    sha->onChange = [this](bool on) { store([&](AppState::IsoChoices& c) { c.sha256 = on; }); };
}

IsoOptionControls::~IsoOptionControls() {
    *m_alive = false;
}

void IsoOptionControls::store(const std::function<void(AppState::IsoChoices&)>& change) {
    AppState::IsoChoices choices = m_state.isoChoices();
    change(choices);
    m_state.setIsoChoices(std::move(choices)); // Change::Iso: every place showing them syncs
    if (m_hooks.changed) {
        m_hooks.changed();
    }
}

bool IsoOptionControls::previousMedia() const {
    const auto& source = m_state.source();
    return source && core::mediaOpensPreviousSetup(*source) && !m_ignoreMediaSetup;
}

void IsoOptionControls::sync(bool running) {
    syncMediaSource(); // Change::Iso brings a new media update here
    const auto& c = m_state.isoChoices();
    if (const std::wstring text = IsoController::chosenLabel(m_state); label->text() != text) {
        label->setText(text);
    }
    label->setEnabled(!running);
    if (boot->selected() != static_cast<int>(c.boot)) {
        boot->setSelected(static_cast<int>(c.boot));
    }
    boot->setEnabled(!running);
    const bool canRepack = IsoController::canRepack(m_state);
    const int compression = canRepack ? std::min<int>(c.repack, 3) : 0;
    if (repack->selected() != compression) {
        repack->setSelected(compression);
    }
    repack->setEnabled(!running && canRepack);
    repack->setTooltip(canRepack ? std::wstring() : m_strings.get(Str::IsoRepackOnlyWork));
    noPrompt->setChecked(c.noPrompt);
    noPrompt->setEnabled(!running);
    // Nothing to write into Setup's image while the answers switch no check off. (The drivers go in
    // whatever the box says, D-134.)
    const bool bypasses = !IsoController::bootPatch(m_state).labConfigValues().empty();
    bootBypass->setChecked(c.bootBypass);
    bootBypass->setEnabled(!running && bypasses);
    bootBypass->setTooltip(bypasses ? std::wstring() : m_strings.get(Str::IsoBootBypassNone));
    // D-074: on by itself for a 24H2+ edition without WinRE until the user decides (older media, D-077,
    // installs it with its own previous Setup).
    const auto& source = m_state.source();
    const bool autoOn = !m_noWinre.empty() && !(source && core::mediaOpensPreviousSetup(*source));
    const bool previous = previousMedia();
    legacySetup->setChecked(!previous && c.legacySetup.value_or(autoOn));
    legacySetup->setEnabled(!running && !previous);
    const auto& media = m_state.mediaUpdate();
    mediaUpdate->setChecked(media.any() && media.enabled);
    mediaUpdate->setEnabled(!running && media.any());
    mediaUpdate->setTooltip(media.any() ? std::wstring() : m_strings.get(Str::IsoMediaUpdateNone));
    secureBoot->setChecked(c.secureBoot2023);
    secureBoot->setEnabled(!running);
    sha->setChecked(c.sha256);
    sha->setEnabled(!running);
}

void IsoOptionControls::checkWinre() {
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
        [this, alive, missing, post = m_hooks.postToUi](Result<bool>) {
            if (!post) {
                return;
            }
            post([this, alive, missing] {
                if (const auto a = alive.lock(); a && *a) {
                    setEditionsWithoutWinre(std::move(*missing));
                    if (m_hooks.changed) {
                        m_hooks.changed();
                    }
                }
            });
        });
}

void IsoOptionControls::setEditionsWithoutWinre(std::vector<int> editions) {
    m_noWinre = std::move(editions);
}

void IsoOptionControls::syncMediaSource() {
    const auto& media = m_state.mediaUpdate();
    if (m_mediaSourceKnown && media.lcu.native() == m_mediaSourceLcu && media.setupDu.native() == m_mediaSourceSetup) {
        return;
    }
    m_mediaSourceKnown = true;
    m_mediaSourceLcu = media.lcu.native();
    m_mediaSourceSetup = media.setupDu.native();
    m_mediaSource = mediaUpdateSource();
}

// "KB5129195 · Setup KB5127216" — what the media update takes; empty: nothing downloaded.
std::wstring IsoOptionControls::mediaUpdateSource() const {
    const auto& media = m_state.mediaUpdate();
    std::wstring text;
    if (!media.lcu.empty()) {
        const auto kb = core::analyzeUpdate(media.lcu).kb;
        text = kb.empty() ? media.lcu.filename().wstring() : kb;
    }
    if (!media.setupDu.empty()) {
        const auto kb = core::analyzeUpdate(media.setupDu).kb;
        text += (text.empty() ? L"" : L" · ") + std::wstring(L"Setup ") + (kb.empty() ? media.setupDu.filename().wstring() : kb);
    }
    return text;
}

// What the build writes into boot.wim; empty: nothing.
std::wstring IsoOptionControls::setupImageText() const {
    const std::size_t checks = bootBypass->checked() ? IsoController::bootPatch(m_state).labConfigValues().size() : 0;
    const std::size_t drivers = m_state.bootDrivers().size();
    if (checks > 0 && drivers > 0) {
        return m_strings.format(Str::IsoBootBypassN, {{L"n", std::to_wstring(checks)}}) + L" · " +
               m_strings.format(Str::IsoBootDriversN, {{L"n", std::to_wstring(drivers)}});
    }
    if (checks > 0) {
        return m_strings.format(Str::IsoBootBypassN, {{L"n", std::to_wstring(checks)}});
    }
    if (drivers > 0) {
        return m_strings.format(Str::IsoBootDriversN, {{L"n", std::to_wstring(drivers)}});
    }
    // The previous Setup has a row of its own ("Kurulum ekranı").
    return legacySetup->checked() ? m_strings.get(Str::IsoBootPatched) : std::wstring();
}

std::pair<std::wstring, Color> IsoOptionControls::mediaUpdateSummary() const {
    const auto& media = m_state.mediaUpdate();
    if (!media.any() || !mediaUpdate->checked()) {
        return {m_strings.get(Str::IsoBootUntouched), Color::TextSecondary};
    }
    const Str what = media.lcu.empty()       ? Str::IsoMediaUpdateFilesOnly
                     : media.setupDu.empty() ? Str::IsoMediaUpdateBootOnly
                                             : Str::IsoMediaUpdateSummary;
    return {m_strings.get(what), Color::TextPrimary};
}

std::pair<std::wstring, Color> IsoOptionControls::setupUiSummary(bool cannotInstall) const {
    const auto& source = m_state.source();
    if (cannotInstall) {
        // D-077: neither the new nor the previous Setup of 24H2+ media installs Windows 10.
        return {m_strings.get(Str::IsoAioSummary), Color::StatusWarning};
    }
    if (source && core::mediaOpensPreviousSetup(*source)) {
        return {m_strings.get(Str::IsoLegacySetupMedia), Color::TextSecondary};
    }
    if (legacySetup->checked()) {
        return {m_strings.get(Str::IsoLegacySetupPrevious), Color::TextPrimary};
    }
    if (!m_noWinre.empty()) {
        return {m_strings.get(Str::IsoLegacySetupNewFails), Color::StatusWarning};
    }
    return {m_strings.get(Str::IsoLegacySetupNew), Color::TextSecondary};
}

// Beside the box: what it does, or why it is on by itself.
void IsoOptionControls::paintLegacyHint(ui::Canvas& canvas, float formRight) const {
    const RectF box = legacySetup->bounds();
    const float x = box.right() + 12;
    if (x >= formRight) {
        return;
    }
    if (previousMedia()) {
        canvas.drawText(m_strings.get(Str::IsoLegacySetupClassic), {x, box.y, formRight - x, box.height}, TypeStyle::Caption,
                        Color::TextTertiary);
        return;
    }
    const bool noWinre = !m_noWinre.empty();
    canvas.drawText(m_strings.get(noWinre ? Str::IsoLegacySetupNoWinre : Str::IsoLegacySetupHint),
                    {x, box.y, formRight - x, box.height}, TypeStyle::Caption,
                    noWinre && !legacySetup->checked() ? Color::StatusWarning
                                                       : (noWinre ? Color::TextSecondary : Color::TextTertiary));
}

// Beside the box: which updates it takes, or where they come from.
void IsoOptionControls::paintMediaHint(ui::Canvas& canvas, float formRight) {
    const RectF box = mediaUpdate->bounds();
    const float x = box.right() + 12;
    if (x >= formRight) {
        return;
    }
    syncMediaSource(); // two string compares; sync() has already asked the files
    const std::wstring& source = m_mediaSource;
    canvas.drawText(source.empty() ? m_strings.get(Str::IsoMediaUpdateNone) : source, {x, box.y, formRight - x, box.height},
                    source.empty() ? TypeStyle::Caption : TypeStyle::Mono, Color::TextTertiary);
}

// Beside the box: whether media signed only by the 2023 CA starts on this PC (its UEFI db).
void IsoOptionControls::paintSecureBootHint(ui::Canvas& canvas, float formRight) const {
    const RectF box = secureBoot->bounds();
    const float x = box.right() + 12;
    if (x >= formRight) {
        return;
    }
    Str text = Str::IsoSecureBootUnknown;
    Color ink = Color::TextTertiary;
    if (m_dbTrusts2023 && *m_dbTrusts2023) {
        text = Str::IsoSecureBootTrusted;
    } else if (m_dbTrusts2023) {
        text = Str::IsoSecureBootUntrusted;
        ink = secureBoot->checked() ? Color::StatusWarning : Color::TextTertiary;
    }
    canvas.drawText(m_strings.get(text), {x, box.y, formRight - x, box.height}, TypeStyle::Caption, ink);
}

std::wstring isoChoicesSummary(const AppState& state, const Localization& strings) {
    const auto& c = state.isoChoices();
    std::vector<std::wstring> parts;
    static constexpr Str kBoot[] = {Str::IsoBootBoth, Str::IsoBootUefi, Str::IsoBootBios};
    parts.push_back(strings.get(kBoot[std::clamp(static_cast<int>(c.boot), 0, 2)]));
    parts.push_back(strings.format(Str::IsoChoicesLabel, {{L"label", IsoController::chosenLabel(state)}}));
    static constexpr Str kRepack[] = {Str::IsoChoicesLzx, Str::IsoChoicesXpress, Str::IsoChoicesEsd};
    if (c.repack > 0 && IsoController::canRepack(state)) {
        parts.push_back(strings.get(kRepack[std::min<int>(c.repack, 3) - 1]));
    }
    if (c.noPrompt) {
        parts.push_back(strings.get(Str::IsoChoicesNoPrompt));
    }
    if (!c.bootBypass && !IsoController::bootPatch(state).labConfigValues().empty()) {
        parts.push_back(strings.get(Str::IsoChoicesNoBypass));
    }
    const auto& source = state.source();
    if (c.legacySetup && *c.legacySetup && !(source && core::mediaOpensPreviousSetup(*source))) {
        parts.push_back(strings.get(Str::IsoChoicesPrevious));
    }
    if (state.mediaUpdate().enabled && state.mediaUpdate().any()) {
        parts.push_back(strings.get(Str::IsoChoicesMedia));
    }
    if (c.secureBoot2023) {
        parts.push_back(strings.get(Str::IsoChoicesSecureBoot));
    }
    if (c.sha256) {
        parts.push_back(strings.get(Str::IsoChoicesSha));
    }
    std::wstring text;
    for (const auto& part : parts) {
        text += (text.empty() ? L"" : L" · ") + part;
    }
    return text;
}

namespace {

constexpr float kDialogWidth = 760.0f;
constexpr float kLabelWidth = 190.0f; // the ISO page's label column
constexpr float kRow = ui::tokens::size::control + ui::tokens::spacing::s2;
constexpr int kRows = 9;

// The dialog's content: the controls in the ISO page's rows (label · control · hint), without its
// sections — the file and the folder are the Apply row's.
class IsoOptionsPanel : public ui::Widget {
public:
    IsoOptionsPanel(AppState& state, const Localization& strings, std::function<void(std::function<void()>)> postToUi)
        : m_state(state), m_strings(strings),
          m_controls(*this, state, strings, IsoOptionControls::Hooks{std::move(postToUi), [this] { refresh(); }}) {
        m_subscription = m_state.subscribe([this](AppState::Change change) {
            if (change == AppState::Change::Iso || change == AppState::Change::Unattend || change == AppState::Change::Source ||
                change == AppState::Change::Drivers) {
                refresh();
            }
        });
        m_controls.checkWinre();
        refresh();
    }
    ~IsoOptionsPanel() override { m_state.unsubscribe(m_subscription); }

    [[nodiscard]] ui::Widget* firstControl() const { return m_controls.label; }

    void layout() override {
        const RectF b = bounds();
        const float fieldX = b.x + kLabelWidth;
        float y = b.y;
        auto place = [&](ui::Widget* w, float width) {
            const ui::SizeF size = w->measure({});
            w->setBounds({fieldX, y + (kRow - ui::tokens::size::control) / 2,
                          std::max(std::min(width > 0 ? width : size.width, b.right() - fieldX), 0.0f), ui::tokens::size::control});
            y += kRow;
        };
        place(m_controls.label, 240);
        place(m_controls.boot, 0);
        place(m_controls.repack, 240);
        place(m_controls.noPrompt, 0);
        place(m_controls.bootBypass, 0);
        place(m_controls.legacySetup, 0);
        place(m_controls.mediaUpdate, 0);
        place(m_controls.secureBoot, 0);
        place(m_controls.sha, 0);
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        float y = b.y;
        for (const Str key : {Str::IsoLabel, Str::IsoBootMode, Str::IsoCompression, Str::IsoPrompt, Str::IsoSetupImage, Str::IsoSetupUi,
                              Str::IsoMediaUpdate, Str::IsoSecureBoot, Str::IsoSha}) {
            canvas.drawText(m_strings.get(key), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
            y += kRow;
        }
        m_controls.paintLegacyHint(canvas, b.right());
        m_controls.paintMediaHint(canvas, b.right());
        m_controls.paintSecureBootHint(canvas, b.right());
    }

private:
    void refresh() {
        const auto& run = m_state.isoRun();
        m_controls.sync(run && run->running);
        layout();
        invalidate();
    }

    AppState& m_state;
    const Localization& m_strings;
    IsoOptionControls m_controls;
    std::size_t m_subscription = 0;
};

} // namespace

IsoOptionsDialog makeIsoOptionsDialog(AppState& state, const Localization& strings,
                                      std::function<void(std::function<void()>)> postToUi, std::function<void()> close) {
    auto dialog = std::make_unique<ui::Dialog>(strings.get(Str::IsoChoicesTitle), strings.get(Str::IsoChoicesBody),
                                               ui::icons::Icon::TweaksSliders, Color::TextSecondary, kDialogWidth);
    ui::Dialog* raw = dialog.get();
    auto& panel = raw->setContent<IsoOptionsPanel>(kRow * kRows, state, strings, std::move(postToUi));
    raw->onCancel = close;
    raw->addButton(ui::ButtonKind::Primary, strings.get(Str::CommonOk), close, /*primary=*/true);
    return {std::move(dialog), panel.firstControl()};
}

} // namespace wl::app
