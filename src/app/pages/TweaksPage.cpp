#include "app/pages/TweaksPage.h"

#include "ui/widget/Host.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;

namespace {
constexpr float kTabsTop = 11.0f;
constexpr float kTabsHeight = ui::tokens::size::control + 1;
constexpr float kFormGap = 11.0f;     // tab line → first section
constexpr float kLabelWidth = 240.0f; // screens.md: "240px etiket sütunu"
constexpr float kDropdownWidth = 280.0f;
constexpr float kBrowseWidth = 28.0f;
constexpr float kBrowseGap = 4.0f;
} // namespace

PictureField::PictureField(std::wstring browseTooltip) {
    m_box = &add<ui::SearchBox>(L"");
    m_box->setPlain(true);
    m_box->onChange = [this](const std::wstring& value) {
        if (onChange) {
            onChange(value);
        }
    };
    m_browse = &add<ui::Button>(ui::ButtonKind::Secondary, L"", ui::icons::Icon::OpenFolder);
    m_browse->setTooltip(std::move(browseTooltip));
    m_browse->onInvoke = [this] {
        if (onBrowse) {
            onBrowse();
        }
    };
    setFocusable(false); // the box and the button take the focus, not their frame
}

ui::SizeF PictureField::measure(ui::SizeF) {
    return {kDropdownWidth, ui::tokens::size::control};
}

void PictureField::layout() {
    const RectF b = bounds();
    m_box->setBounds({b.x, b.y, std::max(b.width - kBrowseWidth - kBrowseGap, 0.0f), b.height});
    m_browse->setBounds({b.right() - kBrowseWidth, b.y, kBrowseWidth, b.height});
}

TweaksPage::TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings,
                       Language language, std::function<void()> goImages, PickImage pickImage)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language), m_pickImage(std::move(pickImage)) {
    std::vector<std::wstring> tabs;
    for (const auto& tab : m_controller.catalog().tabs()) {
        tabs.push_back(tab.title.get(language));
    }
    m_tabs = &add<ui::TabBar>(std::move(tabs), 0);
    m_form = &add<ui::FormView>(kLabelWidth);
    m_tabs->onChange = [this](int index) { showTab(m_controller.catalog().tabs()[static_cast<std::size_t>(index)].id); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::TweaksSliders, strings.get(Str::TweaksNoMountTitle),
                                   strings.get(Str::TweaksNoMountBody));
    m_empty->setAction(strings.get(Str::CommonGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::TweaksTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue || change == AppState::Change::ImageValues ||
                   change == AppState::Change::Services) {
            sync();
        }
    });
    if (!m_controller.catalog().tabs().empty()) {
        showTab(m_controller.catalog().tabs().front().id);
    }
    refresh();
}

TweaksPage::~TweaksPage() {
    m_state.unsubscribe(m_subscription);
}

void TweaksPage::showTab(const std::string& tab) {
    // Rebuilds the controls: only the tab bar and the constructor call this, never a control.
    m_form->clear();
    m_bindings.clear();
    const auto& catalog = m_controller.catalog();
    for (const auto& section : catalog.sections()) {
        if (section.tab != tab) {
            continue;
        }
        bool titled = false;
        for (const auto& setting : catalog.settings()) {
            if (setting.section != section.id) {
                continue;
            }
            if (!titled) {
                m_form->addSection(section.title.get(m_language));
                titled = true;
            }
            addSetting(setting);
        }
    }
    sync();
}

void TweaksPage::reveal(const std::string& settingId) {
    const auto& catalog = m_controller.catalog();
    const auto setting = std::ranges::find(catalog.settings(), settingId, &ImageSetting::id);
    if (setting == catalog.settings().end()) {
        return;
    }
    const auto section = std::ranges::find(catalog.sections(), setting->section, &ImageSettingSection::id);
    if (section == catalog.sections().end()) {
        return;
    }
    for (std::size_t i = 0; i < catalog.tabs().size(); ++i) {
        if (catalog.tabs()[i].id == section->tab) {
            m_tabs->setSelected(static_cast<int>(i));
            showTab(section->tab);
        }
    }
    layout(); // the new rows need their rectangles before one of them can be scrolled to
    for (const auto& b : m_bindings) {
        if (b.setting == &*setting && host()) {
            ui::Widget* control = b.toggle     ? static_cast<ui::Widget*>(b.toggle)
                                  : b.dropdown ? static_cast<ui::Widget*>(b.dropdown)
                                  : b.text     ? static_cast<ui::Widget*>(b.text)
                                  : b.file     ? static_cast<ui::Widget*>(&b.file->box())
                                               : static_cast<ui::Widget*>(b.radio);
            host()->setFocus(control, /*visible=*/true);
        }
    }
}

void TweaksPage::valueTyped(std::size_t binding, const std::wstring& value) {
    if (binding >= m_bindings.size()) {
        return;
    }
    // The queue change comes back as sync(): it must not rewrite the box under the caret.
    m_typing = binding;
    const bool accepted = m_controller.setValue(*m_bindings[binding].setting, value);
    m_typing = static_cast<std::size_t>(-1);
    m_bindings[binding].shown = m_controller.value(*m_bindings[binding].setting);
    m_bindings[binding].problem = !accepted;
    sync();
}

void TweaksPage::addSetting(const ImageSetting& setting) {
    Binding binding;
    binding.setting = &setting;
    const std::wstring& name = setting.label.get(m_language);
    if (ImageSettingsController::takesValue(setting)) {
        const std::size_t index = m_bindings.size();
        if (setting.control == ImageSetting::Control::Text) {
            binding.text = &m_form->addRow<ui::SearchBox>(name, std::wstring(), kDropdownWidth, std::wstring());
            binding.text->setPlain(true);
            binding.text->onChange = [this, index](const std::wstring& value) { valueTyped(index, value); };
            binding.text->setAccessible(ui::AccessRole::Edit, name);
        } else {
            binding.file = &m_form->addRow<PictureField>(name, std::wstring(), kDropdownWidth, m_strings.get(Str::TweaksPickImage));
            binding.file->onChange = [this, index](const std::wstring& value) { valueTyped(index, value); };
            binding.file->onBrowse = [this, index] {
                if (!m_pickImage || index >= m_bindings.size()) {
                    return;
                }
                if (const auto picked = m_pickImage()) {
                    m_bindings[index].file->setText(picked->wstring());
                    valueTyped(index, picked->wstring());
                }
            };
            binding.file->box().setAccessible(ui::AccessRole::Edit, name);
        }
        m_bindings.push_back(binding);
        return;
    }
    const ImageSetting* s = &setting; // catalog entries outlive the page
    auto choose = [this, s](int option) {
        m_controller.select(*s, option);
        sync(); // a refused edit (queue locked while applying) puts the control back
    };
    const std::wstring& label = setting.label.get(m_language);
    ui::Widget* control = nullptr;
    if (setting.control == ImageSetting::Control::Toggle) {
        binding.toggle = &m_form->addRow<ui::Toggle>(label, setting.hint.get(m_language), ui::tokens::size::toggleW,
                                                     std::wstring(), false);
        binding.toggle->onChange = [choose](bool on) { choose(on ? 1 : 0); };
        control = binding.toggle;
    } else {
        std::vector<std::wstring> items;
        for (const auto& option : setting.options) {
            items.push_back(option.label.get(m_language));
        }
        if (setting.control == ImageSetting::Control::Dropdown) {
            binding.dropdown = &m_form->addRow<ui::Dropdown>(label, std::wstring(), kDropdownWidth, std::wstring(),
                                                             std::move(items), 0);
            binding.dropdown->onChange = choose;
            control = binding.dropdown;
        } else {
            binding.radio = &m_form->addRow<ui::RadioGroup>(label, std::wstring(), 0.0f, std::move(items), 0);
            binding.radio->onChange = choose;
            control = binding.radio;
        }
    }
    control->setAccessible(binding.toggle ? ui::AccessRole::CheckBox : ui::AccessRole::Group, label);
    m_bindings.push_back(binding);
}

void TweaksPage::sync() {
    // Next to every control: what its position means for the installed Windows, and whether that
    // is what Windows does on its own. A bare switch beside "Reklam kimliği" does not say whether
    // "on" keeps the feature or applies a tweak against it.
    const std::wstring separator = L" \u00b7 ";
    for (std::size_t i = 0; i < m_bindings.size(); ++i) {
        Binding& b = m_bindings[i];
        const int current = m_controller.current(*b.setting);
        const int image = m_controller.imageOption(*b.setting); // D-045
        const bool changed = current != image;
        std::wstring mark = m_strings.get(changed                                ? Str::TweaksChanged
                                          : image != b.setting->defaultOption ? Str::TweaksInImage
                                                                               : Str::TweaksIsDefault);
        if (!changed && image != b.setting->defaultOption && m_controller.revertOperations(*b.setting).empty()) {
            mark += separator + m_strings.get(Str::TweaksNoRevert);
        }
        const auto color = changed ? ui::tokens::Color::AccentBase : ui::tokens::Color::TextTertiary;
        if (b.text) {
            // What the image has, shown until something is typed over it.
            const std::wstring inImage = m_controller.imageValue(*b.setting);
            b.text->setPlaceholder(inImage.empty() ? std::wstring() : m_strings.get(Str::TweaksInImage) + L": " + inImage);
        }
        if (b.text || b.file) {
            // The box follows the queue (a preset, undo) unless it is the one being typed into.
            const std::wstring value = m_controller.value(*b.setting);
            if (i != m_typing && value != b.shown) {
                b.shown = value;
                b.problem = false;
                if (b.text) {
                    b.text->setText(value);
                } else {
                    b.file->setText(value);
                }
            }
            const ui::Widget& control = b.text ? static_cast<const ui::Widget&>(*b.text) : *b.file;
            if (b.problem) {
                m_form->setHint(control, m_strings.get(Str::TweaksFileProblem), ui::tokens::Color::StatusError);
            } else {
                std::wstring hint = mark;
                if (const std::wstring& note = b.setting->hint.get(m_language); !note.empty()) {
                    hint += separator + note;
                }
                m_form->setHint(control, std::move(hint), color);
            }
            continue;
        }
        if (b.toggle) {
            if (b.toggle->isOn() != (current == 1)) {
                b.toggle->setOn(current == 1);
            }
            std::wstring hint = m_strings.get(current == 1 ? Str::TweaksStateOn : Str::TweaksStateOff) + separator + mark;
            if (const std::wstring& note = b.setting->hint.get(m_language); !note.empty()) {
                hint += separator + note;
            }
            m_form->setHint(*b.toggle, std::move(hint), color);
        } else if (b.dropdown) {
            b.dropdown->setSelected(current);
            m_form->setHint(*b.dropdown, mark, color);
        } else if (b.radio) {
            b.radio->setSelected(current);
            m_form->setHint(*b.radio, mark, color);
        }
    }
}

void TweaksPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_form->setVisible(mounted);
    sync();
    layout();
    invalidate();
}

void TweaksPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    m_tabs->setBounds({b.x, b.y + kTabsTop, b.width, kTabsHeight});
    const float top = b.y + kTabsTop + kTabsHeight + kFormGap;
    m_form->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
}

} // namespace wl::app
