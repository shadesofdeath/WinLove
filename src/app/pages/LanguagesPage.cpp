#include "app/pages/LanguagesPage.h"

#include "app/Format.h"
#include "app/controllers/LanguageFetchController.h"
#include "app/pages/PageBits.h"
#include "app/pages/languages/LanguageAddDialog.h"
#include "ui/anim/Tween.h"

#include <algorithm>
#include <format>

namespace wl::app {

using Kind = core::LanguagePackFile::Kind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kTop = 12.0f;
constexpr float kSection = 36.0f;
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 220.0f;
constexpr float kFieldWidth = 420.0f;
constexpr float kStrip = 52.0f;
constexpr float kInfo = 32.0f;
constexpr int kMaxTableRows = 8;
enum Column : int { kLang, kState, kParts, kComponents, kSize };
enum Field : int { kUi, kSystem, kUser, kKeyboard, kZone };

std::wstring languageList(const std::vector<std::wstring>& tags) {
    std::wstring text;
    for (const auto& t : tags) {
        text += (text.empty() ? L"" : L", ") + LanguageController::localeName(t);
    }
    return text;
}
} // namespace

// D-061: the lookup / download of "Dil ekle…", above the list: spinner, what is happening,
// bytes, a 2px bar and "Durdur" (the .part files stay; the next download resumes).
class LanguagesPage::FetchStrip : public ui::Widget {
public:
    FetchStrip(const Localization& strings, const AppState& state, Language language)
        : m_strings(strings), m_state(state), m_language(language) {
        m_stop = &add<ui::Button>(ui::ButtonKind::Secondary, strings.get(Str::StatusStop), ui::icons::Icon::Stop);
        m_stop->onInvoke = [this] {
            if (onStop) {
                onStop();
            }
        };
    }
    std::function<void()> onStop;

    void start() {
        m_now = ui::nowMs();
        animate();
    }
    bool tick(double now) override {
        m_now = now;
        invalidate();
        return visible() && m_state.languageFetch().has_value();
    }
    void layout() override {
        const RectF b = bounds();
        const ui::SizeF size = m_stop->measure({});
        m_stop->setBounds({b.right() - 8 - size.width, b.y + (b.height - size.height) / 2, size.width, size.height});
    }
    void paint(ui::Canvas& canvas) override {
        const auto& fetch = m_state.languageFetch();
        if (!fetch) {
            return;
        }
        const RectF b = bounds();
        canvas.fillRect(b, Color::BgPanel);
        const float angle = static_cast<float>(static_cast<int>(m_now / 100.0) % 8) * 45.0f;
        canvas.drawIcon(ui::icons::Icon::Spinner, {b.x + 17, b.y + 12}, Color::AccentBase, ui::IconVariant::Regular16, 0,
                        ui::reducedMotion() ? 0.0f : angle);
        const float x = b.x + 40;
        const float width = std::max(m_stop->bounds().x - x - 16, 0.0f);
        using Stage = AppState::LanguageFetch::Stage;
        std::wstring title;
        if (fetch->stage == Stage::Searching) {
            const auto target = LanguageFetchController::targetFor(m_state);
            title = m_strings.format(Str::LanguagesFetchSearching,
                                     {{L"build", target ? std::format(L"{}.{}", target->build, target->revision) : std::wstring()}});
        } else if (fetch->stage == Stage::Verifying) {
            title = m_strings.format(Str::LanguagesFetchVerifying, {{L"langs", languageList(fetch->languages)}});
        } else {
            title = m_strings.format(Str::LanguagesFetchDownloading, {{L"langs", languageList(fetch->languages)},
                                                                      {L"done", formatBytes(fetch->doneBytes, m_language)},
                                                                      {L"total", formatBytes(fetch->totalBytes, m_language)}});
        }
        canvas.drawText(title, {x, b.y + 10, width, 16}, TypeStyle::BodyStrong, Color::TextPrimary);
        if (fetch->stage != Stage::Searching && fetch->totalBytes > 0) {
            const float fraction = std::clamp(
                static_cast<float>(static_cast<double>(fetch->doneBytes) / static_cast<double>(fetch->totalBytes)), 0.0f, 1.0f);
            const float barWidth = std::max(width - 48, 0.0f);
            canvas.progressBar({x, b.y + 34, barWidth, 2}, fraction);
            canvas.drawText(std::to_wstring(static_cast<int>(fraction * 100)) + L"%", {x + barWidth + 8, b.y + 27, 40, 16},
                            TypeStyle::Mono, Color::TextSecondary, ui::TextAlign::Trailing);
        }
    }

private:
    const Localization& m_strings;
    const AppState& m_state;
    Language m_language;
    ui::Button* m_stop = nullptr;
    double m_now = 0;
};

LanguagesPage::LanguagesPage(AppState& state, LanguageController& controller, const Localization& strings, Language language,
                             Intents intents)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    m_fetch = &add<FetchStrip>(strings, state, language);
    m_fetch->onStop = [this] {
        if (m_intents.stopFetch) {
            m_intents.stopFetch();
        }
    };
    m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::LanguagesLanguage), 0},
        {strings.get(Str::LanguagesColState), 210},
        {strings.get(Str::LanguagesColParts), 300},
        {strings.get(Str::LanguagesColComponents), 120, ui::TextAlign::Trailing},
        {strings.get(Str::CommonSize), 96, ui::TextAlign::Trailing},
    });
    m_table->setAccessible(ui::AccessRole::Group, strings.get(Str::LanguagesList));
    m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintCell(c, row, column, rect, cell.selected);
    };
    m_table->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_table->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_rows.size())) {
            const auto& r = m_rows[static_cast<std::size_t>(row)];
            if (!r.queued.empty() || r.componentsQueued > 0) {
                m_controller.unqueueLanguage(r.language);
            }
            return true;
        }
        return false;
    };
    m_lcu = &add<ui::InfoBar>(ui::InfoKind::Warning, strings.get(Str::LanguagesLcuTitle), L"", strings.get(Str::CommonClose));
    m_lcu->setAction(strings.get(Str::LanguagesLcuAction), [this] {
        if (m_intents.findUpdates) {
            m_intents.findUpdates();
        }
    });
    m_lcu->onClose = [this] {
        m_lcuClosed = true;
        refresh();
    };
    const Str labels[5] = {Str::LanguagesUi, Str::LanguagesSystem, Str::LanguagesUser, Str::LanguagesKeyboard, Str::LanguagesZone};
    for (int f = 0; f < 5; ++f) {
        m_fields[f] = &add<ui::Dropdown>(L"", std::vector<std::wstring>{strings.get(Str::LanguagesUnchanged)}, 0);
        m_fields[f]->setAccessible(ui::AccessRole::Group, strings.get(labels[f]));
        m_fields[f]->onChange = [this, f](int index) { picked(f, index); };
    }
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::LanguageGlobe, strings.get(Str::LanguagesNoMountTitle),
                                   strings.get(Str::LanguagesNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = [this] {
        if (m_intents.goImages) {
            m_intents.goImages();
        }
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::LanguagesTitle));
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount || change == AppState::Change::Intl || change == AppState::Change::Queue ||
            change == AppState::Change::LanguageFetch || change == AppState::Change::Source) {
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
    switch (kind) {
    case Kind::LanguagePack: return strings.get(Str::LanguagesKindPack);
    case Kind::Basic: return strings.get(Str::LanguagesKindBasic);
    case Kind::Handwriting: return strings.get(Str::LanguagesKindHandwriting);
    case Kind::Ocr: return strings.get(Str::LanguagesKindOcr);
    case Kind::Speech: return strings.get(Str::LanguagesKindSpeech);
    case Kind::TextToSpeech: return strings.get(Str::LanguagesKindTts);
    case Kind::Fonts: return strings.get(Str::LanguagesKindFonts);
    case Kind::Satellite: return strings.get(Str::LanguagesKindComponent);
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
    const bool fetching = mounted && m_state.languageFetch().has_value();
    if (fetching && !m_fetch->visible()) {
        m_fetch->setVisible(true);
        m_fetch->start();
    }
    m_fetch->setVisible(fetching);
    const bool lcu = mounted && !m_lcuClosed && m_controller.cumulativeUpdateAdvised();
    m_lcu->setVisible(lcu);
    if (lcu) {
        const auto target = LanguageFetchController::targetFor(m_state);
        m_lcu->set(ui::InfoKind::Warning, m_strings.get(Str::LanguagesLcuTitle),
                   m_strings.format(Str::LanguagesLcuBody,
                                    {{L"build", target ? std::format(L"{}.{}", target->build, target->revision) : std::wstring()}}));
    }
    if (mounted) {
        m_controller.load();
    }
    m_rows = mounted ? m_controller.rows() : std::vector<LanguageRow>{};
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->refresh();
    rebuildDropdowns();
    layout();
    invalidate();
}

void LanguagesPage::paintCell(ui::Canvas& canvas, int row, int column, RectF rect, bool selected) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    const auto& r = m_rows[static_cast<std::size_t>(row)];
    const bool adding = !r.queued.empty() || r.componentsQueued > 0;
    switch (column) {
    case kLang: {
        // A small mark: filled for the display language, accent for a language that is added.
        const float cy = rect.y + rect.height / 2;
        if (r.ui || !r.inImage) {
            canvas.fillRoundRect({rect.x + 1, cy - 3, 6, 6}, 3, r.inImage ? Color::TextSecondary : Color::AccentBase);
        } else {
            canvas.strokeRoundRect({rect.x + 1.5f, cy - 2.5f, 5, 5}, 2.5f, Color::TextTertiary, 1.0f);
        }
        const float x = rect.x + 16;
        const std::wstring name = LanguageController::localeName(r.language);
        const TypeStyle style = selected || r.ui ? TypeStyle::BodyStrong : TypeStyle::Body;
        const float nameW = std::min(std::max(rect.right() - x, 0.0f), std::ceil(canvas.text().measure(name, style)));
        canvas.drawText(name, {x, rect.y, nameW, rect.height}, style, Color::TextPrimary);
        const float tx = x + nameW + 8;
        canvas.drawText(r.language, {tx, rect.y, std::max(rect.right() - tx, 0.0f), rect.height}, TypeStyle::Mono, Color::TextTertiary);
        break;
    }
    case kState: {
        const Str state = r.inImage ? (adding ? Str::LanguagesStateMore : r.ui ? Str::LanguagesStateUi : Str::LanguagesStateImage)
                                    : Str::LanguagesStateQueued;
        canvas.drawText(m_strings.get(state), rect, TypeStyle::Caption, adding ? Color::AccentBase : Color::TextSecondary);
        break;
    }
    case kParts: {
        // In the image: secondary ink; added by the queue: accent.
        float x = rect.x;
        bool first = true;
        for (const Kind k : {Kind::Basic, Kind::Handwriting, Kind::Ocr, Kind::TextToSpeech, Kind::Speech}) {
            const bool queued = std::ranges::find(r.queued, k) != r.queued.end();
            const bool installed = std::ranges::find(r.installed, k) != r.installed.end();
            if (!queued && !installed) {
                continue;
            }
            const std::wstring text = (first ? L"" : L" · ") + languagePartsText(m_strings, {k});
            const float w = std::ceil(canvas.text().measure(text, TypeStyle::Caption));
            if (x + w > rect.right()) {
                canvas.drawText(L"…", {x, rect.y, 12, rect.height}, TypeStyle::Caption, Color::TextTertiary);
                break;
            }
            canvas.drawText(text, {x, rect.y, w, rect.height}, TypeStyle::Caption, queued ? Color::AccentBase : Color::TextSecondary);
            x += w;
            first = false;
        }
        if (first) {
            canvas.drawText(L"—", rect, TypeStyle::Caption, Color::TextTertiary);
        }
        break;
    }
    case kComponents: {
        std::wstring text = r.componentsInImage > 0 ? std::to_wstring(r.componentsInImage) : std::wstring();
        if (r.componentsQueued > 0) {
            text += (text.empty() ? L"+" : L" +") + std::to_wstring(r.componentsQueued);
        }
        canvas.drawText(text.empty() ? L"—" : text, rect, TypeStyle::Mono,
                        r.componentsQueued > 0 ? Color::AccentBase : Color::TextSecondary, ui::TextAlign::Trailing);
        break;
    }
    case kSize:
        canvas.drawText(r.queuedBytes > 0 ? formatBytes(r.queuedBytes, m_language) : L"—", rect, TypeStyle::Mono,
                        r.queuedBytes > 0 ? Color::TextPrimary : Color::TextTertiary, ui::TextAlign::Trailing);
        break;
    default: break;
    }
}

void LanguagesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    float y = b.y + kTop;
    if (m_fetch->visible()) {
        m_fetch->setBounds({b.x, y, b.width, kStrip});
        y += kStrip + 12;
    }
    y += kSection; // DİLLER
    const int rows = std::clamp(static_cast<int>(m_rows.size()), 1, kMaxTableRows);
    const float tableH = ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(rows) + 1;
    m_table->setBounds({b.x, y, b.width, tableH});
    y += tableH + 4 + 16 + 8; // the hint line
    if (m_lcu->visible()) {
        m_lcu->setBounds({b.x, y, b.width, kInfo});
        y += kInfo + 8;
    }
    y += kSection; // BÖLGE VE DİL
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
    const RectF table = m_table->bounds();
    paintFormSection(canvas, {b.x, table.y - kSection, b.width, kSection}, m_strings.get(Str::LanguagesList));
    const auto& intl = m_state.imageIntl();
    if (m_rows.empty()) {
        std::wstring line = m_strings.get(Str::LanguagesReading);
        Color ink = Color::TextTertiary;
        if (intl && intl->status == AppState::ImageIntl::Status::Failed) {
            line = m_strings.get(Str::LanguagesReadFailed) + L" — " + intl->error.message;
            ink = Color::StatusError;
        }
        canvas.drawText(line, {table.x, table.y + ui::TableView::kHeader, table.width, ui::TableView::kRow}, TypeStyle::Caption, ink,
                        ui::TextAlign::Center);
    }
    canvas.drawText(m_strings.get(Str::LanguagesListHint), {b.x, table.bottom() + 4, b.width, 16}, TypeStyle::Caption, Color::TextTertiary);
    float y = m_fields[0]->bounds().y - (kRow - ui::tokens::size::control) / 2;
    paintFormSection(canvas, {b.x, y - kSection, b.width, kSection}, m_strings.get(Str::LanguagesRegion));
    const Str labels[5] = {Str::LanguagesUi, Str::LanguagesSystem, Str::LanguagesUser, Str::LanguagesKeyboard, Str::LanguagesZone};
    for (const Str label : labels) {
        canvas.drawText(m_strings.get(label), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        y += kRow;
    }
    canvas.drawText(m_strings.get(Str::LanguagesRegionHint), {b.x, y + 4, b.width, 20}, TypeStyle::Caption, Color::TextTertiary);
}

} // namespace wl::app
