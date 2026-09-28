#pragma once
// LogConsole per 03_components/log-console-inspector-diff-palette.md: bg.input, 1px line.subtle,
// padding 8 12, rows 20 in mono 11; columns time (text.tertiary) · level (DBG text.tertiary,
// INFO status.info, WARN status.warning, ERR status.error) · source (text.secondary) · message
// (text.secondary; ERR rows text.primary on status.errorSubtle). Search matches get
// accent.subtle. Auto-scroll follows new lines until the user scrolls up; then a
// "↓ n new lines" chip (20px, bottom centre) jumps back. Virtualized: only visible rows paint.
// Selection: click / Shift+click / drag rows, Ctrl+A; Ctrl+C copies the selection.
#include "ui/widget/Widget.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace wl::ui {

enum class LogLevel : std::uint8_t { Debug, Info, Warn, Error };

struct LogLine {
    std::wstring time;    // "14:22:16"
    LogLevel level = LogLevel::Info;
    std::wstring source;  // "dism"
    std::wstring message;
};

class LogConsole : public Widget {
public:
    static constexpr float kRow = 20.0f;

    LogConsole();

    std::function<void(bool)> onAutoScrollChanged;          // the user scrolled away / back
    std::function<std::wstring(std::size_t)> newLinesText;   // "↓ {n} yeni satır"
    std::function<std::wstring(const LogLine&)> copyFormat;  // one line of Ctrl+C output

    void setLines(std::vector<LogLine> lines);
    void append(std::span<const LogLine> lines);
    [[nodiscard]] std::size_t lineCount() const noexcept { return m_lines.size(); }
    [[nodiscard]] const std::vector<LogLine>& lines() const noexcept { return m_lines; }

    void setHighlight(std::wstring needle); // case-insensitive, message column
    void setAutoScroll(bool on);
    [[nodiscard]] bool autoScroll() const noexcept { return m_autoScroll; }
    void scrollToEnd();

    [[nodiscard]] std::wstring selectedText() const;

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    bool onWheel(PointF p, float lines) override;
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] RectF viewport() const; // rows area (inside padding, left of the scrollbar)
    [[nodiscard]] float maxOffset() const;
    [[nodiscard]] int rowAt(PointF p) const;
    [[nodiscard]] RectF chipRect(Canvas* canvas) const;
    void setOffset(float offset, bool byUser);
    void select(int from, int to);
    void revealRow(int row);

    std::vector<LogLine> m_lines;
    std::wstring m_needle;
    float m_offset = 0;
    bool m_autoScroll = true;
    std::size_t m_unseen = 0; // lines appended while not following
    int m_anchor = -1;
    int m_selFrom = -1;
    int m_selTo = -1;
    bool m_dragging = false;
    float m_chipWidth = 0;
    ScrollBar* m_scrollBar = nullptr;
};

} // namespace wl::ui
