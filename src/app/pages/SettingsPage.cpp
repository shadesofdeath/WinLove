#include "app/pages/SettingsPage.h"

#include "app/SystemInfo.h"
#include "core/image/dism/HostDism.h"
#include "core/image/wim/WimGapi.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/SearchBox.h"

#include <algorithm>
#include <format>
#include <vector>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;

namespace {

constexpr float kTop = 7.0f;          // page top → first section band (screen 19)
constexpr float kLabelWidth = 240.0f;
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

// Screen 19: the accent swatches — 20×12, 2px radius, 12px apart, the selected one ringed with
// 1px text.primary 2px out; its name after them. Click or ←/→ picks one.
class AccentSwatches : public ui::Widget {
public:
    static constexpr float kW = 20.0f; // D-107: round swatches
    static constexpr float kH = 20.0f;
    static constexpr float kGap = 10.0f;

    explicit AccentSwatches(std::vector<std::wstring> names) : m_names(std::move(names)) {
        setFocusable(true);
        setAccessible(ui::AccessRole::Group, L"");
    }
    std::function<void(ui::Accent)> onChange;
    void setSelected(ui::Accent accent) {
        m_selected = accent;
        invalidate();
    }

    [[nodiscard]] ui::SizeF measure(ui::SizeF) override {
        return {ui::kAccentCount * (kW + kGap) + 120.0f, ui::tokens::size::control};
    }
    void paint(ui::Canvas& canvas) override {
        const ui::RectF b = bounds();
        const float y = b.y + (b.height - kH) / 2;
        for (int i = 0; i < ui::kAccentCount; ++i) {
            const ui::RectF swatch{b.x + 2 + i * (kW + kGap), y, kW, kH};
            canvas.fillRoundRect(swatch, kH / 2, ui::accentSwatch(static_cast<ui::Accent>(i)));
            if (static_cast<int>(m_selected) == i) {
                canvas.strokeRoundRect(swatch.inset(-3, -3), kH / 2 + 3, ui::tokens::Color::TextPrimary, 2.0f * canvas.scale());
            } else if (m_hover == i) {
                canvas.strokeRoundRect(swatch.inset(-3, -3), kH / 2 + 3, ui::tokens::Color::LineStrong, 2.0f * canvas.scale());
            }
        }
        const float x = b.x + 2 + ui::kAccentCount * (kW + kGap);
        canvas.drawText(m_names[static_cast<std::size_t>(m_selected)], {x, b.y, std::max(b.right() - x, 0.0f), b.height},
                        ui::tokens::TypeStyle::Body, ui::tokens::Color::TextSecondary);
    }
    [[nodiscard]] ui::RectF focusRect() const override {
        const ui::RectF b = bounds();
        return {b.x - 2, b.y + (b.height - kH) / 2 - 4, ui::kAccentCount * (kW + kGap), kH + 8};
    }
    void onPointerMove(ui::PointF p) override {
        const int hover = at(p);
        if (hover != m_hover) {
            m_hover = hover;
            setTooltip(hover >= 0 ? m_names[static_cast<std::size_t>(hover)] : std::wstring());
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
    void onClick() override { pick(m_down); }
    bool onKeyDown(const ui::KeyEvent& key) override {
        if (key.virtualKey == VK_LEFT || key.virtualKey == VK_RIGHT) {
            const int next = static_cast<int>(m_selected) + (key.virtualKey == VK_LEFT ? -1 : 1);
            pick(std::clamp(next, 0, ui::kAccentCount - 1));
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] int at(ui::PointF p) const {
        const ui::RectF b = bounds();
        const float x = p.x - b.x - 2;
        const int i = static_cast<int>(x / (kW + kGap));
        return x >= 0 && i < ui::kAccentCount && x - i * (kW + kGap) <= kW ? i : -1;
    }
    void pick(int index) {
        if (index < 0 || index >= ui::kAccentCount || index == static_cast<int>(m_selected)) {
            return;
        }
        m_selected = static_cast<ui::Accent>(index);
        invalidate();
        if (onChange) {
            onChange(m_selected);
        }
    }

    std::vector<std::wstring> m_names;
    ui::Accent m_selected = ui::Accent::Blue;
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
    m_accent = &m_form->addRow<AccentSwatches>(
        s(Str::SettingsAccent), std::wstring(), 0.0f,
        std::vector<std::wstring>{s(Str::SettingsAccentBlue), s(Str::SettingsAccentCopper), s(Str::SettingsAccentSea),
                                  s(Str::SettingsAccentPomegranate), s(Str::SettingsAccentOlive)});
    m_accent->setAccessible(ui::AccessRole::Group, s(Str::SettingsAccent));
    m_accent->onChange = [this](ui::Accent accent) { edit([accent](AppSettings& a) { a.accent = accent; }); };
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
    setAccessible(ui::AccessRole::Group, s(Str::SettingsTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Settings || change == AppState::Change::Mount ||
            change == AppState::Change::Operation || change == AppState::Change::Apply || change == AppState::Change::Iso) {
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
    if (member == &AppSettings::mountFolder && occupiedFolder(path)) {
        sync(); // a mount folder with someone's files in it: never (MountHealth.h, Foreign)
        return;
    }
    edit([&](AppSettings& a) { a.*member = path.empty() ? path : path.lexically_normal(); });
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
    });
    core::setCompressionThreads(0);
}

void SettingsPage::sync() {
    const AppSettings& settings = m_state.settings();
    auto s = [&](Str key) { return m_strings.get(key); };
    m_theme->setSelected(static_cast<int>(settings.theme));
    m_accent->setSelected(settings.accent);
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
                   occupiedFolder(typed)) {
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
    invalidate();
}

void SettingsPage::layout() {
    const RectF b = bounds();
    m_form->setBounds({b.x, b.y + kTop, b.width, std::max(b.height - kTop, 0.0f)});
}

} // namespace wl::app
