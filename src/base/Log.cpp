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

using SinkList = std::vector<std::shared_ptr<Sink>>;

struct Registry {
    std::mutex mutex;
    // Replaced as a whole when a sink is added or removed, so that a line being written takes one
    // reference instead of copying the list (audit ENG-11). Never null.
    std::shared_ptr<const SinkList> sinks = std::make_shared<const SinkList>();
};

// The session logs makeFileSink() leaves in a folder, and how many of them stay (audit BASE-02 /
// ENG-12: nothing ever removed them).
constexpr std::size_t kSessionLogsKept = 50;

// "WinLove-YYYYMMDD-HHMMSS.log", exactly: nothing else in the folder (dism.log, a file the user
// put there, a renamed copy) is ever a candidate.
bool isSessionLogName(const std::wstring& name) {
    constexpr std::wstring_view prefix = L"WinLove-";
    constexpr std::wstring_view suffix = L".log";
    constexpr std::size_t stampLength = 15; // YYYYMMDD-HHMMSS
    if (name.size() != prefix.size() + stampLength + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix)) {
        return false;
    }
    for (std::size_t i = 0; i < stampLength; ++i) {
        const wchar_t c = name[prefix.size() + i];
        if (i == 8 ? c != L'-' : (c < L'0' || c > L'9')) {
            return false;
        }
    }
    return true;
}

// Removes the oldest session logs beyond the newest kSessionLogsKept. Only regular files directly
// in `directory` with a session log's exact name; the stamp in the name orders them. A log another
// running WinLove still writes cannot be deleted (it is open without delete sharing) and stays.
// Every failure is ignored: the log must start whatever the folder looks like.
void pruneSessionLogs(const std::filesystem::path& directory) {
    std::error_code ec;
    std::vector<std::wstring> names;
    std::filesystem::directory_iterator it(directory, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || entryError || it->is_symlink(entryError) || entryError) {
            continue;
        }
        std::wstring name = it->path().filename().wstring();
        if (isSessionLogName(name)) {
            names.push_back(std::move(name));
        }
    }
    if (ec || names.size() <= kSessionLogsKept) {
        return; // a listing that failed half-way is no basis for deleting anything
    }
    std::ranges::sort(names); // oldest first: the stamp is fixed-width digits
    names.resize(names.size() - kSessionLogsKept);
    for (const auto& name : names) {
        std::error_code removeError;
        std::filesystem::remove(directory / name, removeError);
    }
}

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
    auto next = std::make_shared<SinkList>(*r.sinks);
    next->push_back(std::move(sink));
    r.sinks = std::move(next);
}

void removeSink(const std::shared_ptr<Sink>& sink) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    auto next = std::make_shared<SinkList>(*r.sinks);
    std::erase(*next, sink);
    r.sinks = std::move(next);
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
    std::shared_ptr<const SinkList> sinks;
    {
        auto& r = registry();
        std::scoped_lock lock(r.mutex);
        sinks = r.sinks; // write outside the lock: sinks may be slow (disk)
    }
    for (const auto& sink : *sinks) {
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

std::wstring clockText(std::chrono::system_clock::time_point time, bool milliseconds) {
    // system_clock counts 100 ns from 1970; FILETIME from 1601.
    const auto ticks = std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, 10'000'000>>>(
                           time.time_since_epoch()).count() + 116444736000000000LL;
    FILETIME file{static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (ticks < 0 || !FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return milliseconds ? L"00:00:00.000" : L"00:00:00";
    }
    return milliseconds ? std::format(L"{:02}:{:02}:{:02}.{:03}", local.wHour, local.wMinute, local.wSecond, local.wMilliseconds)
                        : std::format(L"{:02}:{:02}:{:02}", local.wHour, local.wMinute, local.wSecond);
}

std::wstring formatLine(const Entry& entry) {
    return std::format(L"{} {:<5} {:<5} {}", clockText(entry.time, true), levelName(entry.level),
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
    pruneSessionLogs(directory);
    // Win32's local time, as clockText(): current_zone() throws on a Windows 10 without icu.dll,
    // and this runs before anything could report it.
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const auto name = std::format(L"WinLove-{:04}{:02}{:02}-{:02}{:02}{:02}.log", now.wYear, now.wMonth, now.wDay, now.wHour,
                                  now.wMinute, now.wSecond);
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
