#include "app/pages/registry/ValueDialog.h"

#include "app/controllers/RegistryController.h"
#include "core/image/RegistryInput.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/Label.h"
#include "ui/widgets/SearchBox.h"

#include <algorithm>
#include <iterator>

namespace wl::app {

using core::RegValueType;

namespace {

constexpr float kWidth = 560.0f;
constexpr float kLabelWidth = 112.0f;
constexpr float kFieldWidth = 400.0f;

struct Draft {
    std::wstring key;
    std::wstring name;
    RegValueType type = RegValueType::String;
    std::wstring data;
    bool afterSetup = true;
};

// The order of the type dropdown.
constexpr RegValueType kTypes[] = {RegValueType::String,  RegValueType::ExpandString, RegValueType::MultiString,
                                   RegValueType::Dword,   RegValueType::Qword,        RegValueType::Binary,
                                   RegValueType::DeleteValue, RegValueType::DeleteKey};

int typeIndex(RegValueType type) {
    for (int i = 0; i < static_cast<int>(std::size(kTypes)); ++i) {
        if (kTypes[i] == type) {
            return i;
        }
    }
    return 0;
}

Str formatHint(RegValueType type) {
    switch (type) {
    case RegValueType::MultiString: return Str::RegistryValueHintMulti;
    case RegValueType::Dword:
    case RegValueType::Qword: return Str::RegistryValueHintNumber;
    case RegValueType::Binary: return Str::RegistryValueHintBinary;
    case RegValueType::DeleteValue: return Str::RegistryValueHintDeleteValue;
    case RegValueType::DeleteKey: return Str::RegistryValueHintDeleteKey;
    default: return Str::RegistryValueHintText;
    }
}

bool hasData(RegValueType type) {
    return type != RegValueType::DeleteValue && type != RegValueType::DeleteKey;
}

} // namespace

ValueDialog makeValueDialog(const Localization& strings, const std::optional<core::RegistryWrite>& initial,
                            bool afterSetup, ValueDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(initial ? Str::RegistryEditValue : Str::RegistryAddValueTitle),
                                               s(Str::RegistryValueBody), std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto draft = std::make_shared<Draft>();
    draft->afterSetup = afterSetup;
    if (initial) {
        const auto input = core::registryWriteInput(*initial);
        draft->key = initial->key;
        draft->name = initial->name;
        draft->type = input.type;
        draft->data = input.data;
    }
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * 6, kLabelWidth);

    auto& key = form.addRow<ui::SearchBox>(s(Str::RegistryKey), std::wstring(), kFieldWidth, L"HKLM\\SOFTWARE\\…");
    key.setPlain(true);
    key.setText(draft->key);
    key.setAccessible(ui::AccessRole::Edit, s(Str::RegistryKey));
    auto& name = form.addRow<ui::SearchBox>(s(Str::RegistryValueName), std::wstring(), kFieldWidth,
                                            s(Str::RegistryValueNameDefault));
    name.setPlain(true);
    name.setText(draft->name);
    name.setAccessible(ui::AccessRole::Edit, s(Str::RegistryValueName));
    std::vector<std::wstring> types{L"REG_SZ", L"REG_EXPAND_SZ", L"REG_MULTI_SZ", L"REG_DWORD", L"REG_QWORD", L"REG_BINARY",
                                    s(Str::RegistryTypeDeleteValue), s(Str::RegistryTypeDeleteKey)};
    auto& type = form.addRow<ui::Dropdown>(s(Str::RegistryValueType), std::wstring(), kFieldWidth, std::wstring(),
                                           std::move(types), typeIndex(draft->type));
    auto& data = form.addRow<ui::SearchBox>(s(Str::RegistryValueData), std::wstring(), kFieldWidth, std::wstring());
    data.setPlain(true);
    data.setText(draft->data);
    data.setAccessible(ui::AccessRole::Edit, s(Str::RegistryValueData));
    auto& status = form.addRow<ui::Label>(std::wstring(), std::wstring(), kFieldWidth, std::wstring(),
                                          ui::tokens::TypeStyle::Caption, ui::tokens::Color::TextTertiary);
    auto& later = form.addRow<ui::CheckField>(std::wstring(), std::wstring(), 0.0f, s(Str::RegistryValueAfterSetup),
                                              draft->afterSetup);

    // Filled in below; the buttons and boxes refer to each other.
    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto build = [draft]() -> Result<core::RegistryWrite> {
        return core::registryWriteFromInput(draft->key, draft->name, draft->type, draft->data);
    };
    auto refresh = [draft, primary, build, &status, &data, &name, s] {
        data.setEnabled(hasData(draft->type));
        name.setEnabled(draft->type != RegValueType::DeleteKey);
        const auto write = build();
        bool ok = write.has_value();
        std::wstring text = s(formatHint(draft->type));
        auto color = ui::tokens::Color::TextTertiary;
        if (!draft->key.empty() && !ok) {
            text = s(draft->key.find(L'\\') == std::wstring::npos || core::normalizeRegistryKey(draft->key).empty()
                         ? Str::RegistryValueBadKey
                         : Str::RegistryValueBadData);
            color = ui::tokens::Color::StatusError;
        } else if (ok && !RegistryController::importable(*write)) {
            ok = false;
            text = s(Str::RegistryValueNoHive);
            color = ui::tokens::Color::StatusError;
        }
        status.setText(std::move(text));
        status.setColor(color);
        if (*primary) {
            (*primary)->setEnabled(ok);
        }
    };
    auto accept = [draft, build, close = actions.close, done = actions.accept] {
        auto write = build();
        if (!write || !RegistryController::importable(*write)) {
            return;
        }
        const bool after = draft->afterSetup;
        close();
        done(std::move(*write), after);
    };
    key.onChange = [draft, refresh](const std::wstring& v) {
        draft->key = v;
        refresh();
    };
    name.onChange = [draft, refresh](const std::wstring& v) {
        draft->name = v;
        refresh();
    };
    data.onChange = [draft, refresh](const std::wstring& v) {
        draft->data = v;
        refresh();
    };
    type.onChange = [draft, refresh](int index) {
        draft->type = kTypes[std::clamp(index, 0, static_cast<int>(std::size(kTypes)) - 1)];
        refresh();
    };
    later.onChange = [draft](bool on) { draft->afterSetup = on; };
    key.onSubmit = accept;
    name.onSubmit = accept;
    data.onSubmit = accept;

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, s(initial ? Str::CommonSave : Str::CommonAdd), accept, /*primary=*/true);
    raw->onCancel = actions.close;
    refresh();
    return ValueDialog{std::move(dialog), &key};
}

} // namespace wl::app
