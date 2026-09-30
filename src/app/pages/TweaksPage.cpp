#include "app/pages/TweaksPage.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;

namespace {
constexpr float kTabsTop = 11.0f;
constexpr float kTabsHeight = ui::tokens::size::control + 1;
constexpr float kFormGap = 11.0f;     // tab line → first section
constexpr float kLabelWidth = 240.0f; // screens.md: "240px etiket sütunu"
constexpr float kDropdownWidth = 280.0f;
} // namespace

TweaksPage::TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings,
                       Language language, std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_language(language) {
    std::vector<std::wstring> tabs;
    for (const auto& tab : m_controller.catalog().tabs()) {
        tabs.push_back(tab.title.get(language));
    }
    m_tabs = &add<ui::TabBar>(std::move(tabs), 0);
    m_form = &add<ui::FormView>(kLabelWidth);
    m_tabs->onChange = [this](int index) { showTab(m_controller.catalog().tabs()[static_cast<std::size_t>(index)].id); };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::TweaksSliders, strings.get(Str::TweaksNoMountTitle),
                                   strings.get(Str::TweaksNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::TweaksTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue) {
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

void TweaksPage::addSetting(const ImageSetting& setting) {
    Binding binding{&setting, nullptr, nullptr, nullptr};
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
    for (const auto& b : m_bindings) {
        const int current = m_controller.current(*b.setting);
        if (b.toggle) {
            if (b.toggle->isOn() != (current == 1)) {
                b.toggle->setOn(current == 1);
            }
        } else if (b.dropdown) {
            b.dropdown->setSelected(current);
        } else if (b.radio) {
            b.radio->setSelected(current);
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
