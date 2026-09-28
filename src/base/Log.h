#pragma once
// Process-wide log. Thread-safe. Entries fan out to sinks: file (%LOCALAPPDATA%\WinLove\logs),
// the in-memory ring buffer the Logs page reads (P03), and stdout for wlcli.
//
//   log::info("wim", std::format(L"{} images in {}", count, path));
//   log::error("dism", describe(error));
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace wl::log {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error };

struct Entry {
    std::chrono::system_clock::time_point time;
    Level level;
    std::string source;   // short subsystem tag: "app", "wim", "udf", "dism", "reg", "ui"
    std::wstring message;
    std::uint32_t threadId;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Entry& entry) = 0;
};

void addSink(std::shared_ptr<Sink> sink);
void removeSink(const std::shared_ptr<Sink>& sink);
void setMinimumLevel(Level level);

void write(Level level, std::string_view source, std::wstring message);
inline void trace(std::string_view source, std::wstring message) { write(Level::Trace, source, std::move(message)); }
inline void debug(std::string_view source, std::wstring message) { write(Level::Debug, source, std::move(message)); }
inline void info(std::string_view source, std::wstring message) { write(Level::Info, source, std::move(message)); }
inline void warn(std::string_view source, std::wstring message) { write(Level::Warn, source, std::move(message)); }
inline void error(std::string_view source, std::wstring message) { write(Level::Error, source, std::move(message)); }

[[nodiscard]] const wchar_t* levelName(Level level) noexcept; // "DBG", "INFO", "WARN", "ERR"
// "14:02:11.482 INFO  wim   message" — the format of the file sink and stdout.
[[nodiscard]] std::wstring formatLine(const Entry& entry);

// Keeps the last `capacity` entries; the Logs page polls snapshot() when version() changes.
class RingBufferSink final : public Sink {
public:
    explicit RingBufferSink(std::size_t capacity = 20000) : m_capacity(capacity) {}
    void write(const Entry& entry) override;
    [[nodiscard]] std::vector<Entry> snapshot() const;
    [[nodiscard]] std::uint64_t version() const;
    // Entries written after `version` (oldest first); `version` is advanced to the current one.
    // If more than `capacity` were written meanwhile, returns what is still buffered.
    [[nodiscard]] std::vector<Entry> since(std::uint64_t& version) const;

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries; // circular once full
    std::size_t m_next = 0;
    std::size_t m_capacity;
    std::uint64_t m_version = 0;
};

// Appends UTF-8 lines; one file per process start: WinLove-YYYYMMDD-HHMMSS.log
[[nodiscard]] std::shared_ptr<Sink> makeFileSink(const std::filesystem::path& directory);
// UTF-8 to stdout (console or pipe); used by wlcli.
[[nodiscard]] std::shared_ptr<Sink> makeStdoutSink();
// %LOCALAPPDATA%\WinLove\logs
[[nodiscard]] std::filesystem::path defaultDirectory();

} // namespace wl::log
