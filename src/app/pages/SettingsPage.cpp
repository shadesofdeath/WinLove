#include "app/pages/SettingsPage.h"

#include "ui/widgets/Button.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/SearchBox.h"

#include <windows.h>

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

// "C:\Windows\System32\dismapi.dll · 10.0.26100.1" — the library the engine loads (D-017).
std::wstring dismLibrary() {
    wchar_t system[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring path = std::wstring(system) + L"\\dismapi.dll";
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (size == 0 || !GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || !info) {
        return path;
    }
    return std::format(L"{} \u00b7 {}.{}.{}.{}", path, HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

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

SettingsPage::SettingsPage(AppState& state, const Localization& strings, Intents intents)
    : m_state(state), m_strings(strings), m_intents(std::move(intents)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_form = &add<ui::FormView>(kLabelWidth);

    m_form->addSection(s(Str::SettingsAppearance));
    m_theme = &m_form->addRow<ui::RadioGroup>(
        s(Str::SettingsTheme), std::wstring(), 0.0f,
        std::vector<std::wstring>{s(Str::SettingsThemeDark), s(Str::SettingsThemeLight), s(Str::SettingsThemeHc),
                                  s(Str::SettingsThemeSystem)},
        0);
    m_theme->setAccessible(ui::AccessRole::Group, s(Str::SettingsTheme));
    m_theme->onChange = [this](int index) {
        edit([index](AppSettings& a) { a.theme = static_cast<ThemeChoice>(std::clamp(index, 0, 3)); });
    };
    m_motion = &m_form->addRow<ui::Toggle>(s(Str::SettingsReduceMotion), std::wstring(), ui::tokens::size::toggleW,
                                           std::wstring(), false);
    m_motion->setAccessible(ui::AccessRole::CheckBox, s(Str::SettingsReduceMotion));
    m_motion->onChange = [this](bool on) { edit([on](AppSettings& a) { a.reduceMotion = on; }); };

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
    m_form->addRow<ui::Label>(s(Str::SettingsDismPath), std::wstring(), 0.0f, dismLibrary(), ui::tokens::TypeStyle::Mono,
                              Color::TextSecondary);
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
    edit([&](AppSettings& a) { a.*member = path.empty() ? path : path.lexically_normal(); });
}

void SettingsPage::resetToDefaults() {
    const AppSettings defaults;
    edit([&](AppSettings& a) {
        a.theme = defaults.theme;
        a.reduceMotion = defaults.reduceMotion;
        a.language = defaults.language;
        if (!foldersLocked()) {
            a.workRoot = defaults.workRoot;
            a.mountFolder = defaults.mountFolder;
        }
    });
}

void SettingsPage::sync() {
    const AppSettings& settings = m_state.settings();
    auto s = [&](Str key) { return m_strings.get(key); };
    m_theme->setSelected(static_cast<int>(settings.theme));
    if (m_motion->isOn() != settings.reduceMotion) {
        m_motion->setOn(settings.reduceMotion);
    }
    m_form->setHint(*m_motion, settings.reduceMotion ? std::wstring() : s(Str::SettingsFollowSystem));
    m_language->setSelected(settings.language == Language::English ? 1 : 0);
    // A language change rebuilds every widget: not while a job reports into them.
    const bool busy = m_intents.busy && m_intents.busy();
    m_language->setEnabled(!busy);
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
        } else if ((typed.empty() ? typed : typed.lexically_normal()) != saved) {
            m_form->setHint(field, s(Str::SettingsPressEnter));
        } else {
            m_form->setHint(field, {});
        }
    };
    path(*m_work, settings.workRoot, /*allowEmpty=*/false);
    path(*m_mount, settings.mountFolder, /*allowEmpty=*/true);
    invalidate();
}

void SettingsPage::layout() {
    const RectF b = bounds();
    m_form->setBounds({b.x, b.y + kTop, b.width, std::max(b.height - kTop, 0.0f)});
}

} // namespace wl::app
