#include "base/Log.h"

#include "base/Utf8.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <fstream>

namespace wl::log {

namespace {

struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<Sink>> sinks;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

std::atomic<Level> g_minimum{Level::Debug};

class FileSink final : public Sink {
public:
    explicit FileSink(const std::filesystem::path& path) : m_file(path, std::ios::binary | std::ios::app) {}
    void write(const Entry& entry) override {
        const std::string line = utf8::fromWide(formatLine(entry)) + "\r\n";
        std::scoped_lock lock(m_mutex);
        m_file.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_file.flush(); // a crash must not lose the lines that explain it
    }

private:
    std::mutex m_mutex;
    std::ofstream m_file;
};

class StdoutSink final : public Sink {
public:
    void write(const Entry& entry) override {
        const std::wstring line = formatLine(entry) + L"\n";
        std::scoped_lock lock(m_mutex);
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode)) {
            DWORD written = 0;
            WriteConsoleW(out, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        } else {
            const std::string utf8 = utf8::fromWide(line);
            std::fwrite(utf8.data(), 1, utf8.size(), stdout);
            std::fflush(stdout);
        }
    }

private:
    std::mutex m_mutex;
};

} // namespace

void addSink(std::shared_ptr<Sink> sink) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    r.sinks.push_back(std::move(sink));
}

void removeSink(const std::shared_ptr<Sink>& sink) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    std::erase(r.sinks, sink);
}

void setMinimumLevel(Level level) {
    g_minimum = level;
}

void write(Level level, std::string_view source, std::wstring message) {
    if (level < g_minimum.load()) {
        return;
    }
    const Entry entry{std::chrono::system_clock::now(), level, std::string(source), std::move(message),
                      GetCurrentThreadId()};
    std::vector<std::shared_ptr<Sink>> sinks;
    {
        auto& r = registry();
        std::scoped_lock lock(r.mutex);
        sinks = r.sinks; // write outside the lock: sinks may be slow (disk)
    }
    for (const auto& sink : sinks) {
        sink->write(entry);
    }
}

const wchar_t* levelName(Level level) noexcept {
    switch (level) {
    case Level::Trace: return L"TRC";
    case Level::Debug: return L"DBG";
    case Level::Info: return L"INFO";
    case Level::Warn: return L"WARN";
    case Level::Error: return L"ERR";
    }
    return L"?";
}

std::wstring formatLine(const Entry& entry) {
    const auto local = std::chrono::zoned_time(std::chrono::current_zone(), entry.time).get_local_time();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(local.time_since_epoch()) % 1000;
    const auto time = std::chrono::floor<std::chrono::seconds>(local);
    return std::format(L"{:%H:%M:%S}.{:03} {:<5} {:<5} {}", time, ms.count(), levelName(entry.level),
                       utf8::toWide(entry.source), entry.message);
}

void RingBufferSink::write(const Entry& entry) {
    std::scoped_lock lock(m_mutex);
    if (m_entries.size() < m_capacity) {
        m_entries.push_back(entry);
    } else {
        m_entries[m_next] = entry;
        m_next = (m_next + 1) % m_capacity;
    }
    ++m_version;
}

std::vector<Entry> RingBufferSink::snapshot() const {
    std::scoped_lock lock(m_mutex);
    if (m_entries.size() < m_capacity) {
        return m_entries;
    }
    std::vector<Entry> ordered(m_entries.begin() + static_cast<std::ptrdiff_t>(m_next), m_entries.end());
    ordered.insert(ordered.end(), m_entries.begin(), m_entries.begin() + static_cast<std::ptrdiff_t>(m_next));
    return ordered;
}

std::vector<Entry> RingBufferSink::since(std::uint64_t& version) const {
    std::scoped_lock lock(m_mutex);
    const std::uint64_t fresh = m_version > version ? m_version - version : 0;
    version = m_version;
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(fresh, m_entries.size()));
    std::vector<Entry> result;
    result.reserve(count);
    // Oldest buffered entry sits at m_next once the buffer is full, else at 0.
    const std::size_t size = m_entries.size();
    const std::size_t start = size < m_capacity ? size - count : (m_next + size - count) % size;
    for (std::size_t i = 0; i < count; ++i) {
        result.push_back(m_entries[(start + i) % size]);
    }
    return result;
}

std::uint64_t RingBufferSink::version() const {
    std::scoped_lock lock(m_mutex);
    return m_version;
}

std::shared_ptr<Sink> makeFileSink(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const auto now = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now());
    const auto name = std::format(L"WinLove-{:%Y%m%d-%H%M%S}.log",
                                  std::chrono::floor<std::chrono::seconds>(now.get_local_time()));
    return std::make_shared<FileSink>(directory / name);
}

std::shared_ptr<Sink> makeStdoutSink() {
    return std::make_shared<StdoutSink>();
}

std::filesystem::path defaultDirectory() {
    PWSTR local = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        path = std::filesystem::path(local) / L"WinLove" / L"logs";
    }
    CoTaskMemFree(local);
    return path;
}

} // namespace wl::log
