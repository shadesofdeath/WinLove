#include "app/pages/LanguagesPage.h"

#include "app/Format.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kSection = 36.0f;
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 220.0f;
constexpr float kFieldWidth = 420.0f;
constexpr int kMaxTableRows = 6;
enum Column : int { kLang, kKind, kArch, kSize, kFile };
enum Field : int { kUi, kSystem, kUser, kKeyboard, kZone };
} // namespace

LanguagesPage::LanguagesPage(AppState& state, LanguageController& controller, const Localization& strings, Language language,
                             std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::LanguagesLanguage), 220},
        {strings.get(Str::CommonType), 180},
        {strings.get(Str::ImagesArch), 90},
        {strings.get(Str::CommonSize), 96, ui::TextAlign::Trailing},
        {strings.get(Str::LanguagesFile), 0},
    });
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState) {
        paintCell(c, row, column, rect);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_packs.size())) {
            m_controller.unqueuePack(m_packs[static_cast<std::size_t>(row)].path);
            return true;
        }
        return false;
    };
    const Str labels[5] = {Str::LanguagesUi, Str::LanguagesSystem, Str::LanguagesUser, Str::LanguagesKeyboard, Str::LanguagesZone};
    for (int f = 0; f < 5; ++f) {
        m_fields[f] = &add<ui::Dropdown>(L"", std::vector<std::wstring>{strings.get(Str::LanguagesUnchanged)}, 0);
        m_fields[f]->setAccessible(ui::AccessRole::Group, strings.get(labels[f]));
        m_fields[f]->onChange = [this, f](int index) { picked(f, index); };
    }
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::LanguageGlobe, strings.get(Str::LanguagesNoMountTitle),
                                   strings.get(Str::LanguagesNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::LanguagesTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Intl || change == AppState::Change::Queue) {
            refresh();
        }
    });
    m_controller.load();
    refresh();
}

LanguagesPage::~LanguagesPage() {
    m_state.unsubscribe(m_subscription);
}

std::wstring LanguagesPage::kindLabel(const Localization& strings, core::LanguagePackFile::Kind kind) {
    using K = core::LanguagePackFile::Kind;
    switch (kind) {
    case K::LanguagePack: return strings.get(Str::LanguagesKindPack);
    case K::Basic: return strings.get(Str::LanguagesKindBasic);
    case K::Handwriting: return strings.get(Str::LanguagesKindHandwriting);
    case K::Ocr: return strings.get(Str::LanguagesKindOcr);
    case K::Speech: return strings.get(Str::LanguagesKindSpeech);
    case K::TextToSpeech: return strings.get(Str::LanguagesKindTts);
    case K::Fonts: return strings.get(Str::LanguagesKindFonts);
    default: return strings.get(Str::LanguagesKindOther);
    }
}

void LanguagesPage::rebuildDropdowns() {
    const auto settings = m_controller.settings();
    const auto& intl = m_state.imageIntl();
    const bool ready = intl && intl->status == AppState::ImageIntl::Status::Ready;
    const core::IntlSettings& now = ready ? intl->intl.current : core::IntlSettings{};
    auto unchanged = [&](const std::wstring& current, const std::wstring& shown) {
        return current.empty() ? m_strings.get(Str::LanguagesUnchanged)
                               : m_strings.format(Str::LanguagesUnchangedNow, {{L"now", shown.empty() ? current : shown}});
    };
    auto fill = [&](int field, const std::vector<IntlChoice>& choices, const std::wstring& currentValue,
                    const std::wstring& currentShown, const std::wstring& selectedValue) {
        std::vector<std::wstring> items{unchanged(currentValue, currentShown)};
        m_values[field] = {L""};
        int selected = 0;
        for (const auto& c : choices) {
            items.push_back(c.display == c.value ? c.value : c.display + L"  ·  " + c.value);
            m_values[field].push_back(c.value);
            if (!selectedValue.empty() && _wcsicmp(c.value.c_str(), selectedValue.c_str()) == 0) {
                selected = static_cast<int>(m_values[field].size()) - 1;
            }
        }
        m_fields[field]->setItems(std::move(items), selected);
    };
    std::vector<IntlChoice> ui;
    for (const auto& l : m_controller.uiLanguages()) {
        ui.push_back({l, LanguageController::localeName(l)});
    }
    fill(kUi, ui, now.uiLanguage, LanguageController::localeName(now.uiLanguage), settings.uiLanguage);
    fill(kSystem, LanguageController::locales(), now.systemLocale, LanguageController::localeName(now.systemLocale),
         settings.systemLocale);
    fill(kUser, LanguageController::locales(), now.userLocale, LanguageController::localeName(now.userLocale), settings.userLocale);
    std::wstring keyboardShown;
    for (const auto& k : LanguageController::keyboards()) {
        if (_wcsicmp(k.value.c_str(), now.inputLocale.c_str()) == 0) {
            keyboardShown = k.display;
        }
    }
    fill(kKeyboard, LanguageController::keyboards(), now.inputLocale, keyboardShown, settings.inputLocale);
    std::wstring zoneShown;
    for (const auto& z : LanguageController::timeZones()) {
        if (z.value == now.timeZone) {
            zoneShown = z.display;
        }
    }
    fill(kZone, LanguageController::timeZones(), now.timeZone, zoneShown, settings.timeZone);
}

void LanguagesPage::picked(int field, int index) {
    auto settings = m_controller.settings();
    const std::wstring value =
        index >= 0 && index < static_cast<int>(m_values[field].size()) ? m_values[field][static_cast<std::size_t>(index)] : L"";
    switch (field) {
    case kUi: settings.uiLanguage = value; break;
    case kSystem: settings.systemLocale = value; break;
    case kUser: settings.userLocale = value; break;
    case kKeyboard: settings.inputLocale = value; break;
    case kZone: settings.timeZone = value; break;
    default: break;
    }
    m_controller.setSettings(settings);
}

void LanguagesPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_table->setVisible(mounted);
    for (auto* f : m_fields) {
        f->setVisible(mounted);
    }
    if (mounted) {
        m_controller.load();
    }
    m_packs = m_controller.queuedPacks();
    m_table->setRowCount(static_cast<int>(m_packs.size()));
    m_table->refresh();
    rebuildDropdowns();
    layout();
    invalidate();
}

void LanguagesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect) {
    if (row < 0 || row >= static_cast<int>(m_packs.size())) {
        return;
    }
    const auto& f = m_packs[static_cast<std::size_t>(row)];
    switch (column) {
    case kLang: {
        const std::wstring text = f.language.empty() ? L"—" : LanguageController::localeName(f.language) + L"  ·  " + f.language;
        canvas.drawText(text, rect, TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case kKind: canvas.drawText(kindLabel(m_strings, f.kind), rect, TypeStyle::Caption, Color::TextSecondary); break;
    case kArch: canvas.drawText(f.architecture, rect, TypeStyle::Mono, Color::TextSecondary); break;
    case kSize:
        canvas.drawText(formatBytes(f.size, m_language), rect, TypeStyle::Mono, Color::TextPrimary, ui::TextAlign::Trailing);
        break;
    case kFile: canvas.drawText(f.path.filename().wstring(), rect, TypeStyle::Caption, Color::TextTertiary); break;
    default: break;
    }
}

void LanguagesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop + kSection + 24 + 12; // İMAJDAKİ DİLLER + its line
    y += kSection;                              // EKLENECEK DİL PAKETLERİ
    const int rows = std::clamp(static_cast<int>(m_packs.size()), 1, kMaxTableRows);
    const float tableH = ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(rows) + 1;
    m_table->setBounds({b.x, y, b.width, tableH});
    y += tableH + 12 + kSection; // BÖLGE VE DİL
    for (auto* f : m_fields) {
        f->setBounds({b.x + kLabelWidth, y + (kRow - ui::tokens::size::control) / 2, kFieldWidth, ui::tokens::size::control});
        y += kRow;
    }
}

void LanguagesPage::paint(ui::Canvas& canvas) {
    if (!m_state.mounted()) {
        return;
    }
    const RectF b = bounds();
    float y = b.y + kTop;
    auto section = [&](Str title) {
        canvas.drawText(m_strings.get(title), {b.x, y + 8, 400, 20}, TypeStyle::Section, Color::TextSecondary);
        canvas.hairlineH(b.x, y + kSection - 6, b.width, Color::LineSubtle);
        y += kSection;
    };
    section(Str::LanguagesInImage);
    const auto& intl = m_state.imageIntl();
    std::wstring line;
    Color ink = Color::TextPrimary;
    if (!intl || intl->status == AppState::ImageIntl::Status::Loading) {
        line = m_strings.get(Str::LanguagesReading);
        ink = Color::TextTertiary;
    } else if (intl->status == AppState::ImageIntl::Status::Failed) {
        line = m_strings.get(Str::LanguagesReadFailed) + L" — " + intl->error.message;
        ink = Color::StatusError;
    } else {
        for (const auto& l : intl->intl.languages) {
            line += (line.empty() ? L"" : L", ") + LanguageController::localeName(l) + L" (" + l + L")";
        }
        line = m_strings.format(Str::LanguagesInstalled, {{L"list", line}, {L"ui", intl->intl.current.uiLanguage}});
    }
    canvas.drawText(line, {b.x, y, b.width, 24}, TypeStyle::Body, ink);
    y += 24 + 12;
    section(Str::LanguagesPacks);
    if (m_packs.empty()) {
        const RectF t = m_table->bounds();
        canvas.drawText(m_strings.get(Str::LanguagesPacksEmpty), {t.x, t.y + ui::TableView::kHeader, t.width, ui::TableView::kRow},
                        TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Center);
    }
    y = m_table->bounds().bottom() + 12;
    section(Str::LanguagesRegion);
    const Str labels[5] = {Str::LanguagesUi, Str::LanguagesSystem, Str::LanguagesUser, Str::LanguagesKeyboard, Str::LanguagesZone};
    for (const Str label : labels) {
        canvas.drawText(m_strings.get(label), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        y += kRow;
    }
    canvas.drawText(m_strings.get(Str::LanguagesRegionHint), {b.x, y + 4, b.width, 20}, TypeStyle::Caption, Color::TextTertiary);
}

} // namespace wl::app
