#include "core/image/HostsFile.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <windows.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace wl::core {

namespace {

constexpr std::wstring_view kOpen = L"# >>> WinLove: ";
constexpr std::wstring_view kClose = L"# <<< WinLove: ";

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

std::vector<std::wstring> splitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(L'\n', at);
        if (end == std::wstring_view::npos) {
            end = text.size();
        }
        std::wstring line(text.substr(at, end - at));
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        at = end + 1;
    }
    return lines;
}

std::wstring trim(std::wstring_view text) {
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && std::iswspace(text[a])) {
        ++a;
    }
    while (b > a && std::iswspace(text[b - 1])) {
        --b;
    }
    return std::wstring(text.substr(a, b - a));
}

} // namespace

bool validHostsSection(std::wstring_view id) {
    return !id.empty() && id.size() <= 32 && std::ranges::all_of(id, [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
    });
}

bool validHostName(std::wstring_view name) {
    if (name.empty() || name.size() > 253 || name.front() == L'.' || name.back() == L'.' || name.front() == L'-') {
        return false;
    }
    bool dot = false;
    for (const wchar_t c : name) {
        const bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' ||
                        c == L'_' || c == L'.';
        if (!ok) {
            return false;
        }
        dot = dot || c == L'.';
    }
    return dot && name.find(L"..") == std::wstring_view::npos;
}

bool validHostAddress(std::wstring_view address) {
    const std::wstring text(address);
    IN_ADDR v4{};
    IN6_ADDR v6{};
    return InetPtonW(AF_INET, text.c_str(), &v4) == 1 || InetPtonW(AF_INET6, text.c_str(), &v6) == 1;
}

std::vector<HostEntry> parseHosts(std::wstring_view text) {
    std::vector<HostEntry> entries;
    std::set<std::wstring> seen;
    for (auto line : splitLines(text)) {
        if (const auto hash = line.find(L'#'); hash != std::wstring::npos) {
            line.erase(hash);
        }
        std::wistringstream words(line);
        std::wstring address;
        if (!(words >> address) || !validHostAddress(address)) {
            continue;
        }
        std::wstring name;
        while (words >> name) {
            name = lower(name);
            if (name == L"localhost" || name == L"localhost.localdomain" || name == L"local" || name == L"broadcasthost" ||
                name == L"0.0.0.0" || !validHostName(name) || !seen.insert(name).second) {
                continue;
            }
            entries.push_back({address, name});
        }
    }
    return entries;
}

std::wstring formatHostEntries(const std::vector<HostEntry>& entries) {
    std::wstring out;
    for (const auto& e : entries) {
        out += e.address + L" " + e.name + L"\r\n";
    }
    return out;
}

std::wstring setHostsSection(std::wstring_view file, std::wstring_view id, std::wstring_view entries) {
    const std::wstring open = std::wstring(kOpen) + std::wstring(id);
    const std::wstring close = std::wstring(kClose) + std::wstring(id);
    std::vector<std::wstring> out;
    std::size_t sectionAt = static_cast<std::size_t>(-1);
    bool inside = false;
    for (auto& line : splitLines(file)) {
        const std::wstring t = trim(line);
        if (!inside && t == open) {
            inside = true;
            sectionAt = out.size();
            continue;
        }
        if (inside) {
            if (t == close) {
                inside = false;
            }
            continue;
        }
        out.push_back(std::move(line));
    }
    while (!out.empty() && out.back().empty() && sectionAt != out.size()) {
        out.pop_back(); // trailing blank lines: the file ends with exactly one line break
    }
    std::vector<std::wstring> section;
    for (auto& line : splitLines(entries)) {
        if (!trim(line).empty()) {
            section.push_back(trim(line));
        }
    }
    if (!section.empty()) {
        section.insert(section.begin(), open);
        section.push_back(close);
        const std::size_t at = sectionAt <= out.size() ? sectionAt : out.size();
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), section.begin(), section.end());
    }
    std::wstring text;
    for (const auto& line : out) {
        text += line + L"\r\n";
    }
    return text;
}

std::map<std::wstring, std::wstring> hostsSections(std::wstring_view file) {
    std::map<std::wstring, std::wstring> sections;
    std::wstring current;
    bool inside = false;
    for (const auto& line : splitLines(file)) {
        const std::wstring t = trim(line);
        if (!inside && t.starts_with(kOpen)) {
            current = t.substr(kOpen.size());
            inside = validHostsSection(current);
            if (inside) {
                sections[current];
            }
            continue;
        }
        if (inside) {
            if (t == std::wstring(kClose) + current) {
                inside = false;
            } else if (!t.empty()) {
                sections[current] += t + L"\r\n";
            }
        }
    }
    return sections;
}

std::filesystem::path hostsPath(const std::filesystem::path& mountDir) {
    return mountDir / L"Windows" / L"System32" / L"drivers" / L"etc" / L"hosts";
}

namespace {

std::wstring readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string bytes = buffer.str();
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        bytes.erase(0, 3);
    }
    // Microsoft's file is ASCII; a user's may be ANSI: take it as UTF-8 when it is valid, else ANSI.
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (n > 0 || bytes.empty()) {
        return utf8::toWide(bytes);
    }
    const int m = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(m), L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), m);
    return wide;
}

} // namespace

Result<void> applyHostsSection(const std::filesystem::path& mountDir, std::wstring_view id, std::wstring_view entries) {
    if (!validHostsSection(id)) {
        return fail(ErrorCode::InvalidArgument, L"not a hosts section id", std::wstring(id));
    }
    // Only what parses is written: the value comes from presets and imported files.
    std::wstring clean;
    for (const auto& e : parseHosts(entries)) {
        clean += e.address + L" " + e.name + L"\r\n";
    }
    const auto file = hostsPath(mountDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(file.parent_path(), ec)) {
        return fail(ErrorCode::NotFound, L"the image has no drivers\\etc folder", file.parent_path().wstring());
    }
    const std::wstring text = setHostsSection(readText(file), id, clean);
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL); // a read-only hosts file is still ours to change
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::string bytes = utf8::fromWide(text);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return fail(ErrorCode::IoError, L"could not write the hosts file", file.wstring());
    }
    log::info("hosts", std::format(L"hosts section '{}': {} entries", id, parseHosts(clean).size()));
    return {};
}

std::map<std::wstring, std::wstring> readHostsSections(const std::filesystem::path& mountDir) {
    return hostsSections(readText(hostsPath(mountDir)));
}

} // namespace wl::core
