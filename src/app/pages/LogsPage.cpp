#include "app/pages/LogsPage.h"

#include "app/Format.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "ui/widget/Host.h"

#include <algorithm>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 8.0f;
constexpr float kConsoleGap = 12.0f;

std::wstring clockTime(std::chrono::system_clock::time_point t) {
    const auto local = std::chrono::zoned_time(std::chrono::current_zone(), t).get_local_time();
    return std::format(L"{:%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(local));
}
} // namespace

LogsPage::LogsPage(AppState& state, const Localization& strings, Language language)
    : m_state(state), m_strings(strings), m_language(language) {
    m_search = &add<ui::SearchBox>(strings.get(Str::LogsSearchLog), std::vector<std::wstring>{L"Ctrl", L"F"});
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = wl::text::lower(text);
        m_console->setHighlight(text);
        rebuild();
    };
    m_level = &add<ui::Dropdown>(strings.get(Str::LogsLevel),
                                 std::vector<std::wstring>{strings.get(Str::CommonAll), strings.get(Str::LogsDebugAndUp),
                                                           strings.get(Str::LogsInfoAndUp), strings.get(Str::LogsWarnAndUp),
                                                           strings.get(Str::LogsErrorsOnly)},
                                 static_cast<int>(MinLevel::Info));
    m_level->onChange = [this](int index) {
        m_minLevel = static_cast<MinLevel>(index);
        rebuild();
    };
    m_sourceBox = &add<ui::Dropdown>(strings.get(Str::LogsSource), std::vector<std::wstring>{strings.get(Str::CommonAll)}, 0);
    m_sourceBox->onChange = [this](int index) {
        m_source = index <= 0 ? std::string() : m_sources[static_cast<std::size_t>(index - 1)];
        rebuild();
    };
    m_follow = &add<ui::Toggle>(strings.get(Str::LogsAutoScroll), true);
    m_follow->onChange = [this](bool on) {
        if (on) {
            m_console->scrollToEnd();
        } else {
            m_console->setAutoScroll(false);
        }
    };
    m_console = &add<ui::LogConsole>();
    m_console->onAutoScrollChanged = [this](bool on) { m_follow->setOn(on); };
    m_console->newLinesText = [this](std::size_t n) {
        return m_strings.format(Str::LogsNewLines, {{L"n", std::to_wstring(n)}});
    };
    m_console->copyFormat = [](const ui::LogLine& l) {
        return std::format(L"{} {:<5} {:<6} {}", l.time,
                           l.level == ui::LogLevel::Debug  ? L"DBG"
                           : l.level == ui::LogLevel::Info ? L"INFO"
                           : l.level == ui::LogLevel::Warn ? L"WARN"
                                                           : L"ERR",
                           l.source, l.message);
    };
    setAccessible(ui::AccessRole::Group, strings.get(Str::LogsTitle));

    m_version = m_state.logClearedVersion();
    poll();
}

ui::LogLine LogsPage::toLine(const log::Entry& entry) {
    ui::LogLevel level = ui::LogLevel::Info;
    switch (entry.level) {
    case log::Level::Trace:
    case log::Level::Debug: level = ui::LogLevel::Debug; break;
    case log::Level::Info: level = ui::LogLevel::Info; break;
    case log::Level::Warn: level = ui::LogLevel::Warn; break;
    case log::Level::Error: level = ui::LogLevel::Error; break;
    }
    return {clockTime(entry.time), level, utf8::toWide(entry.source), entry.message};
}

bool LogsPage::passes(const log::Entry& entry) const {
    const auto rank = [](log::Level l) {
        switch (l) {
        case log::Level::Trace: return 0;
        case log::Level::Debug: return 1;
        case log::Level::Info: return 2;
        case log::Level::Warn: return 3;
        case log::Level::Error: return 4;
        }
        return 0;
    };
    if (rank(entry.level) < static_cast<int>(m_minLevel)) {
        return false;
    }
    if (!m_source.empty() && entry.source != m_source) {
        return false;
    }
    return m_needle.empty() || wl::text::lower(entry.message).find(m_needle) != std::wstring::npos;
}

void LogsPage::updateSources() {
    bool added = false;
    for (const auto& e : m_entries) {
        if (std::ranges::find(m_sources, e.source) == m_sources.end()) {
            m_sources.push_back(e.source);
            added = true;
        }
    }
    if (!added) {
        return;
    }
    std::ranges::sort(m_sources);
    std::vector<std::wstring> items{m_strings.get(Str::CommonAll)};
    int selected = 0;
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        items.push_back(utf8::toWide(m_sources[i]));
        if (m_sources[i] == m_source) {
            selected = static_cast<int>(i) + 1;
        }
    }
    m_sourceBox->setItems(std::move(items), selected);
    layout();
}

void LogsPage::rebuild() {
    std::vector<ui::LogLine> lines;
    m_lastTime.clear();
    for (const auto& e : m_entries) {
        if (passes(e)) {
            lines.push_back(toLine(e));
        }
    }
    if (!lines.empty()) {
        m_lastTime = lines.back().time;
    }
    m_console->setLines(std::move(lines));
    invalidate();
}

void LogsPage::poll() {
    const auto buffer = m_state.logBuffer();
    if (!buffer) {
        return;
    }
    auto fresh = buffer->since(m_version);
    if (fresh.empty()) {
        return;
    }
    std::vector<ui::LogLine> lines;
    for (auto& e : fresh) {
        if (passes(e)) {
            lines.push_back(toLine(e));
        }
        m_entries.push_back(std::move(e));
    }
    // Same cap as the ring buffer: the page never holds more than the process keeps.
    constexpr std::size_t kMax = 20000;
    if (m_entries.size() > kMax) {
        m_entries.erase(m_entries.begin(), m_entries.begin() + static_cast<std::ptrdiff_t>(m_entries.size() - kMax));
        updateSources();
        rebuild();
        return;
    }
    updateSources();
    if (!lines.empty()) {
        m_lastTime = lines.back().time;
        m_console->append(lines);
    }
    invalidate();
}

void LogsPage::clear() {
    if (const auto buffer = m_state.logBuffer()) {
        m_version = buffer->version();
        m_state.setLogClearedVersion(m_version);
    }
    m_entries.clear();
    rebuild();
}

void LogsPage::focusSearch() {
    if (host()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

std::wstring LogsPage::exportText() const {
    std::wstring out;
    for (const auto& e : m_entries) {
        if (passes(e)) {
            out += log::formatLine(e);
            out += L"\r\n";
        }
    }
    return out;
}

void LogsPage::layout() {
    const RectF b = bounds();
    const float y = b.y + kToolbarTop;
    float x = b.x;
    m_search->setWidth(240);
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{m_search, m_level, m_sourceBox, m_follow}) {
        const ui::SizeF size = w->measure({});
        w->setBounds({x, y, size.width, kToolbar});
        x += size.width + kGap;
    }
    const float top = y + kToolbar + kConsoleGap;
    m_console->setBounds({b.x, top, b.width, std::max(b.bottom() - top, 0.0f)});
}

void LogsPage::paint(ui::Canvas& canvas) {
    // Counter on the right of the toolbar: "1.284 satır · son 14:22:20".
    const RectF b = bounds();
    const std::wstring counter =
        m_strings.format(Str::LogsLines, {{L"n", formatCount(m_console->lineCount(), m_language)},
                                          {L"t", m_lastTime.empty() ? L"—" : m_lastTime}});
    const float left = m_follow->bounds().right() + kGap;
    canvas.drawText(counter, {left, b.y + kToolbarTop, std::max(b.right() - left, 0.0f), kToolbar}, TypeStyle::Caption,
                    Color::TextSecondary, ui::TextAlign::Trailing);
}

} // namespace wl::app
