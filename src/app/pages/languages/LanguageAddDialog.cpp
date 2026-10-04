#include "app/pages/languages/LanguageAddDialog.h"

#include "app/Format.h"
#include "base/Text.h"
#include "core/updates/UpdateCatalog.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <set>

namespace wl::app {

using Kind = core::LanguagePackFile::Kind;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kWidth = 760.0f;
constexpr int kVisibleRows = 10;
constexpr float kGap = 8.0f;
enum Column : int { kName, kTag, kParts, kSize };

// The dialog's body: search, list, parts.
class Picker : public ui::Widget {
public:
    Picker(const Localization& strings, Language language, const LanguageController& controller,
           std::vector<core::UupLanguage> languages)
        : m_strings(strings), m_language(language), m_controller(controller), m_all(std::move(languages)) {
        // By the name shown, as the user reads them.
        std::ranges::sort(m_all, [](const core::UupLanguage& a, const core::UupLanguage& b) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, LanguageController::localeName(a.language).c_str(),
                                   -1, LanguageController::localeName(b.language).c_str(), -1, nullptr, nullptr,
                                   0) == CSTR_LESS_THAN;
        });
        m_search = &add<ui::SearchBox>(strings.get(Str::LanguagesAddSearch));
        m_search->onChange = [this](const std::wstring&) { filter(); };
        m_table = &add<ui::TableView>(std::vector<ui::TableColumn>{
            {strings.get(Str::LanguagesLanguage), 0},
            {L"", 96},
            {strings.get(Str::LanguagesColParts), 300},
            {strings.get(Str::CommonSize), 84, ui::TextAlign::Trailing},
        });
        m_table->setAccessible(ui::AccessRole::Group, strings.get(Str::LanguagesAddTitle));
        m_table->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
            paintCell(c, row, column, rect, cell);
        };
        m_table->onCellClick = [this](int row, int, ui::PointF) { toggle(row); };
        m_table->onKey = [this](const ui::KeyEvent& key) {
            if (key.virtualKey == VK_SPACE) {
                toggle(m_table->selected());
                return true;
            }
            return false;
        };
        // Optional parts: on by default, what NTLite adds too.
        const Str labels[] = {Str::LanguagesPartHandwriting, Str::LanguagesPartOcr, Str::LanguagesPartTts, Str::LanguagesPartSpeech};
        for (const Str label : labels) {
            auto* field = &add<ui::CheckField>(strings.get(label), true);
            field->onChange = [this](bool) { partsChanged(); };
            m_parts.push_back(field);
        }
        m_components = &add<ui::CheckField>(strings.format(Str::LanguagesAddComponents, {{L"n", std::to_wstring(componentCount())}}), true);
        m_components->onChange = [this](bool) { partsChanged(); };
        m_setUi = &add<ui::CheckField>(strings.get(Str::LanguagesAddSetUi), true);
        m_setUi->onChange = [this](bool) { notify(); };
        m_setUi->setEnabled(false); // one language picked
        // What each language would take with every part: a language the image has in full is no choice.
        for (const auto& l : m_all) {
            m_complete.push_back(m_controller.pick(l, LanguageParts{}).empty());
        }
        filter();
    }

    std::function<void()> onChange;

    [[nodiscard]] ui::SearchBox* search() const { return m_search; }
    [[nodiscard]] LanguageParts parts() const {
        return LanguageParts{m_parts[0]->checked(), m_parts[1]->checked(), m_parts[2]->checked(), m_parts[3]->checked(),
                             m_components->checked()};
    }
    [[nodiscard]] std::vector<const core::UupLanguage*> chosen() const {
        std::vector<const core::UupLanguage*> out;
        for (const auto& l : m_all) {
            if (m_checked.contains(l.language)) {
                out.push_back(&l);
            }
        }
        return out;
    }
    [[nodiscard]] std::vector<core::UupLanguageFile> files() const {
        std::vector<core::UupLanguageFile> out;
        std::set<std::wstring> seen; // the fonts of two languages of one script go once
        for (const auto* l : chosen()) {
            for (auto& f : m_controller.pick(*l, parts())) {
                if (seen.insert(text::lower(f.source.name)).second) {
                    out.push_back(std::move(f));
                }
            }
        }
        return out;
    }
    [[nodiscard]] std::wstring uiLanguage() const {
        const auto c = chosen();
        return c.size() == 1 && m_setUi->checked() && !m_controller.imageHasLanguage(c.front()->language) ? c.front()->language
                                                                                                         : std::wstring();
    }

    void layout() override {
        const RectF b = bounds();
        float y = b.y;
        m_search->setBounds({b.x, y, 240, ui::tokens::size::control});
        y += ui::tokens::size::control + kGap;
        const float tableH = ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows + 1;
        m_table->setBounds({b.x, y, b.width, tableH});
        y += tableH + 12;
        float x = b.x;
        for (auto* field : m_parts) {
            const auto size = field->measure({b.width, ui::tokens::size::control});
            field->setBounds({x, y, size.width, ui::tokens::size::control});
            x += size.width + 16;
        }
        const auto cs = m_components->measure({b.width, ui::tokens::size::control});
        m_components->setBounds({x, y, cs.width, ui::tokens::size::control});
        y += ui::tokens::size::control + 4;
        const auto us = m_setUi->measure({b.width, ui::tokens::size::control});
        m_setUi->setBounds({b.x, y, us.width, ui::tokens::size::control});
        m_noteRect = {b.x + us.width + 16, y, std::max(b.right() - b.x - us.width - 16, 0.0f), ui::tokens::size::control};
    }
    void paint(ui::Canvas& canvas) override {
        canvas.drawText(m_strings.get(Str::LanguagesAddAlways), m_noteRect, TypeStyle::Caption, Color::TextTertiary);
    }
    [[nodiscard]] static float height() {
        return ui::tokens::size::control + kGap + ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows + 1 + 12 +
               ui::tokens::size::control * 2 + 4;
    }

private:
    void notify() {
        m_setUi->setEnabled(chosen().size() == 1);
        if (onChange) {
            onChange();
        }
    }
    void partsChanged() {
        // Speech needs Text to speech (DISM installs it as a dependency): ticking one ticks both.
        if (m_parts[3]->checked() && !m_parts[2]->checked()) {
            m_parts[2]->setChecked(true);
        }
        m_table->refresh();
        notify();
    }
    void filter() {
        const std::wstring q = text::lower(m_search->text());
        m_rows.clear();
        for (std::size_t i = 0; i < m_all.size(); ++i) {
            const auto& tag = m_all[i].language;
            const std::wstring name = LanguageController::localeName(tag);
            if (q.empty() || text::lower(name).find(q) != std::wstring::npos || text::lower(tag).find(q) != std::wstring::npos) {
                m_rows.push_back(i);
            }
        }
        m_table->setRowCount(static_cast<int>(m_rows.size()));
        m_table->refresh();
    }
    void toggle(int row) {
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const std::size_t i = m_rows[static_cast<std::size_t>(row)];
        if (m_complete[i]) {
            return;
        }
        const auto& tag = m_all[i].language;
        if (!m_checked.erase(tag)) {
            m_checked.insert(tag);
        }
        m_table->refresh();
        notify();
    }
    [[nodiscard]] int componentCount() const {
        // The same components take a language file whatever the language: count them on the first.
        for (const auto& l : m_all) {
            const auto files = m_controller.pick(l, LanguageParts{false, false, false, false, true});
            const auto n = std::ranges::count_if(files, [](const core::UupLanguageFile& f) { return f.file.kind == Kind::Satellite; });
            if (n > 0) {
                return static_cast<int>(n);
            }
        }
        return 0;
    }
    void paintCell(ui::Canvas& canvas, int row, int column, RectF rect, ui::TableView::CellState cell) {
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        const std::size_t i = m_rows[static_cast<std::size_t>(row)];
        const auto& l = m_all[i];
        const bool blocked = m_complete[i];
        switch (column) {
        case kName: {
            if (blocked) {
                canvas.pushOpacity(ui::tokens::opacity::disabled);
            }
            ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                                   blocked || m_checked.contains(l.language) ? ui::CheckState::On : ui::CheckState::Off,
                                   cell.hovered && !blocked);
            if (blocked) {
                canvas.popOpacity();
            }
            const float x = rect.x + ui::Checkbox::kBox + 8;
            canvas.drawText(LanguageController::localeName(l.language), {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                            cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, blocked ? Color::TextTertiary : Color::TextPrimary);
            break;
        }
        case kTag: canvas.drawText(l.language, rect, TypeStyle::Mono, Color::TextTertiary); break;
        case kParts: {
            if (blocked) {
                canvas.drawText(m_strings.get(Str::LanguagesAddInImage), rect, TypeStyle::Caption, Color::TextTertiary);
                break;
            }
            std::vector<Kind> kinds;
            for (const auto& f : l.files) {
                if (f.file.kind != Kind::LanguagePack && f.file.kind != Kind::Satellite &&
                    std::ranges::find(kinds, f.file.kind) == kinds.end()) {
                    kinds.push_back(f.file.kind);
                }
            }
            canvas.drawText(languagePartsText(m_strings, kinds), rect, TypeStyle::Caption, Color::TextSecondary);
            break;
        }
        case kSize: {
            if (blocked) {
                break;
            }
            std::uint64_t bytes = 0;
            for (const auto& f : m_controller.pick(l, parts())) {
                bytes += f.source.size;
            }
            canvas.drawText(formatBytes(bytes, m_language), rect, TypeStyle::Mono,
                            m_checked.contains(l.language) ? Color::TextPrimary : Color::TextSecondary, ui::TextAlign::Trailing);
            break;
        }
        default: break;
        }
    }

    const Localization& m_strings;
    Language m_language;
    const LanguageController& m_controller;
    std::vector<core::UupLanguage> m_all;
    std::vector<bool> m_complete;  // per m_all: nothing left to add
    std::vector<std::size_t> m_rows; // m_all indexes shown (search)
    std::set<std::wstring> m_checked;
    ui::SearchBox* m_search = nullptr;
    ui::TableView* m_table = nullptr;
    std::vector<ui::CheckField*> m_parts; // handwriting, OCR, text to speech, speech
    ui::CheckField* m_components = nullptr;
    ui::CheckField* m_setUi = nullptr;
    RectF m_noteRect{};
};

} // namespace

std::wstring languagePartsText(const Localization& strings, const std::vector<Kind>& kinds) {
    std::wstring text;
    for (const Kind k : {Kind::Basic, Kind::Handwriting, Kind::Ocr, Kind::TextToSpeech, Kind::Speech, Kind::Fonts}) {
        if (std::ranges::find(kinds, k) == kinds.end()) {
            continue;
        }
        const Str label = k == Kind::Basic         ? Str::LanguagesPartBasic
                          : k == Kind::Handwriting  ? Str::LanguagesPartHandwriting
                          : k == Kind::Ocr          ? Str::LanguagesPartOcr
                          : k == Kind::TextToSpeech ? Str::LanguagesPartTts
                          : k == Kind::Speech       ? Str::LanguagesPartSpeech
                                                    : Str::LanguagesPartFonts;
        text += (text.empty() ? L"" : L" · ") + strings.get(label);
    }
    return text;
}

LanguageAddDialog makeLanguageAddDialog(const Localization& strings, Language language, const LanguageController& controller,
                                        const LanguageTarget& target, std::vector<core::UupLanguage> languages,
                                        LanguageAddActions actions) {
    const auto release = core::catalogTarget(target.build, target.revision, target.architecture);
    const std::wstring body = strings.format(Str::LanguagesAddBody, {{L"release", std::format(L"{} {}", release.windows, release.release)},
                                                                     {L"build", std::format(L"{}.{}", target.build, target.revision)},
                                                                     {L"arch", target.architecture}});
    auto dialog = std::make_unique<ui::Dialog>(strings.get(Str::LanguagesAddTitle), body, ui::icons::Icon::Download,
                                               Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto* picker = &raw->setContent<Picker>(Picker::height(), strings, language, controller, std::move(languages));
    const Localization* text = &strings; // outlives every dialog

    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto update = [raw, picker, primary, text, language] {
        if (!*primary) {
            return;
        }
        const auto chosen = picker->chosen();
        std::uint64_t bytes = 0;
        for (const auto& f : picker->files()) {
            bytes += f.source.size;
        }
        (*primary)->setText(chosen.empty() ? text->get(Str::LanguagesAddNone)
                                           : text->format(Str::LanguagesAddDownload, {{L"n", std::to_wstring(chosen.size())},
                                                                                      {L"size", formatBytes(bytes, language)}}));
        (*primary)->setEnabled(!chosen.empty());
        raw->layout(); // the button's width follows its text
    };
    picker->onChange = update;
    raw->addButton(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(
        ui::ButtonKind::Primary, std::wstring(),
        [picker, close = actions.close, done = actions.download] {
            auto files = picker->files();
            if (files.empty()) {
                return;
            }
            const std::wstring ui = picker->uiLanguage();
            close();
            done(std::move(files), ui);
        },
        /*primary=*/true);
    raw->onCancel = actions.close;
    update();
    return LanguageAddDialog{std::move(dialog), picker->search()};
}

} // namespace wl::app
