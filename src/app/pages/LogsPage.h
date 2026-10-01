#pragma once
// P03 Loglar (docs/pages/03-logs.md, screen 18): toolbar — search (Ctrl F), level, source,
// auto-scroll, line counter — over a full-height LogConsole. Reads the process log ring buffer
// (AppState::logBuffer) incrementally: the shell calls poll() on a timer while the page is shown.
// "Temizle" hides everything logged so far (the files on disk are untouched).
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/LogConsole.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/Toggle.h"

#include <string>
#include <vector>

namespace wl::app {

class LogsPage : public ui::Widget {
public:
    LogsPage(AppState& state, const Localization& strings, Language language);

    void poll();                               // pull new entries from the ring buffer
    void clear();                              // "Temizle"
    void focusSearch();                        // Ctrl+F
    [[nodiscard]] std::wstring exportText() const; // filtered lines, file-log format

    void layout() override;
    void paint(ui::Canvas& canvas) override;

    // One log entry as the console shows it (also the Apply page's live log).
    [[nodiscard]] static ui::LogLine toLine(const log::Entry& entry);

private:
    enum class MinLevel : std::uint8_t { All, Debug, Info, Warn, Error };
    [[nodiscard]] bool passes(const log::Entry& entry) const;
    void rebuild();          // filters changed: refilter everything
    void updateSources();    // new source tags appeared

    AppState& m_state;
    const Localization& m_strings;
    Language m_language;
    std::vector<log::Entry> m_entries; // everything since the last "Temizle"
    std::vector<std::string> m_sources;
    std::uint64_t m_version = 0;
    MinLevel m_minLevel = MinLevel::Info;
    std::string m_source;    // empty = all
    std::wstring m_needle;   // lower-cased search text
    std::wstring m_lastTime; // time of the newest visible line
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_level = nullptr;
    ui::Dropdown* m_sourceBox = nullptr;
    ui::Toggle* m_follow = nullptr;
    ui::LogConsole* m_console = nullptr;
};

} // namespace wl::app
