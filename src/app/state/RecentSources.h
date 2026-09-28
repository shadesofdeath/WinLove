#pragma once
// "Son kullanılanlar": the last 10 opened sources, newest first, persisted as JSON in
// %LOCALAPPDATA%\WinLove\recent.json. Summaries are stored so the list renders without
// reopening files (and still shows entries whose file was moved or deleted).
#include "base/Result.h"
#include "core/image/Source.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::app {

struct RecentSource {
    std::filesystem::path path;
    std::wstring format;   // "ISO", "WIM", "Folder"…
    std::wstring summary;  // "11 25H2 · 26200.8037"
    std::uint64_t size = 0;
    std::chrono::system_clock::time_point lastOpened;
};

class RecentSources {
public:
    static constexpr std::size_t kCapacity = 10;

    explicit RecentSources(std::filesystem::path file) : m_file(std::move(file)) {}
    [[nodiscard]] static std::filesystem::path defaultFile();

    // Missing file = empty list; a corrupt file is logged and treated as empty (never fatal).
    void load();
    [[nodiscard]] Result<void> save() const;

    // Moves (or inserts) the source to the top and saves.
    void touch(const core::SourceInfo& source, std::chrono::system_clock::time_point when = std::chrono::system_clock::now());
    void remove(const std::filesystem::path& path);
    [[nodiscard]] const std::vector<RecentSource>& entries() const noexcept { return m_entries; }

private:
    std::filesystem::path m_file;
    std::vector<RecentSource> m_entries;
};

} // namespace wl::app
