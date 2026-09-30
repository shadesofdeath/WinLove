#pragma once
// D-049: entries WinLove puts into the image's hosts file (Windows\System32\drivers\etc\hosts).
// Each list is a section of its own between marker lines, so lists come and go independently
// and whatever else the file holds (Microsoft's comments, a user's own lines) is never touched:
//   # >>> WinLove: telemetry
//   0.0.0.0 vortex.data.microsoft.com
//   # <<< WinLove: telemetry
// In the queue a section is one SetHosts operation: target = section id, value = its entries,
// one "address name" per line ("" removes the section).
#include "base/Result.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct HostEntry {
    std::wstring address; // "0.0.0.0", "127.0.0.1", "::" or any IPv4 / IPv6 address
    std::wstring name;    // lower case

    [[nodiscard]] bool operator==(const HostEntry&) const = default;
};

// "a-z 0-9 - _", ≤ 32: what a section id may be.
[[nodiscard]] bool validHostsSection(std::wstring_view id);
[[nodiscard]] bool validHostName(std::wstring_view name);
[[nodiscard]] bool validHostAddress(std::wstring_view address);

// Entries of a hosts-format text (comments, blank lines, malformed and localhost lines are
// dropped; a line with several names gives one entry each; duplicates by name: the first wins).
[[nodiscard]] std::vector<HostEntry> parseHosts(std::wstring_view text);
// "address name" lines (CRLF), the value of a SetHosts operation.
[[nodiscard]] std::wstring formatHostEntries(const std::vector<HostEntry>& entries);

// The file with section `id` set to `entries` (empty: the section removed). Other sections and
// every other line stay where they are; a new section goes at the end.
[[nodiscard]] std::wstring setHostsSection(std::wstring_view file, std::wstring_view id, std::wstring_view entries);
// The WinLove sections of a hosts file: id → entry lines as written.
[[nodiscard]] std::map<std::wstring, std::wstring> hostsSections(std::wstring_view file);

[[nodiscard]] std::filesystem::path hostsPath(const std::filesystem::path& mountDir);
// Reads, changes one section and writes the image's hosts file (ANSI / UTF-8 text, CRLF).
[[nodiscard]] Result<void> applyHostsSection(const std::filesystem::path& mountDir, std::wstring_view id,
                                             std::wstring_view entries);
[[nodiscard]] std::map<std::wstring, std::wstring> readHostsSections(const std::filesystem::path& mountDir);

} // namespace wl::core
