#include "app/pages/SettingsPage.h"

#include "app/SystemInfo.h"
#include "core/image/dism/HostDism.h"
#include "core/image/wim/WimGapi.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/ColorPicker.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/SearchBox.h"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;

namespace {

constexpr float kTop = 7.0f;          // page top → first section band (screen 19)
constexpr float kLabelWidth = 264.0f; // D-136: as the Ayarlar form's
constexpr float kPathWidth = 360.0f;
constexpr float kLanguageWidth = 240.0f;
constexpr float kBrowse = 28.0f;

} // namespace

// A path box with a browse button. The path is committed with Enter or by picking a folder,
// not on every keystroke (a half-typed path must not become the work folder).
class PathField : public ui::Widget {
public:
    PathField(std::wstring placeholder, std::wstring browseTooltip) {
        m_box = &add<ui::SearchBox>(std::move(placeholder));
        m_box->setPlain(true);
        m_box->onSubmit = [this] {
            if (onCommit) {
                onCommit();
            }
        };
        m_box->onChange = [this](const std::wstring&) {
            if (onEdit) {
                onEdit();
            }
        };
        m_browse = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::OpenFolder);
        m_browse->setTooltip(std::move(browseTooltip));
        m_browse->onInvoke = [this] {
            if (onBrowse) {
                onBrowse();
            }
        };
    }
    std::function<void()> onCommit; // Enter
    std::function<void()> onEdit;   // typing
    std::function<void()> onBrowse;

    [[nodiscard]] const std::wstring& text() const { return m_box->text(); }
    void setText(std::wstring text) { m_box->setText(std::move(text)); }
    [[nodiscard]] bool editing() const { return m_box->focused(); }

    [[nodiscard]] ui::SizeF measure(ui::SizeF /*available*/) override {
        return {kPathWidth + 4 + kBrowse, ui::tokens::size::control};
    }
    void layout() override {
        const RectF b = bounds();
        m_box->setBounds({b.x, b.y, std::max(b.width - kBrowse - 4, 0.0f), b.height});
        m_browse->setBounds({b.right() - kBrowse, b.y, kBrowse, b.height});
    }

private:
    ui::SearchBox* m_box = nullptr;
    ui::Button* m_browse = nullptr;
};

// Screen 19: the accent swatches — round, 20 across, 10 apart, the chosen one ringed with text.primary
// 3 out; its name after them. D-143: a sixth swatch is "Özel renk" — the colour picker in a panel under
// the row (any colour; a custom colour shows in that swatch). Click or ←/→ picks a preset; Enter / Space
// open the picker. What the row shows is the accent as drawn (a colour being dragged in the picker too).
class AccentSwatches : public ui::Widget {
public:
    static constexpr float kW = 20.0f; // D-107: round swatches
    static constexpr float kH = 20.0f;
    static constexpr float kGap = 10.0f;
    static constexpr int kCustom = ui::kAccentCount; // the picker's swatch, after the presets

    struct Texts {
        std::vector<std::wstring> names; // the presets'
        std::wstring custom;             // "Özel renk"
        std::wstring pick;               // its tooltip, "Renk seç…"
        ui::ColorPicker::Texts picker;
    };
    explicit AccentSwatches(Texts texts) : m_texts(std::move(texts)) {
        setFocusable(true);
        setAccessible(ui::AccessRole::Group, L"");
    }
    ~AccentSwatches() override {
        if (m_popup) { // its callbacks point here
            m_popup->detach();
            m_popup->close();
        }
        if (m_dragging) {
            ui::setAccentColor(m_saved); // a preview must not outlive the page
        }
    }
    // Saved (0xRRGGBB) — committed with Enter / letting go / a preset; a preview is ui::setAccentColor only.
    std::function<void(std::uint32_t)> onChange;
    std::function<void(std::uint32_t)> onPreview;

    void setSaved(std::uint32_t rgb) {
        m_saved = rgb;
        invalidate();
    }

    [[nodiscard]] ui::SizeF measure(ui::SizeF) override {
        return {(kCustom + 1) * (kW + kGap) + 200.0f, ui::tokens::size::control};
    }
    void paint(ui::Canvas& canvas) override {
        const ui::RectF b = bounds();
        const std::uint32_t shown = ui::accentColor();
        const auto preset = ui::presetOf(shown);
        for (int i = 0; i <= kCustom; ++i) {
            const ui::RectF swatch = swatchRect(i);
            if (i < kCustom) {
                canvas.fillRoundRect(swatch, kH / 2, ui::accentSwatch(static_cast<ui::Accent>(i)));
            } else if (!preset) {
                canvas.fillRoundRect(swatch, kH / 2, ui::rgbColor(shown));
            } else {
                // No colour of its own yet: the hues it opens on.
                std::array<ui::Rgba, 7> hues{};
                for (std::size_t h = 0; h < hues.size(); ++h) {
                    hues[h] = ui::rgbColor(ui::fromHsv({60.0f * static_cast<float>(h), 0.62f, 0.86f}));
                }
                canvas.fillLinearGradient(swatch, kH / 2, hues, /*vertical=*/false);
            }
            const bool chosen = i < kCustom ? preset && static_cast<int>(*preset) == i : !preset;
            if (chosen || (i == kCustom && m_popup)) {
                canvas.strokeRoundRect(swatch.inset(-3, -3), kH / 2 + 3, ui::tokens::Color::TextPrimary, 2.0f * canvas.scale());
            } else if (m_hover == i) {
                canvas.strokeRoundRect(swatch.inset(-3, -3), kH / 2 + 3, ui::tokens::Color::LineStrong, 2.0f * canvas.scale());
            }
        }
        const float x = swatchRect(kCustom).right() + kGap + 4;
        const std::wstring name = preset ? m_texts.names[static_cast<std::size_t>(*preset)]
                                         : m_texts.custom + L" · " + ui::hexColor(shown);
        canvas.drawText(name, {x, b.y, std::max(b.right() - x, 0.0f), b.height}, ui::tokens::TypeStyle::Body,
                        ui::tokens::Color::TextSecondary);
    }
    [[nodiscard]] ui::RectF focusRect() const override {
        const ui::RectF b = bounds();
        return {b.x - 2, b.y + (b.height - kH) / 2 - 4, (kCustom + 1) * (kW + kGap), kH + 8};
    }
    void onPointerMove(ui::PointF p) override {
        const int hover = at(p);
        if (hover != m_hover) {
            m_hover = hover;
            setTooltip(hover < 0 ? std::wstring() : hover == kCustom ? m_texts.pick : m_texts.names[static_cast<std::size_t>(hover)]);
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
        }
        invalidate();
    }
    void onPointerDown(ui::PointF p) override { m_down = at(p); }
    void onClick() override {
        if (m_down == kCustom) {
            openPicker();
        } else {
            pick(m_down);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        if (key.virtualKey == VK_LEFT || key.virtualKey == VK_RIGHT) {
            const auto preset = ui::presetOf(m_saved);
            const int current = preset ? static_cast<int>(*preset) : kCustom;
            const int next = current + (key.virtualKey == VK_LEFT ? -1 : 1);
            pick(std::clamp(next, 0, ui::kAccentCount - 1));
            return true;
        }
        if (key.virtualKey == VK_RETURN || key.virtualKey == VK_SPACE) {
            openPicker();
            return true;
        }
        return false;
    }

    void openPicker() {
        if (m_popup || !host()) {
            return;
        }
        const ui::RectF row = bounds();
        auto popup = std::make_unique<ui::ColorPickerPopup>(ui::RectF{row.x, row.y, (kCustom + 1) * (kW + kGap), row.height},
                                                            m_texts.picker, ui::accentColor());
        m_opened = m_saved;
        popup->onPreview = [this](std::uint32_t rgb) {
            m_dragging = true;
            if (onPreview) {
                onPreview(rgb);
            }
        };
        popup->onCommit = [this](std::uint32_t rgb) { commit(rgb); };
        popup->onCancel = [this] { commit(m_opened); }; // Esc: the colour it opened with, saved again
        popup->onClosed = [this] {
            m_popup = nullptr;
            if (m_dragging) {
                commit(ui::accentColor()); // closed mid-preview (typed, not entered): keep what shows
            }
            invalidate();
        };
        m_popup = popup.get();
        host()->pushModal(std::move(popup), m_popup, /*scrim=*/false, /*owner=*/this);
        invalidate();
    }

private:
    [[nodiscard]] ui::RectF swatchRect(int i) const {
        const ui::RectF b = bounds();
        return {b.x + 2 + static_cast<float>(i) * (kW + kGap), b.y + (b.height - kH) / 2, kW, kH};
    }
    [[nodiscard]] int at(ui::PointF p) const {
        const ui::RectF b = bounds();
        const float x = p.x - b.x - 2;
        const int i = static_cast<int>(x / (kW + kGap));
        return x >= 0 && i <= kCustom && x - static_cast<float>(i) * (kW + kGap) <= kW ? i : -1;
    }
    void pick(int index) {
        if (index < 0 || index >= ui::kAccentCount) {
            return;
        }
        const std::uint32_t rgb = ui::presetColor(static_cast<ui::Accent>(index));
        if (rgb != m_saved) {
            commit(rgb);
        }
    }
    void commit(std::uint32_t rgb) {
        m_dragging = false;
        if (onChange) {
            onChange(rgb);
        }
        invalidate();
    }

    Texts m_texts;
    std::uint32_t m_saved = ui::kDefaultAccent;
    std::uint32_t m_opened = ui::kDefaultAccent; // what the picker opened with (Esc)
    ui::ColorPickerPopup* m_popup = nullptr;     // while open
    bool m_dragging = false;                     // a preview not committed yet
    int m_hover = -1;
    int m_down = -1;
};

SettingsPage::SettingsPage(AppState& state, const Localization& strings, Intents intents)
    : m_state(state), m_strings(strings), m_intents(std::move(intents)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_form = &add<ui::FormView>(kLabelWidth);

    m_form->addSection(s(Str::SettingsAppearance));
    m_theme = &m_form->addRow<ui::RadioGroup>(
        s(Str::SettingsTheme), std::wstring(), 0.0f,
        std::vector<std::wstring>{s(Str::SettingsThemeDark), s(Str::SettingsThemeLight), s(Str::SettingsThemeBlack),
                                  s(Str::SettingsThemeSystem)},
        0);
    m_theme->setAccessible(ui::AccessRole::Group, s(Str::SettingsTheme));
    m_theme->onChange = [this](int index) {
        edit([index](AppSettings& a) { a.theme = static_cast<ThemeChoice>(std::clamp(index, 0, 3)); });
    };
    AccentSwatches::Texts accentTexts{
        {s(Str::SettingsAccentBlue), s(Str::SettingsAccentCopper), s(Str::SettingsAccentSea), s(Str::SettingsAccentPomegranate),
         s(Str::SettingsAccentOlive)},
        s(Str::SettingsAccentCustom),
        s(Str::SettingsAccentPick),
        ui::ColorPicker::Texts{s(Str::SettingsAccentField), s(Str::SettingsAccentHue), s(Str::SettingsAccentHex),
                               s(Str::SettingsAccentSample), s(Str::SettingsAccentDarkened), s(Str::SettingsAccentLightened)}};
    m_accent = &m_form->addRow<AccentSwatches>(s(Str::SettingsAccent), std::wstring(), 0.0f, std::move(accentTexts));
    m_accent->setAccessible(ui::AccessRole::Group, s(Str::SettingsAccent));
    // D-143: a colour picked is drawn at once (a render applies no settings) and saved; one being dragged in
    // the picker is only drawn — the whole window, every accent brush is resolved at paint time.
    m_accent->onChange = [this](std::uint32_t rgb) {
        ui::setAccentColor(rgb);
        edit([rgb](AppSettings& a) { a.accent = rgb; });
    };
    m_accent->onPreview = [this](std::uint32_t rgb) {
        ui::setAccentColor(rgb);
        invalidate();
    };
    m_motion = &m_form->addRow<ui::Toggle>(s(Str::SettingsReduceMotion), std::wstring(), ui::tokens::size::toggleW,
                                           std::wstring(), false);
    m_motion->setAccessible(ui::AccessRole::CheckBox, s(Str::SettingsReduceMotion));
    m_motion->onChange = [this](bool on) { edit([on](AppSettings& a) { a.reduceMotion = on; }); };
    // D-092: the type family (rebuilds the interface, as a language change does) and the density.
    m_font = &m_form->addRow<ui::Dropdown>(s(Str::SettingsFont), std::wstring(), kLanguageWidth, std::wstring(),
                                           std::vector<std::wstring>{L"Inter", L"Geist", L"Segoe UI Variable"}, 0);
    m_font->setAccessible(ui::AccessRole::Group, s(Str::SettingsFont));
    m_font->onChange = [this](int index) {
        edit([index](AppSettings& a) { a.font = static_cast<UiFont>(std::clamp(index, 0, 2)); });
    };
    m_density = &m_form->addRow<ui::Dropdown>(
        s(Str::SettingsDensity), std::wstring(), kLanguageWidth, std::wstring(),
        std::vector<std::wstring>{s(Str::SettingsDensityCompact), s(Str::SettingsDensityComfortable), s(Str::SettingsDensityLarge)},
        0);
    m_density->setAccessible(ui::AccessRole::Group, s(Str::SettingsDensity));
    m_density->onChange = [this](int index) {
        edit([index](AppSettings& a) { a.density = static_cast<Density>(std::clamp(index, 0, 2)); });
    };

    m_form->addSection(s(Str::SettingsLanguage));
    m_language = &m_form->addRow<ui::Dropdown>(
        s(Str::SettingsUiLanguage), std::wstring(), kLanguageWidth, std::wstring(),
        std::vector<std::wstring>{s(Str::SettingsLangTr), s(Str::SettingsLangEn)}, 0);
    m_language->setAccessible(ui::AccessRole::Group, s(Str::SettingsUiLanguage));
    m_language->onChange = [this](int index) {
        // The App rebuilds the whole UI in the new language right after this (posted).
        edit([index](AppSettings& a) { a.language = index == 1 ? Language::English : Language::Turkish; });
    };

    m_form->addSection(s(Str::SettingsEnvironment));
    auto folder = [&](Str label, std::wstring placeholder, std::filesystem::path AppSettings::*member,
                      bool allowEmpty) -> PathField* {
        auto& field = m_form->addRow<PathField>(s(label), std::wstring(), 0.0f, std::move(placeholder), s(Str::SettingsBrowse));
        field.onCommit = [this, &field, member, allowEmpty] { commitFolder(field, member, allowEmpty); };
        field.onEdit = [this] { sync(); };
        field.onBrowse = [this, &field, member, allowEmpty] {
            if (foldersLocked() || !m_intents.pickFolder) {
                return;
            }
            if (const auto picked = m_intents.pickFolder()) {
                field.setText(picked->wstring());
                commitFolder(field, member, allowEmpty);
            }
        };
        return &field;
    };
    m_work = folder(Str::SettingsWorkDir, std::wstring(), &AppSettings::workRoot, /*allowEmpty=*/false);
    m_mount = folder(Str::SettingsMountDir, s(Str::SettingsMountDefault), &AppSettings::mountFolder, /*allowEmpty=*/true);
    const std::wstring dismVersion = dismLibraryVersion();
    m_form->addRow<ui::Label>(s(Str::SettingsDismPath), std::wstring(), 0.0f,
                              (dismVersion.empty() ? dismLibraryPath() : dismLibraryPath() + L" \u00b7 " + dismVersion) +
                                  (dismFromAdk() ? L" \u00b7 " + s(Str::SettingsDismAdk) : std::wstring()),
                              ui::tokens::TypeStyle::Mono,
                              Color::TextSecondary);

    // D-123: the reference tool's Tool settings › DISM / Performance / Presets, the way WinLove can do them.
    m_form->addSection(s(Str::SettingsEngine));
    std::vector<std::wstring> engines{s(Str::SettingsAutomatic)};
    m_dismChoices = {DismEngineChoice::Auto};
    if (const std::wstring host = core::hostDismVersion(); !host.empty()) {
        engines.push_back(m_strings.format(Str::SettingsDismHost, {{L"v", L"v" + host}}));
        m_dismChoices.push_back(DismEngineChoice::Host);
    }
    if (const std::wstring adk = core::adkDismVersion(); !adk.empty()) {
        engines.push_back(m_strings.format(Str::SettingsDismAdkItem, {{L"v", L"v" + adk}}));
        m_dismChoices.push_back(DismEngineChoice::Adk);
    }
    m_dismEngine = &m_form->addRow<ui::Dropdown>(s(Str::SettingsDismEngine), std::wstring(), kLanguageWidth + 60, std::wstring(),
                                                 std::move(engines), 0);
    m_dismEngine->setAccessible(ui::AccessRole::Group, s(Str::SettingsDismEngine));
    m_dismEngine->onChange = [this](int index) {
        if (index >= 0 && index < static_cast<int>(m_dismChoices.size())) {
            const DismEngineChoice choice = m_dismChoices[static_cast<std::size_t>(index)];
            edit([choice](AppSettings& a) { a.dismEngine = choice; });
        }
    };
    std::vector<std::wstring> threads{s(Str::SettingsAutomatic)};
    m_processors = std::clamp(static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)), 1, 64);
    for (int n = 1; n <= m_processors; ++n) {
        threads.push_back(std::to_wstring(n));
    }
    m_esdThreads = &m_form->addRow<ui::Dropdown>(s(Str::SettingsEsdThreads), std::wstring(), kLanguageWidth, std::wstring(),
                                                 std::move(threads), 0);
    m_esdThreads->setAccessible(ui::AccessRole::Group, s(Str::SettingsEsdThreads));
    m_esdThreads->onChange = [this](int index) {
        const int n = std::max(index, 0);
        core::setCompressionThreads(n); // the next export already uses it
        edit([n](AppSettings& a) { a.esdThreads = n; });
    };

    m_form->addSection(s(Str::SettingsPresets));
    m_presetInIso = &m_form->addRow<ui::Toggle>(s(Str::SettingsPresetInIso), std::wstring(), ui::tokens::size::toggleW,
                                                std::wstring(), true);
    m_presetInIso->setAccessible(ui::AccessRole::CheckBox, s(Str::SettingsPresetInIso));
    m_presetInIso->onChange = [this](bool on) { edit([on](AppSettings& a) { a.presetInIso = on; }); };

    // D-140: WinLove's own updates — notify only (the About page says what the last check found).
    m_form->addSection(s(Str::SettingsUpdates));
    m_checkUpdates = &m_form->addRow<ui::Toggle>(s(Str::SettingsCheckUpdates), std::wstring(), ui::tokens::size::toggleW,
                                                 std::wstring(), true);
    m_checkUpdates->setAccessible(ui::AccessRole::CheckBox, s(Str::SettingsCheckUpdates));
    m_checkUpdates->onChange = [this](bool on) { edit([on](AppSettings& a) { a.checkUpdates = on; }); };
    setAccessible(ui::AccessRole::Group, s(Str::SettingsTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        // The image's lists too: one being read locks the language and font (audit CTL-09), and
        // the lock must go when it arrives, not at the next unrelated change. Lists are read only
        // while an image is mounted (unmounting tells Mount as well).
        const bool imageList = change == AppState::Change::Features || change == AppState::Change::Components ||
                               change == AppState::Change::Services || change == AppState::Change::ImageValues ||
                               change == AppState::Change::Drivers || change == AppState::Change::Intl;
        if (change == AppState::Change::Settings || change == AppState::Change::Mount ||
            change == AppState::Change::Operation || change == AppState::Change::Apply || change == AppState::Change::Iso ||
            (imageList && m_state.mounted())) {
            sync();
        }
    });
    sync();
}

SettingsPage::~SettingsPage() {
    m_state.unsubscribe(m_subscription);
}

bool SettingsPage::occupiedFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    return !folder.empty() && std::filesystem::is_directory(folder, ec) && !std::filesystem::is_empty(folder, ec);
}

bool SettingsPage::mountOccupied(const std::filesystem::path& typed, bool fresh) {
    if (fresh || !m_occupiedKnown || typed.native() != m_occupiedText) {
        m_occupiedKnown = true;
        m_occupiedText = typed.native();
        m_occupied = occupiedFolder(typed);
    }
    return m_occupied;
}

bool SettingsPage::foldersLocked() const {
    return m_state.mounted().has_value() || (m_intents.busy && m_intents.busy());
}

void SettingsPage::edit(const std::function<void(AppSettings&)>& change) {
    AppSettings settings = m_state.settings();
    change(settings);
    m_state.setSettings(std::move(settings));
    sync(); // also when nothing was saved: the control goes back
}

void SettingsPage::commitFolder(PathField& field, std::filesystem::path AppSettings::*member, bool allowEmpty) {
    if (foldersLocked()) {
        sync();
        return;
    }
    const std::filesystem::path path = field.text();
    if (path.empty() ? !allowEmpty : !path.is_absolute()) {
        sync(); // the hint says what is wrong; nothing is saved
        return;
    }
    if (member == &AppSettings::mountFolder && mountOccupied(path, /*fresh=*/true)) {
        sync(); // a mount folder with someone's files in it: never (MountHealth.h, Foreign)
        return;
    }
    edit([&](AppSettings& a) { a.*member = path.empty() ? path : path.lexically_normal(); });
}

void SettingsPage::openAccentPicker() {
    m_accent->openPicker();
}

void SettingsPage::resetToDefaults() {
    const AppSettings defaults;
    edit([&](AppSettings& a) {
        a.theme = defaults.theme;
        a.reduceMotion = defaults.reduceMotion;
        // A language change rebuilds the whole UI: never while a job runs (it was reset regardless,
        // and a running Uygula lost its controller, audit A6). The dropdown is locked then too.
        if (!(m_intents.busy && m_intents.busy())) {
            a.language = defaults.language;
        }
        if (!foldersLocked()) {
            a.workRoot = defaults.workRoot;
            a.mountFolder = defaults.mountFolder;
        }
        a.dismEngine = defaults.dismEngine; // D-123 (the engine itself follows on the next start)
        a.esdThreads = defaults.esdThreads;
        a.presetInIso = defaults.presetInIso;
        a.checkUpdates = defaults.checkUpdates; // D-140
    });
    core::setCompressionThreads(0);
}

void SettingsPage::sync() {
    const AppSettings& settings = m_state.settings();
    auto s = [&](Str key) { return m_strings.get(key); };
    m_theme->setSelected(static_cast<int>(settings.theme));
    m_accent->setSaved(settings.accent);
    if (m_motion->isOn() != settings.reduceMotion) {
        m_motion->setOn(settings.reduceMotion);
    }
    m_form->setHint(*m_motion, settings.reduceMotion ? std::wstring() : s(Str::SettingsFollowSystem));
    m_language->setSelected(settings.language == Language::English ? 1 : 0);
    m_font->setSelected(static_cast<int>(settings.font));
    m_density->setSelected(static_cast<int>(settings.density));
    m_form->setHint(*m_density, s(Str::SettingsDensityHint));
    // A language change rebuilds every widget: not while a job reports into them.
    const bool busy = m_intents.busy && m_intents.busy();
    m_language->setEnabled(!busy);
    m_font->setEnabled(!busy); // a new family rebuilds every widget too
    m_form->setHint(*m_font, busy ? s(Str::SettingsLockedHint) : std::wstring());
    m_form->setHint(*m_language, busy ? s(Str::SettingsLockedHint) : std::wstring());

    const bool locked = foldersLocked();
    auto path = [&](PathField& field, const std::filesystem::path& saved, bool allowEmpty) {
        field.setEnabled(!locked);
        if (!field.editing()) {
            field.setText(saved.wstring()); // typed text stays until Enter
        }
        const std::filesystem::path typed = field.text();
        if (locked) {
            m_form->setHint(field, s(Str::SettingsLockedHint));
        } else if (typed.empty() ? !allowEmpty : !typed.is_absolute()) {
            m_form->setHint(field, s(Str::SettingsPathInvalid), Color::StatusError);
        } else if (&field == m_mount && (typed.empty() ? typed : typed.lexically_normal()) != saved &&
                   mountOccupied(typed)) {
            m_form->setHint(field, s(Str::SettingsMountNotEmpty), Color::StatusError);
        } else if ((typed.empty() ? typed : typed.lexically_normal()) != saved) {
            m_form->setHint(field, s(Str::SettingsPressEnter));
        } else {
            m_form->setHint(field, {});
        }
    };
    path(*m_work, settings.workRoot, /*allowEmpty=*/false);
    path(*m_mount, settings.mountFolder, /*allowEmpty=*/true);

    // D-123
    const auto choice = std::ranges::find(m_dismChoices, settings.dismEngine);
    m_dismEngine->setSelected(choice == m_dismChoices.end() ? 0 : static_cast<int>(choice - m_dismChoices.begin()));
    const bool usingAdk = core::dismLocation().adk;
    const bool asStarted = settings.dismEngine == DismEngineChoice::Auto ||
                           (settings.dismEngine == DismEngineChoice::Adk) == usingAdk;
    m_form->setHint(*m_dismEngine, asStarted ? (settings.dismEngine == DismEngineChoice::Auto ? s(Str::SettingsDismAutoHint) : std::wstring())
                                             : s(Str::SettingsDismRestartHint));
    m_esdThreads->setSelected(std::clamp(settings.esdThreads, 0, m_processors));
    m_form->setHint(*m_esdThreads, s(Str::SettingsEsdThreadsHint));
    if (m_presetInIso->isOn() != settings.presetInIso) {
        m_presetInIso->setOn(settings.presetInIso);
    }
    m_form->setHint(*m_presetInIso, s(Str::SettingsPresetInIsoHint));
    if (m_checkUpdates->isOn() != settings.checkUpdates) {
        m_checkUpdates->setOn(settings.checkUpdates);
    }
    m_form->setHint(*m_checkUpdates, s(Str::SettingsCheckUpdatesHint));
    invalidate();
}

void SettingsPage::layout() {
    const RectF b = bounds();
    m_form->setBounds({b.x, b.y + kTop, b.width, std::max(b.height - kTop, 0.0f)});
}

} // namespace wl::app
