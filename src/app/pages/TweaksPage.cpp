#include "app/pages/TweaksPage.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/ScrollBar.h"
#include "ui/widgets/Toggle.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kTabsTop = 11.0f;
constexpr float kTabsHeight = ui::tokens::size::control + 1;
constexpr float kFormGap = 11.0f;     // tab line → first section
constexpr float kSection = 32.0f;     // caps title + rule
constexpr float kRow = 32.0f;
constexpr float kLabelWidth = 240.0f; // screens.md: "240px etiket sütunu"
constexpr float kDropdownWidth = 280.0f;
constexpr float kHintGap = 8.0f;
constexpr float kWheelStep = kRow;

// Tells the form when the control takes the keyboard focus, so it can scroll it into view.
template <class W>
class Revealing : public W {
public:
    using W::W;
    std::function<void()> onFocused;
    void onFocusChanged(bool focused) override {
        W::onFocusChanged(focused);
        if (focused && onFocused) {
            onFocused();
        }
    }
};

} // namespace

// The scrolling form of one tab. Controls are children; titles, labels and hints are painted.
class SettingsForm : public ui::Widget {
public:
    SettingsForm(ImageSettingsController& controller, Language language) : m_controller(controller), m_language(language) {}

    // Rebuilds the controls. Never called from one of its own controls (they would be destroyed
    // inside their click): only the tab bar and the page do.
    void showTab(const std::string& tab) {
        clearChildren();
        m_rows.clear();
        m_offset = 0;
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
                    m_rows.push_back(Row{&section, nullptr, nullptr, nullptr, nullptr});
                    titled = true;
                }
                m_rows.push_back(makeRow(setting));
            }
        }
        m_scroll = &add<ui::ScrollBar>();
        m_scroll->onScroll = [this](float offset) {
            m_offset = offset;
            layout();
            invalidate();
        };
        sync();
        layout();
        invalidate();
    }

    // Control positions from the queue (after any queue change, and after a refused edit).
    void sync() {
        for (const auto& row : m_rows) {
            if (!row.setting) {
                continue;
            }
            const int current = m_controller.current(*row.setting);
            if (row.toggle) {
                if (row.toggle->isOn() != (current == 1)) {
                    row.toggle->setOn(current == 1);
                }
            } else if (row.dropdown) {
                row.dropdown->setSelected(current);
            } else if (row.radio) {
                row.radio->setSelected(current);
            }
        }
    }

    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }

    void layout() override {
        const RectF b = bounds();
        float content = 0;
        for (const auto& row : m_rows) {
            content += row.setting ? kRow : kSection;
        }
        m_offset = std::clamp(m_offset, 0.0f, std::max(content - b.height, 0.0f));
        float y = b.y - m_offset;
        const float x = b.x + kLabelWidth;
        for (const auto& row : m_rows) {
            if (!row.setting) {
                y += kSection;
                continue;
            }
            const float top = y + (kRow - ui::tokens::size::control) / 2;
            if (row.toggle) {
                row.toggle->setBounds({x, top, ui::tokens::size::toggleW, ui::tokens::size::control});
            } else if (row.dropdown) {
                row.dropdown->setBounds({x, top, kDropdownWidth, ui::tokens::size::control});
            } else if (row.radio) {
                row.radio->setBounds({x, top, row.radio->measure({}).width, ui::tokens::size::control});
            }
            y += kRow;
        }
        if (m_scroll) {
            m_scroll->setBounds({b.right() - ui::ScrollBar::kWidth, b.y, ui::ScrollBar::kWidth, b.height});
            m_scroll->setRange(content, b.height);
            m_scroll->setOffset(m_offset);
            m_scroll->setVisible(m_scroll->needed());
        }
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.pushClip(b);
        float y = b.y - m_offset;
        for (const auto& row : m_rows) {
            if (!row.setting) {
                canvas.drawText(row.section->title.get(m_language), {b.x, y + 10, b.width, 20}, TypeStyle::Section,
                                Color::TextSecondary);
                canvas.hairlineH(b.x, y + kSection - 1, b.width, Color::LineSubtle);
                y += kSection;
                continue;
            }
            canvas.drawText(row.setting->label.get(m_language), {b.x, y, kLabelWidth - 8, kRow}, TypeStyle::Body,
                            Color::TextSecondary);
            const std::wstring& hint = row.setting->hint.get(m_language);
            if (row.toggle && !hint.empty()) {
                const float hx = b.x + kLabelWidth + ui::tokens::size::toggleW + kHintGap;
                canvas.drawText(hint, {hx, y, std::max(b.right() - hx, 0.0f), kRow}, TypeStyle::Caption,
                                Color::TextTertiary);
            }
            y += kRow;
        }
        canvas.popClip();
    }

    bool onWheel(ui::PointF /*p*/, float lines) override {
        if (!m_scroll || !m_scroll->needed()) {
            return false;
        }
        m_offset -= lines * kWheelStep;
        layout();
        invalidate();
        return true;
    }

private:
    struct Row {
        const ImageSettingSection* section; // a section title row when `setting` is null
        const ImageSetting* setting;
        ui::Toggle* toggle;
        ui::Dropdown* dropdown;
        ui::RadioGroup* radio;
    };

    Row makeRow(const ImageSetting& setting) {
        Row row{nullptr, &setting, nullptr, nullptr, nullptr};
        const ImageSetting* s = &setting; // catalog entries outlive the form
        auto choose = [this, s](int option) {
            m_controller.select(*s, option);
            sync(); // a refused edit (queue locked while applying) puts the control back
        };
        ui::Widget* control = nullptr;
        if (setting.control == ImageSetting::Control::Toggle) {
            auto& toggle = add<Revealing<ui::Toggle>>(std::wstring(), false);
            toggle.onChange = [choose](bool on) { choose(on ? 1 : 0); };
            toggle.onFocused = [this, &toggle] { reveal(toggle); };
            row.toggle = &toggle;
            control = &toggle;
        } else {
            std::vector<std::wstring> items;
            for (const auto& option : setting.options) {
                items.push_back(option.label.get(m_language));
            }
            if (setting.control == ImageSetting::Control::Dropdown) {
                auto& dropdown = add<Revealing<ui::Dropdown>>(std::wstring(), std::move(items), 0);
                dropdown.onChange = choose;
                dropdown.onFocused = [this, &dropdown] { reveal(dropdown); };
                row.dropdown = &dropdown;
                control = &dropdown;
            } else {
                auto& radio = add<Revealing<ui::RadioGroup>>(std::move(items), 0);
                radio.onChange = choose;
                radio.onFocused = [this, &radio] { reveal(radio); };
                row.radio = &radio;
                control = &radio;
            }
        }
        control->setAccessible(row.toggle ? ui::AccessRole::CheckBox : ui::AccessRole::Group, setting.label.get(m_language));
        return row;
    }

    // Keyboard focus moved to a control outside the visible part: scroll its row in.
    void reveal(const ui::Widget& control) {
        const RectF b = bounds();
        const RectF r = control.bounds();
        const float pad = (kRow - ui::tokens::size::control) / 2;
        if (r.y - pad < b.y) {
            m_offset -= b.y - (r.y - pad);
        } else if (r.bottom() + pad > b.bottom()) {
            m_offset += r.bottom() + pad - b.bottom();
        } else {
            return;
        }
        layout();
        invalidate();
    }

    ImageSettingsController& m_controller;
    Language m_language;
    std::vector<Row> m_rows;
    ui::ScrollBar* m_scroll = nullptr;
    float m_offset = 0;
};

TweaksPage::TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings,
                       Language language, std::function<void()> goImages)
    : m_state(state), m_controller(controller), m_strings(strings) {
    std::vector<std::wstring> tabs;
    for (const auto& tab : m_controller.catalog().tabs()) {
        tabs.push_back(tab.title.get(language));
    }
    m_tabs = &add<ui::TabBar>(std::move(tabs), 0);
    m_form = &add<SettingsForm>(m_controller, language);
    m_tabs->onChange = [this](int index) {
        m_form->showTab(m_controller.catalog().tabs()[static_cast<std::size_t>(index)].id);
    };
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::TweaksSliders, strings.get(Str::TweaksNoMountTitle),
                                   strings.get(Str::TweaksNoMountBody));
    m_empty->setAction(strings.get(Str::FeaturesGoImages)).onInvoke = std::move(goImages);
    setAccessible(ui::AccessRole::Group, strings.get(Str::TweaksTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            refresh();
        } else if (change == AppState::Change::Queue) {
            m_form->sync();
        }
    });
    if (!m_controller.catalog().tabs().empty()) {
        m_form->showTab(m_controller.catalog().tabs().front().id);
    }
    refresh();
}

TweaksPage::~TweaksPage() {
    m_state.unsubscribe(m_subscription);
}

void TweaksPage::refresh() {
    const bool mounted = m_state.mounted().has_value();
    m_empty->setVisible(!mounted);
    m_tabs->setVisible(mounted);
    m_form->setVisible(mounted);
    m_form->sync();
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
