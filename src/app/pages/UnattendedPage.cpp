#include "app/pages/UnattendedPage.h"

#include "base/Text.h"

#include "ui/widget/Host.h"
#include "ui/widgets/ScrollBar.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Problem = core::UnattendProblem;

namespace {

constexpr std::size_t kCommandSlots = 3; // per list: specialize / first logon

// "ali=parola, ayse" <-> the further accounts; the administrator flag is one for all of them.
std::wstring extraAccountsText(const std::vector<core::UnattendAccount>& accounts) {
    std::wstring text;
    for (const auto& a : accounts) {
        if (!text.empty()) {
            text += L", ";
        }
        text += a.password.empty() ? a.name : a.name + L"=" + a.password;
    }
    return text;
}

std::vector<core::UnattendAccount> parseExtraAccounts(std::wstring_view text, bool administrators) {
    std::vector<core::UnattendAccount> accounts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(L',', start);
        const std::wstring_view item = wl::text::trim(text.substr(start, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - start));
        if (!item.empty()) {
            const std::size_t eq = item.find(L'=');
            accounts.push_back({std::wstring(wl::text::trim(item.substr(0, eq))),
                                eq == std::wstring_view::npos ? std::wstring() : std::wstring(item.substr(eq + 1)), administrators});
        }
        if (comma == std::wstring_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return accounts;
}

// One numbered command field: the list grows to it, and empty trailing slots are dropped.
void setSlot(std::vector<std::wstring>& list, std::size_t index, const std::wstring& value) {
    if (list.size() <= index) {
        list.resize(index + 1);
    }
    list[index] = value;
    while (!list.empty() && list.back().empty()) {
        list.pop_back();
    }
}

constexpr float kTop = 11.0f;          // page top → step bar / preview panel
constexpr float kStepsHeight = 24.0f;
constexpr float kFormGap = 8.0f;       // step bar → first section
constexpr float kLabelWidth = 220.0f;  // screen 11
constexpr float kFieldWidth = 220.0f;
constexpr float kPickerWidth = 280.0f;
constexpr float kPanelGap = 16.0f;
constexpr float kPanelBottom = 16.0f;
constexpr float kPreviewShare = 0.4435f; // of the width left after the gap (screen 11: 528 of 1191)

// Lines of `now` that are not part of the longest common subsequence with `before`.
std::vector<bool> changedLines(const std::vector<std::wstring>& before, const std::vector<std::wstring>& now) {
    const std::size_t n = before.size();
    const std::size_t m = now.size();
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            lcs[i][j] = before[i] == now[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }
    std::vector<bool> changed(m, true);
    for (std::size_t i = 0, j = 0; i < n && j < m;) {
        if (before[i] == now[j]) {
            changed[j] = false;
            ++i;
            ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            ++i;
        } else {
            ++j;
        }
    }
    return changed;
}

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find(L'\n', start);
        lines.push_back(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

} // namespace

// Step indicator (screen 11): 6px square + caption per step; before the current one = done
// (success), current = accent + primary text, after = line.strong + tertiary. Click or ←/→.
class StepBar : public ui::Widget {
public:
    explicit StepBar(std::vector<std::wstring> steps) : m_steps(std::move(steps)) {
        setFocusable(true);
        setAccessible(ui::AccessRole::List, L"");
    }
    std::function<void(int)> onSelect;

    void setCurrent(int index) {
        if (index != m_current) {
            m_current = index;
            invalidate();
        }
    }
    [[nodiscard]] ui::Cursor cursor() const override { return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow; }
    [[nodiscard]] RectF focusRect() const override {
        const auto r = rects();
        return m_current >= 0 && m_current < static_cast<int>(r.size()) ? r[static_cast<std::size_t>(m_current)] : bounds();
    }
    void onPointerMove(ui::PointF p) override {
        const int hit = stepAt(p);
        if (hit != m_hover) {
            m_hover = hit;
            invalidate();
        }
    }
    void onHoverChanged(bool hovered) override {
        if (!hovered) {
            m_hover = -1;
        }
        invalidate();
    }
    void onPointerDown(ui::PointF p) override {
        if (const int hit = stepAt(p); hit >= 0 && onSelect) {
            onSelect(hit);
        }
    }
    bool onKeyDown(const ui::KeyEvent& key) override {
        if (key.virtualKey != VK_LEFT && key.virtualKey != VK_RIGHT) {
            return false;
        }
        const int next = std::clamp(m_current + (key.virtualKey == VK_RIGHT ? 1 : -1), 0, static_cast<int>(m_steps.size()) - 1);
        if (next != m_current && onSelect) {
            onSelect(next);
        }
        return true;
    }
    void paint(ui::Canvas& canvas) override {
        const auto r = rects();
        for (int i = 0; i < static_cast<int>(r.size()); ++i) {
            const RectF step = r[static_cast<std::size_t>(i)];
            const bool current = i == m_current;
            const Color mark = current ? Color::AccentBase : i < m_current ? Color::StatusSuccess : Color::LineStrong;
            const Color ink = current || i == m_hover ? Color::TextPrimary : i < m_current ? Color::TextSecondary
                                                                                           : Color::TextTertiary;
            canvas.fillRoundRect({step.x, step.y + (step.height - kMark) / 2, kMark, kMark}, 1.0f, mark);
            canvas.drawText(m_steps[static_cast<std::size_t>(i)], {step.x + kMark + kMarkGap, step.y, step.width, step.height},
                            TypeStyle::Caption, ink);
        }
    }

private:
    static constexpr float kMark = 6.0f;
    static constexpr float kMarkGap = 6.0f;
    static constexpr float kStepGap = 24.0f;

    [[nodiscard]] std::vector<RectF> rects() const {
        std::vector<RectF> result;
        const RectF b = bounds();
        float x = b.x;
        for (const auto& step : m_steps) {
            const float text = host() ? std::ceil(host()->text().measure(step, TypeStyle::Caption)) : 60.0f;
            const float width = kMark + kMarkGap + text;
            result.push_back({x, b.y, width, b.height});
            x += width + kStepGap;
        }
        return result;
    }
    [[nodiscard]] int stepAt(ui::PointF p) const {
        const auto r = rects();
        for (int i = 0; i < static_cast<int>(r.size()); ++i) {
            if (r[static_cast<std::size_t>(i)].contains(p)) {
                return i;
            }
        }
        return -1;
    }

    std::vector<std::wstring> m_steps;
    int m_current = 0;
    int m_hover = -1;
};

// The answer file as text (screen 11): bg.input panel, caption title, numbered mono lines at a
// 16px pitch; lines that changed with the last edit in accent. Long lines wrap (no number on the
// continuation); wheel / scroll bar when the file is taller than the panel.
class XmlPreview : public ui::Widget {
public:
    explicit XmlPreview(std::wstring title) : m_title(std::move(title)) {
        m_scroll = &add<ui::ScrollBar>();
        m_scroll->onScroll = [this](float offset) {
            m_offset = offset;
            layout();
            invalidate();
        };
    }

    // The top of the panel, right of the title: room for the page's own control.
    [[nodiscard]] RectF headerArea() const {
        const RectF b = bounds();
        return {b.x + kPad, b.y, b.width - 2 * kPad, kHeader};
    }

    void setText(const std::wstring& xml) {
        auto lines = splitLines(xml);
        if (lines == m_lines) {
            return;
        }
        // The first text is the starting point, not a change.
        m_changed = m_lines.empty() ? std::vector<bool>(lines.size(), false) : changedLines(m_lines, lines);
        m_lines = std::move(lines);
        layout();
        invalidate();
    }

    void layout() override {
        const RectF b = bounds();
        rewrap();
        const float content = static_cast<float>(m_visual.size()) * kLine + kBottom;
        const float view = std::max(b.height - kHeader, 0.0f);
        m_offset = std::clamp(m_offset, 0.0f, std::max(content - view, 0.0f));
        m_scroll->setBounds({b.right() - ui::ScrollBar::kWidth - 1, b.y + kHeader, ui::ScrollBar::kWidth, view});
        m_scroll->setRange(content, view);
        m_scroll->setOffset(m_offset);
        m_scroll->setVisible(m_scroll->needed());
    }

    bool onWheel(ui::PointF /*p*/, float lines) override {
        if (!m_scroll->needed()) {
            return false;
        }
        m_offset -= lines * kLine * 3;
        layout();
        invalidate();
        return true;
    }

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.fillRect(b, Color::BgInput);
        canvas.strokeRoundRect(b, 0.0f, Color::LineSubtle);
        canvas.drawText(m_title, {b.x + kPad, b.y + 4, b.width - 2 * kPad, 16}, TypeStyle::Caption, Color::TextTertiary);

        const RectF view{b.x + 1, b.y + kHeader, b.width - 2, std::max(b.height - kHeader - 1, 0.0f)};
        canvas.pushClip(view);
        // A little slack: an exact-width box ellipsizes the number.
        const float numberWidth = std::ceil(static_cast<float>(numberDigits()) * m_charWidth) + 2;
        float y = view.y - m_offset;
        for (const auto& line : m_visual) {
            if (y + kLine > view.y && y < view.bottom()) {
                if (line.number > 0) {
                    canvas.drawText(std::to_wstring(line.number), {b.x + kPad - 2, y, numberWidth, kLine}, TypeStyle::Mono,
                                    Color::TextTertiary, ui::TextAlign::Trailing);
                }
                const float x = b.x + kPad - 2 + numberWidth + kNumberGap;
                canvas.drawText(line.text, {x, y, std::max(view.right() - x, 0.0f), kLine}, TypeStyle::Mono,
                                line.changed ? Color::AccentBase : Color::TextSecondary);
            }
            y += kLine;
        }
        canvas.popClip();
    }

private:
    static constexpr float kPad = 12.0f;
    static constexpr float kHeader = 41.0f; // title row + gap: the first line box starts here
    static constexpr float kLine = 16.0f;
    static constexpr float kNumberGap = 11.0f;
    static constexpr float kBottom = 12.0f;

    struct Visual {
        int number; // 0 = continuation of the line above
        std::wstring text;
        bool changed;
    };

    [[nodiscard]] int numberDigits() const { return m_lines.size() > 99 ? 3 : 2; }

    // Breaks the file's lines to the panel width (mono: a fixed number of characters).
    void rewrap() {
        m_visual.clear();
        m_charWidth = host() ? host()->text().measure(L"0000000000", TypeStyle::Mono) / 10.0f : 6.6f;
        const float room = bounds().width - 2 * kPad - static_cast<float>(numberDigits()) * m_charWidth - kNumberGap -
                           ui::ScrollBar::kWidth;
        const std::size_t columns = static_cast<std::size_t>(std::max(room / std::max(m_charWidth, 1.0f), 20.0f));
        for (std::size_t i = 0; i < m_lines.size(); ++i) {
            const std::wstring& line = m_lines[i];
            const bool changed = i < m_changed.size() && m_changed[i];
            if (line.size() <= columns) {
                m_visual.push_back({static_cast<int>(i + 1), line, changed});
                continue;
            }
            const std::size_t indent = std::min(line.find_first_not_of(L' '), line.size());
            const std::wstring hanging(std::min(indent + 4, columns / 2), L' ');
            std::wstring rest = line;
            bool first = true;
            while (!rest.empty()) {
                const std::size_t room2 = first ? columns : columns - hanging.size();
                std::size_t cut = std::min(room2, rest.size());
                if (cut < rest.size()) {
                    // Prefer a space to break at, as long as it is past the indent.
                    const std::size_t space = rest.rfind(L' ', cut);
                    if (space != std::wstring::npos && space > (first ? indent : 0)) {
                        cut = space + 1;
                    }
                }
                m_visual.push_back({first ? static_cast<int>(i + 1) : 0, first ? rest.substr(0, cut) : hanging + rest.substr(0, cut),
                                    changed});
                rest.erase(0, cut);
                first = false;
            }
        }
    }

    std::wstring m_title;
    std::vector<std::wstring> m_lines;
    std::vector<bool> m_changed;
    std::vector<Visual> m_visual;
    ui::ScrollBar* m_scroll = nullptr;
    float m_offset = 0;
    float m_charWidth = 6.6f;
};

UnattendedPage::UnattendedPage(AppState& state, UnattendController& controller, const Localization& strings)
    : m_state(state), m_controller(controller), m_strings(strings) {
    auto s = [&](Str key) { return strings.get(key); };
    m_steps = &add<StepBar>(std::vector<std::wstring>{s(Str::UnattendedStepsLocale), s(Str::UnattendedStepsAccount),
                                                      s(Str::UnattendedStepsDisk), s(Str::UnattendedStepsOobe),
                                                      s(Str::UnattendedStepsKey), s(Str::UnattendedStepsRequirements),
                                                      s(Str::UnattendedStepsSystem)});
    m_steps->onSelect = [this](int index) { showStep(index); };
    m_form = &add<ui::FormView>(kLabelWidth);
    m_form->onScroll = [this] { m_steps->setCurrent(m_form->currentSection()); };
    m_preview = &add<XmlPreview>(s(Str::UnattendedPreview));
    m_include = &add<ui::CheckField>(s(Str::UnattendedIncludeInIso), m_controller.includeInIso());
    m_include->setTooltip(s(Str::UnattendedIncludeInIsoHint));
    m_include->onChange = [this](bool on) { m_controller.setIncludeInIso(on); };
    setAccessible(ui::AccessRole::Group, s(Str::UnattendedTitle));

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Source) {
            buildForm(); // languages and editions come from the source
        } else if (change == AppState::Change::Unattend || change == AppState::Change::Selection ||
                   change == AppState::Change::Queue) { // the welcome's plan is in the queue (D-084)
            sync();
        }
    });
    buildForm();
}

UnattendedPage::~UnattendedPage() {
    m_state.unsubscribe(m_subscription);
}

void UnattendedPage::showStep(int index) {
    m_form->scrollToSection(index);
    m_steps->setCurrent(m_form->currentSection());
}

void UnattendedPage::addPicker(Str label, std::wstring firstLabel, std::vector<UnattendController::Choice> choices,
                               std::function<std::wstring(const Options&)> get,
                               std::function<void(Options&, const std::wstring&)> set) {
    auto& box = m_form->addRow<ui::Dropdown>(m_strings.get(label), std::wstring(), kPickerWidth, std::wstring(),
                                             std::vector<std::wstring>{firstLabel}, 0);
    box.setAccessible(ui::AccessRole::Group, m_strings.get(label));
    const std::size_t slot = m_pickers.size();
    box.onChange = [this, slot, set = std::move(set)](int index) {
        const auto& values = m_pickers[slot].values;
        if (index >= 0 && index < static_cast<int>(values.size())) {
            const std::wstring value = values[static_cast<std::size_t>(index)];
            m_controller.edit([&](Options& o) { set(o, value); });
        }
    };
    m_pickers.push_back(Picker{&box, std::move(firstLabel), std::move(choices), std::move(get), {}});
}

ui::SearchBox& UnattendedPage::addText(Str label, std::optional<Str> hint, float width,
                                       std::function<std::wstring(const Options&)> get,
                                       std::function<void(Options&, const std::wstring&)> set) {
    auto& box = m_form->addRow<ui::SearchBox>(m_strings.get(label), hint ? m_strings.get(*hint) : std::wstring(), width,
                                              std::wstring());
    box.setPlain(true);
    box.setAccessible(ui::AccessRole::Edit, m_strings.get(label));
    box.onChange = [this, set = std::move(set)](const std::wstring& text) {
        m_controller.edit([&](Options& o) { set(o, text); });
    };
    m_texts.push_back(TextField{&box, std::move(get)});
    return box;
}

void UnattendedPage::addSwitch(Str label, std::wstring onHint, std::function<bool(const Options&)> get,
                               std::function<void(Options&, bool)> set) {
    auto& toggle = m_form->addRow<ui::Toggle>(m_strings.get(label), std::wstring(), ui::tokens::size::toggleW,
                                              std::wstring(), false);
    toggle.setAccessible(ui::AccessRole::CheckBox, m_strings.get(label));
    toggle.onChange = [this, set = std::move(set)](bool on) {
        m_controller.edit([&](Options& o) { set(o, on); });
    };
    m_switches.push_back(Switch{&toggle, std::move(get), std::move(onHint)});
}

void UnattendedPage::buildForm() {
    // Never reached from one of the form's own controls (it destroys them): constructor and
    // source changes only.
    m_form->clear();
    m_pickers.clear();
    m_texts.clear();
    m_switches.clear();
    auto s = [&](Str key) { return m_strings.get(key); };
    const std::wstring ask = s(Str::UnattendedAsk);

    m_form->addSection(s(Str::UnattendedStepsLocale));
    addPicker(Str::UnattendedUiLanguage, ask, m_controller.languages(), [](const Options& o) { return o.uiLanguage; },
              [](Options& o, const std::wstring& v) { o.uiLanguage = v; });
    addPicker(Str::UnattendedLocale, ask, UnattendController::locales(), [](const Options& o) { return o.locale; },
              [](Options& o, const std::wstring& v) { o.locale = v; });
    addPicker(Str::UnattendedKeyboard, ask, UnattendController::keyboards(), [](const Options& o) { return o.keyboard; },
              [](Options& o, const std::wstring& v) { o.keyboard = v; });
    addPicker(Str::UnattendedTimeZone, s(Str::UnattendedTimeZoneAuto), UnattendController::timeZones(),
              [](const Options& o) { return o.timeZone; }, [](Options& o, const std::wstring& v) { o.timeZone = v; });

    m_form->addSection(s(Str::UnattendedStepsAccount));
    // D-084: WinLove's welcome in place of the account rows below.
    m_welcome = &m_form->addRow<ui::Toggle>(s(Str::UnattendedWelcome), std::wstring(), ui::tokens::size::toggleW, std::wstring(), false);
    m_welcome->setAccessible(ui::AccessRole::CheckBox, s(Str::UnattendedWelcome));
    m_welcome->onChange = [this](bool on) { m_controller.setWelcome(on); };
    auto askRow = [&](Str label, bool core::WelcomePlan::*field) {
        auto& toggle = m_form->addRow<ui::Toggle>(s(label), std::wstring(), ui::tokens::size::toggleW, std::wstring(), true);
        toggle.setAccessible(ui::AccessRole::CheckBox, s(label));
        toggle.onChange = [this, field](bool on) { editWelcome([&](core::WelcomePlan& p) { p.*field = on; }); };
        return &toggle;
    };
    m_askComputer = askRow(Str::UnattendedWelcomeAskComputer, &core::WelcomePlan::computerPage);
    m_askLook = askRow(Str::UnattendedWelcomeAskLook, &core::WelcomePlan::lookPage);
    m_askPrivacy = askRow(Str::UnattendedWelcomeAskPrivacy, &core::WelcomePlan::privacyPage);
    m_welcomeTheme = &m_form->addRow<ui::Dropdown>(s(Str::UnattendedWelcomeTheme), std::wstring(), kPickerWidth, std::wstring(),
                                                   std::vector<std::wstring>{s(Str::WelcomeThemeDark), s(Str::WelcomeThemeLight)}, 0);
    m_welcomeTheme->setAccessible(ui::AccessRole::Group, s(Str::UnattendedWelcomeTheme));
    m_welcomeTheme->onChange = [this](int index) { editWelcome([index](core::WelcomePlan& p) { p.theme = index == 1 ? L"light" : L"dark"; }); };
    m_welcomePrivacy = &m_form->addRow<ui::Dropdown>(s(Str::UnattendedWelcomePrivacy), std::wstring(), kPickerWidth, std::wstring(),
                                                     std::vector<std::wstring>{s(Str::WelcomePrivacyStrict), s(Str::WelcomePrivacyWindows)}, 0);
    m_welcomePrivacy->setAccessible(ui::AccessRole::Group, s(Str::UnattendedWelcomePrivacy));
    m_welcomePrivacy->onChange = [this](int index) {
        editWelcome([index](core::WelcomePlan& p) { p.privacy = index == 1 ? L"windows" : L"strict"; });
    };
    m_emptyPassword = askRow(Str::UnattendedWelcomeEmptyPassword, &core::WelcomePlan::allowEmptyPassword);
    m_welcomePreview = &m_form->addRow<ui::Button>(s(Str::UnattendedWelcomePreviewLabel), std::wstring(), 0.0f,
                                                   ui::ButtonKind::Secondary, s(Str::UnattendedWelcomePreview), ui::icons::Icon::Eye);
    m_welcomePreview->onInvoke = [this] {
        if (onPreviewWelcome) {
            onPreviewWelcome();
        }
    };
    m_account = &addText(Str::UnattendedLocalAccount, Str::UnattendedAccountHint, kFieldWidth,
                         [](const Options& o) { return o.accountName; },
                         [](Options& o, const std::wstring& v) { o.accountName = v; });
    m_password = &addText(Str::UnattendedPassword, std::nullopt, kFieldWidth, [](const Options& o) { return o.password; },
                          [](Options& o, const std::wstring& v) { o.password = v; });
    m_password->setPassword(true);
    m_autoLogon = &m_form->addRow<ui::CheckField>(s(Str::UnattendedAutoLogon), std::wstring(), 0.0f,
                                                  s(Str::UnattendedAutoLogonHint), false);
    m_autoLogon->setAccessible(ui::AccessRole::CheckBox, s(Str::UnattendedAutoLogon));
    m_autoLogon->onChange = [this](bool on) { m_controller.edit([on](Options& o) { o.autoLogon = on; }); };
    m_computer = &addText(Str::UnattendedComputerName, Str::UnattendedComputerNameHint, kFieldWidth,
                          [](const Options& o) { return o.computerName; },
                          [](Options& o, const std::wstring& v) { o.computerName = v; });
    addSwitch(Str::UnattendedRandomName, s(Str::UnattendedRandomNameHint), [](const Options& o) { return o.randomComputerName; },
              [](Options& o, bool on) { o.randomComputerName = on; });
    // Further accounts in one line: "ali=parola, ayse" (a name alone = no password).
    m_extraAccounts = &addText(Str::UnattendedExtraAccounts, Str::UnattendedExtraAccountsHint, kFieldWidth,
                               [](const Options& o) { return extraAccountsText(o.extraAccounts); },
                               [](Options& o, const std::wstring& v) {
                                   const bool admins = !o.extraAccounts.empty() && o.extraAccounts.front().administrator;
                                   o.extraAccounts = parseExtraAccounts(v, admins);
                               });
    addSwitch(Str::UnattendedExtraAdmins, {},
              [](const Options& o) { return !o.extraAccounts.empty() && o.extraAccounts.front().administrator; },
              [](Options& o, bool on) {
                  for (auto& account : o.extraAccounts) {
                      account.administrator = on;
                  }
              });
    addSwitch(Str::UnattendedBuiltInAdmin, s(Str::UnattendedBuiltInAdminHint), [](const Options& o) { return o.enableAdministrator; },
              [](Options& o, bool on) { o.enableAdministrator = on; });
    addText(Str::UnattendedAdminPassword, std::nullopt, kFieldWidth, [](const Options& o) { return o.administratorPassword; },
            [](Options& o, const std::wstring& v) { o.administratorPassword = v; })
        .setPassword(true);
    addSwitch(Str::UnattendedNeverExpire, {}, [](const Options& o) { return o.passwordsNeverExpire; },
              [](Options& o, bool on) { o.passwordsNeverExpire = on; });
    addSwitch(Str::UnattendedNoLockout, {}, [](const Options& o) { return o.disableLockout; },
              [](Options& o, bool on) { o.disableLockout = on; });
    addText(Str::UnattendedOwner, std::nullopt, kFieldWidth, [](const Options& o) { return o.registeredOwner; },
            [](Options& o, const std::wstring& v) { o.registeredOwner = v; });
    addText(Str::UnattendedOrganization, std::nullopt, kFieldWidth, [](const Options& o) { return o.registeredOrganization; },
            [](Options& o, const std::wstring& v) { o.registeredOrganization = v; });

    m_form->addSection(s(Str::UnattendedStepsDisk));
    m_disk = &m_form->addRow<ui::Dropdown>(s(Str::UnattendedDiskLayout), std::wstring(), kPickerWidth, std::wstring(),
                                           std::vector<std::wstring>{ask, s(Str::UnattendedDiskGpt), s(Str::UnattendedDiskMbr)},
                                           0);
    m_disk->setAccessible(ui::AccessRole::Group, s(Str::UnattendedDiskLayout));
    m_disk->onChange = [this](int index) {
        m_controller.edit([index](Options& o) { o.disk = static_cast<core::UnattendDisk>(std::clamp(index, 0, 2)); });
    };
    m_diskId = &addText(Str::UnattendedDiskId, Str::UnattendedDiskIdHint, kPickerWidth,
                        [](const Options& o) { return std::to_wstring(o.diskId); },
                        [](Options& o, const std::wstring& v) {
                            const bool digits = !v.empty() && v.size() <= 2 && std::ranges::all_of(v, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
                            o.diskId = digits ? std::stoi(v) : -1; // -1: shown as a problem until it is a number
                        });
    addSwitch(Str::UnattendedRecovery, s(Str::UnattendedRecoveryHint), [](const Options& o) { return o.recoveryPartition; },
              [](Options& o, bool on) { o.recoveryPartition = on; });
    // D-056: Compact OS — Windows' own files stay compressed on the disk.
    addSwitch(Str::UnattendedCompactOs, s(Str::UnattendedCompactOsHint), [](const Options& o) { return o.compactOs; },
              [](Options& o, bool on) { o.compactOs = on; });

    m_form->addSection(s(Str::UnattendedStepsOobe));
    addSwitch(Str::UnattendedAcceptEula, {}, [](const Options& o) { return o.acceptEula; },
              [](Options& o, bool on) { o.acceptEula = on; });
    addSwitch(Str::UnattendedSkipPrivacy, {}, [](const Options& o) { return o.skipPrivacy; },
              [](Options& o, bool on) { o.skipPrivacy = on; });
    // Every switch reads "on = WinLove writes this into the answer file" (D-032): the design's
    // "Microsoft hesabı zorunluluğu" / "TPM denetimi" rows wrote their XML when switched OFF.
    addSwitch(Str::UnattendedMsAccount, s(Str::UnattendedBypassNro), [](const Options& o) { return o.bypassNro; },
              [](Options& o, bool on) { o.bypassNro = on; });
    addSwitch(Str::UnattendedSkipOnline, {}, [](const Options& o) { return o.skipOnlineAccount; },
              [](Options& o, bool on) { o.skipOnlineAccount = on; });
    addSwitch(Str::UnattendedHideWifi, {}, [](const Options& o) { return o.hideWifiSetup || o.skipOnlineAccount; },
              [](Options& o, bool on) { o.hideWifiSetup = on; });
    addSwitch(Str::UnattendedHideOem, {}, [](const Options& o) { return o.hideOemRegistration; },
              [](Options& o, bool on) { o.hideOemRegistration = on; });

    m_form->addSection(s(Str::UnattendedStepsKey));
    m_key = &addText(Str::UnattendedProductKey, Str::UnattendedProductKeyHint, kPickerWidth,
                     [](const Options& o) { return o.productKey; },
                     [](Options& o, const std::wstring& v) { o.productKey = v; });
    addPicker(Str::UnattendedEdition, ask, m_controller.editions(),
              [](const Options& o) { return o.imageIndex > 0 ? std::to_wstring(o.imageIndex) : std::wstring(); },
              [](Options& o, const std::wstring& v) { o.imageIndex = v.empty() ? 0 : std::stoi(v); });

    // On = Setup skips the check (LabConfig).
    m_form->addSection(s(Str::UnattendedStepsRequirements));
    addSwitch(Str::UnattendedTpm, s(Str::UnattendedSkipLabConfig), [](const Options& o) { return o.bypassTpm; },
              [](Options& o, bool on) { o.bypassTpm = on; });
    addSwitch(Str::UnattendedSecureBoot, s(Str::UnattendedSkipLabConfig),
              [](const Options& o) { return o.bypassSecureBoot; }, [](Options& o, bool on) { o.bypassSecureBoot = on; });
    addSwitch(Str::UnattendedRam, s(Str::UnattendedSkipLabConfig), [](const Options& o) { return o.bypassRam; },
              [](Options& o, bool on) { o.bypassRam = on; });
    addSwitch(Str::UnattendedCpu, s(Str::UnattendedSkipLabConfig), [](const Options& o) { return o.bypassCpu; },
              [](Options& o, bool on) { o.bypassCpu = on; });
    addSwitch(Str::UnattendedStorage, s(Str::UnattendedSkipLabConfig), [](const Options& o) { return o.bypassStorage; },
              [](Options& o, bool on) { o.bypassStorage = on; });

    // Sistem ve komutlar: specialize (SYSTEM, before OOBE) and the first logon.
    m_form->addSection(s(Str::UnattendedStepsSystem));
    addSwitch(Str::UnattendedNoEncryption, s(Str::UnattendedNoEncryptionHint),
              [](const Options& o) { return o.preventDeviceEncryption; }, [](Options& o, bool on) { o.preventDeviceEncryption = on; });
    for (std::size_t i = 0; i < kCommandSlots; ++i) {
        addText(Str::UnattendedSystemCommand, i == 0 ? std::optional<Str>(Str::UnattendedSystemCommandHint) : std::nullopt, kFieldWidth,
                [i](const Options& o) { return i < o.specializeCommands.size() ? o.specializeCommands[i] : std::wstring(); },
                [i](Options& o, const std::wstring& v) { setSlot(o.specializeCommands, i, v); });
    }
    for (std::size_t i = 0; i < kCommandSlots; ++i) {
        addText(Str::UnattendedLogonCommand, i == 0 ? std::optional<Str>(Str::UnattendedLogonCommandHint) : std::nullopt, kFieldWidth,
                [i](const Options& o) { return i < o.firstLogonCommands.size() ? o.firstLogonCommands[i] : std::wstring(); },
                [i](Options& o, const std::wstring& v) { setSlot(o.firstLogonCommands, i, v); });
    }

    sync();
    m_steps->setCurrent(m_form->currentSection());
}

void UnattendedPage::sync() {
    const Options o = m_controller.options();
    const auto problems = m_controller.problems();
    auto has = [&](Problem p) { return std::ranges::find(problems, p) != problems.end(); };

    for (auto& picker : m_pickers) {
        std::vector<std::wstring> items{picker.firstLabel};
        picker.values = {std::wstring()};
        for (const auto& choice : picker.choices) {
            items.push_back(choice.label);
            picker.values.push_back(choice.value);
        }
        const std::wstring current = picker.get(o);
        auto at = std::ranges::find(picker.values, current);
        if (at == picker.values.end()) {
            // A value from an imported file that the lists do not offer: shown, and kept.
            items.push_back(m_strings.format(Str::UnattendedFromFile, {{L"value", current}}));
            picker.values.push_back(current);
            at = picker.values.end() - 1;
        }
        picker.box->setItems(std::move(items), static_cast<int>(at - picker.values.begin()));
    }
    for (const auto& field : m_texts) {
        // Not the field being typed in: its text is the source (a half-typed "ali=" or disk number
        // would be rewritten from the parsed value under the caret).
        if (field.box->focused()) {
            continue;
        }
        if (const std::wstring value = field.get(o); field.box->text() != value) {
            field.box->setText(value);
        }
    }
    for (const auto& sw : m_switches) {
        const bool on = sw.get(o);
        if (sw.toggle->isOn() != on) {
            sw.toggle->setOn(on);
        }
        m_form->setHint(*sw.toggle, on ? sw.onHint : std::wstring());
    }
    m_disk->setSelected(static_cast<int>(o.disk));
    m_form->setHint(*m_disk, o.disk == core::UnattendDisk::Ask ? std::wstring() : m_strings.get(Str::UnattendedDiskWarning),
                    Color::StatusWarning);
    m_autoLogon->setChecked(o.autoLogon);

    // A problem with the value in red; otherwise, while the field is empty, what empty means.
    auto hint = [&](const ui::Widget& control, Problem problem, Str problemText, std::optional<Str> whenEmpty) {
        if (has(problem)) {
            m_form->setHint(control, m_strings.get(problemText), Color::StatusError);
        } else {
            m_form->setHint(control, whenEmpty ? m_strings.get(*whenEmpty) : std::wstring());
        }
    };
    auto emptyHint = [](const std::wstring& value, Str text) { return value.empty() ? std::optional<Str>(text) : std::nullopt; };
    hint(*m_account, Problem::AccountName, Str::UnattendedProblemAccountName,
         emptyHint(o.accountName, Str::UnattendedAccountHint));
    hint(*m_computer, Problem::ComputerName, Str::UnattendedProblemComputerName,
         emptyHint(o.computerName, Str::UnattendedComputerNameHint));
    hint(*m_key, Problem::ProductKey, Str::UnattendedProblemProductKey, emptyHint(o.productKey, Str::UnattendedProductKeyHint));
    hint(*m_autoLogon, Problem::AutoLogonNeedsAccount, Str::UnattendedProblemAutoLogon, std::nullopt);
    hint(*m_extraAccounts, Problem::ExtraAccountName, Str::UnattendedProblemExtraAccounts,
         o.extraAccounts.empty() ? std::optional<Str>(Str::UnattendedExtraAccountsHint) : std::nullopt);
    hint(*m_diskId, Problem::DiskId, Str::UnattendedProblemDiskId, std::optional<Str>(Str::UnattendedDiskIdHint));

    // D-084: the welcome's rows from its plan in the queue; the account rows it replaces are off.
    if (m_welcome) {
        const auto plan = m_controller.welcomePlan();
        if (m_welcome->isOn() != o.welcome) {
            m_welcome->setOn(o.welcome);
        }
        m_form->setHint(*m_welcome, o.welcome ? m_strings.get(Str::UnattendedWelcomeHint) : std::wstring());
        for (const auto& [toggle, on] : {std::pair{m_askComputer, plan.computerPage}, std::pair{m_askLook, plan.lookPage},
                                         std::pair{m_askPrivacy, plan.privacyPage}, std::pair{m_emptyPassword, plan.allowEmptyPassword}}) {
            if (toggle->isOn() != on) {
                toggle->setOn(on);
            }
            toggle->setEnabled(o.welcome);
        }
        m_welcomeTheme->setSelected(plan.theme == L"light" ? 1 : 0);
        m_welcomePrivacy->setSelected(plan.privacy == L"windows" ? 1 : 0);
        for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_welcomeTheme, m_welcomePrivacy, m_welcomePreview}) {
            w->setEnabled(o.welcome);
        }
        m_welcomePreview->setEnabled(true); // a preview needs nothing switched on
        for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_account, m_password, m_autoLogon}) {
            w->setEnabled(!o.welcome);
        }
        if (o.welcome) {
            m_form->setHint(*m_account, m_strings.get(Str::UnattendedWelcomeUsed));
        }
    }

    m_include->setChecked(m_controller.includeInIso());
    m_preview->setText(m_controller.xml());
    invalidate();
}

void UnattendedPage::editWelcome(const std::function<void(core::WelcomePlan&)>& change) {
    if (!m_controller.welcome()) {
        return;
    }
    auto plan = m_controller.welcomePlan();
    change(plan);
    m_controller.setWelcomePlan(std::move(plan));
    sync();
}

void UnattendedPage::layout() {
    const RectF b = bounds();
    const float previewWidth = std::round(std::max(b.width - kPanelGap, 0.0f) * kPreviewShare);
    const float formWidth = std::max(b.width - previewWidth - kPanelGap, 0.0f);
    m_steps->setBounds({b.x, b.y + kTop, formWidth, kStepsHeight});
    const float formTop = b.y + kTop + kStepsHeight + kFormGap;
    m_form->setBounds({b.x, formTop, formWidth, std::max(b.bottom() - formTop, 0.0f)});
    m_preview->setBounds({b.right() - previewWidth, b.y + kTop, previewWidth,
                          std::max(b.height - kTop - kPanelBottom, 0.0f)});
    const RectF header = m_preview->headerArea();
    const float width = m_include->measure({}).width;
    m_include->setBounds({header.right() - width, header.y, width, ui::tokens::size::control});
}

} // namespace wl::app
