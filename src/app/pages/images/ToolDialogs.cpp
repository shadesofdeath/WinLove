#include "app/pages/images/ToolDialogs.h"

#include "core/system/Hash.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/SearchBox.h"

#include <format>

namespace wl::app {

namespace {

constexpr float kWidth = 520.0f;
constexpr float kLabelWidth = 120.0f;
constexpr float kFieldWidth = 352.0f;

bool blank(const std::wstring& text) {
    return text.find_first_not_of(L' ') == std::wstring::npos;
}

// A menu item's "…" does not belong in the dialog it opens.
std::wstring titled(std::wstring text) {
    while (!text.empty() && (text.back() == L'…' || text.back() == L'.')) {
        text.pop_back();
    }
    return text;
}

} // namespace

ToolDialog makeChoiceDialog(const Localization& strings, std::wstring title, std::wstring note, std::wstring label,
                            std::vector<std::wstring> options, int selected, std::wstring acceptLabel,
                            std::function<void(int)> accept, std::function<void()> close) {
    auto dialog = std::make_unique<ui::Dialog>(titled(std::move(title)), std::move(note), std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow, kLabelWidth);
    auto& choice = form.addRow<ui::Dropdown>(label, std::wstring(), kFieldWidth, std::wstring(), std::move(options), selected);
    choice.setAccessible(ui::AccessRole::Group, label);
    raw->addButton(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), close);
    raw->addButton(ui::ButtonKind::Primary, std::move(acceptLabel),
                   [&choice, accept = std::move(accept), close] {
                       const int picked = choice.selected();
                       close();
                       accept(picked);
                   },
                   /*primary=*/true);
    raw->onCancel = close;
    return ToolDialog{std::move(dialog), &choice};
}

ToolDialog makeTextDialog(const Localization& strings, std::wstring title, std::wstring note, std::wstring label, std::wstring text,
                          std::wstring acceptLabel, std::function<void(std::wstring)> accept, std::function<void()> close) {
    auto dialog = std::make_unique<ui::Dialog>(titled(std::move(title)), std::move(note), std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow, kLabelWidth);
    auto& box = form.addRow<ui::SearchBox>(label, std::wstring(), kFieldWidth, std::wstring());
    box.setPlain(true);
    box.setText(text);
    box.setAccessible(ui::AccessRole::Edit, label);
    auto value = std::make_shared<std::wstring>(std::move(text));
    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto go = [value, accept = std::move(accept), close] {
        if (blank(*value)) {
            return;
        }
        const std::wstring result = *value;
        close();
        accept(result);
    };
    box.onChange = [value, primary](const std::wstring& t) {
        *value = t;
        if (*primary) {
            (*primary)->setEnabled(!blank(t));
        }
    };
    box.onSubmit = go;
    raw->addButton(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, std::move(acceptLabel), go, /*primary=*/true);
    (*primary)->setEnabled(!blank(*value));
    raw->onCancel = close;
    return ToolDialog{std::move(dialog), &box};
}

ToolDialog makeEditionsDialog(const Localization& strings, std::wstring title, std::wstring note,
                              const std::vector<core::ImageInfo>& editions, std::wstring acceptLabel,
                              std::function<void(std::vector<int>)> accept, std::function<void()> close) {
    auto dialog = std::make_unique<ui::Dialog>(titled(std::move(title)), std::move(note), std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    const float rows = static_cast<float>(std::min<std::size_t>(editions.size(), 8));
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * std::max(rows, 1.0f), 48.0f);
    auto ticked = std::make_shared<std::vector<int>>();
    auto primary = std::make_shared<ui::Button*>(nullptr);
    ui::Widget* first = nullptr;
    for (const auto& image : editions) {
        ticked->push_back(image.index);
        const std::wstring name = image.displayName.empty() ? image.name : image.displayName;
        auto& check = form.addRow<ui::CheckField>(std::to_wstring(image.index), std::wstring(), 0.0f,
                                                  std::format(L"{} · {}", name, core::architectureName(image.architecture)), true);
        check.setAccessible(ui::AccessRole::CheckBox, name);
        const int index = image.index;
        check.onChange = [ticked, primary, index](bool on) {
            std::erase(*ticked, index);
            if (on) {
                ticked->push_back(index);
                std::ranges::sort(*ticked);
            }
            if (*primary) {
                (*primary)->setEnabled(!ticked->empty());
            }
        };
        if (!first) {
            first = &check;
        }
    }
    raw->addButton(ui::ButtonKind::Secondary, strings.get(Str::CommonCancel), close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, std::move(acceptLabel),
                               [ticked, accept = std::move(accept), close] {
                                   if (ticked->empty()) {
                                       return;
                                   }
                                   const std::vector<int> result = *ticked;
                                   close();
                                   accept(result);
                               },
                               /*primary=*/true);
    raw->onCancel = close;
    return ToolDialog{std::move(dialog), first};
}

ToolDialog makeCaptureDialog(const Localization& strings, std::wstring note, std::wstring name,
                             std::function<void(CaptureAnswers)> accept, std::function<void()> close) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::SourceCaptureTitle), std::move(note), std::nullopt,
                                               ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * 3, kLabelWidth);
    auto answers = std::make_shared<CaptureAnswers>(CaptureAnswers{std::move(name), std::wstring(), 0});
    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto go = [answers, accept = std::move(accept), close] {
        if (blank(answers->name)) {
            return;
        }
        const CaptureAnswers result = *answers;
        close();
        accept(result);
    };
    auto& nameBox = form.addRow<ui::SearchBox>(s(Str::CommonName), std::wstring(), kFieldWidth, std::wstring());
    nameBox.setPlain(true);
    nameBox.setText(answers->name);
    nameBox.onChange = [answers, primary](const std::wstring& t) {
        answers->name = t;
        if (*primary) {
            (*primary)->setEnabled(!blank(t));
        }
    };
    nameBox.onSubmit = go;
    auto& descBox = form.addRow<ui::SearchBox>(s(Str::DialogsRenameDescription), std::wstring(), kFieldWidth, std::wstring());
    descBox.setPlain(true);
    descBox.onChange = [answers](const std::wstring& t) { answers->description = t; };
    descBox.onSubmit = go;
    auto& compression = form.addRow<ui::Dropdown>(s(Str::ImagesCompression), std::wstring(), kFieldWidth, std::wstring(),
                                                  std::vector<std::wstring>{s(Str::ImagesCompressLzx), s(Str::ImagesCompressXpress)}, 0);
    compression.onChange = [answers](int i) { answers->compression = i; };
    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, s(Str::SourceCapture), go, /*primary=*/true);
    (*primary)->setEnabled(!blank(answers->name));
    raw->onCancel = close;
    return ToolDialog{std::move(dialog), &nameBox};
}

HashDialog makeHashDialog(const Localization& strings, std::wstring file, std::function<void(std::wstring)> copy,
                          std::function<void()> close) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::SourceHashTitle), std::move(file), std::nullopt,
                                               ui::tokens::Color::TextSecondary, kWidth + 80);
    ui::Dialog* raw = dialog.get();
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * 3, kLabelWidth);
    auto& value = form.addRow<ui::Label>(L"SHA-256", std::wstring(), kFieldWidth + 80, s(Str::SourceHashComputing),
                                         ui::tokens::TypeStyle::Mono, ui::tokens::Color::TextSecondary);
    auto& expected = form.addRow<ui::SearchBox>(s(Str::SourceHashExpected), std::wstring(), kFieldWidth + 80, std::wstring());
    expected.setPlain(true);
    expected.setPlaceholder(s(Str::SourceHashPaste));
    auto& verdict = form.addRow<ui::Label>(std::wstring(), std::wstring(), kFieldWidth + 80, std::wstring(),
                                           ui::tokens::TypeStyle::BodyStrong, ui::tokens::Color::TextSecondary);
    auto state = std::make_shared<std::pair<std::wstring, std::wstring>>(); // computed, pasted
    auto judge = [state, &verdict, s] {
        if (state->second.empty()) {
            verdict.setText(std::wstring());
            return;
        }
        const std::wstring want = core::normalizeSha256(state->second);
        if (want.empty()) {
            verdict.setText(s(Str::SourceHashNotAHash));
            verdict.setColor(ui::tokens::Color::StatusWarning);
        } else if (state->first.empty()) {
            verdict.setText(s(Str::SourceHashComputing));
            verdict.setColor(ui::tokens::Color::TextSecondary);
        } else if (want == state->first) {
            verdict.setText(s(Str::SourceHashMatch));
            verdict.setColor(ui::tokens::Color::StatusSuccess);
        } else {
            verdict.setText(s(Str::SourceHashMismatch));
            verdict.setColor(ui::tokens::Color::StatusError);
        }
    };
    expected.onChange = [state, judge](const std::wstring& t) {
        state->second = t;
        judge();
    };
    auto& copyButton = raw->addButton(ui::ButtonKind::Secondary, s(Str::SourceHashCopy), [state, copy] {
        if (!state->first.empty() && copy) {
            copy(state->first);
        }
    });
    copyButton.setEnabled(false);
    raw->addButton(ui::ButtonKind::Primary, s(Str::CommonClose), close, /*primary=*/true);
    raw->onCancel = close;
    HashDialog out{std::move(dialog), &expected, {}, {}, {}};
    out.progress = [&value, &strings](double f) {
        value.setText(strings.format(Str::SourceHashProgress, {{L"p", std::to_wstring(static_cast<int>(f * 100))}}));
    };
    out.done = [&value, &copyButton, state, judge](std::wstring hash) {
        state->first = hash;
        value.setText(hash);
        value.setColor(ui::tokens::Color::TextPrimary);
        copyButton.setEnabled(true);
        judge();
    };
    out.failed = [&value](std::wstring error) {
        value.setText(error);
        value.setColor(ui::tokens::Color::StatusError);
    };
    return out;
}

} // namespace wl::app
