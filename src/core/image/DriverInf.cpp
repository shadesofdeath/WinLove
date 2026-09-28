#include "core/image/DriverInf.h"

#include "base/Utf8.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

namespace wl::core {

namespace {

std::wstring lowered(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
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

std::wstring unquote(std::wstring value) {
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

bool DriverInf::supports(std::wstring_view arch) const {
    if (architectures.empty()) {
        return true;
    }
    const std::wstring want = arch == L"x64" ? L"amd64" : std::wstring(arch);
    return std::ranges::find(architectures, want) != architectures.end();
}

DriverInf parseInfText(const std::wstring& text, const std::filesystem::path& path) {
    DriverInf inf;
    inf.path = path;
    // section (lower-case) → key (lower-case) → raw value; plus [Manufacturer] values in order.
    std::map<std::wstring, std::map<std::wstring, std::wstring>> sections;
    std::vector<std::wstring> manufacturerValues;
    std::wstring section;
    std::wistringstream lines(text);
    std::wstring line;
    while (std::getline(lines, line)) {
        // Strip comments outside quotes.
        bool quoted = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == L'"') {
                quoted = !quoted;
            } else if (line[i] == L';' && !quoted) {
                line.resize(i);
                break;
            }
        }
        const std::wstring t = trim(line);
        if (t.empty()) {
            continue;
        }
        if (t.front() == L'[' && t.back() == L']') {
            section = lowered(trim(std::wstring_view(t).substr(1, t.size() - 2)));
            continue;
        }
        const auto eq = t.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        const std::wstring key = lowered(trim(std::wstring_view(t).substr(0, eq)));
        const std::wstring value = trim(std::wstring_view(t).substr(eq + 1));
        sections[section][key] = value;
        if (section == L"manufacturer") {
            manufacturerValues.push_back(value);
        }
    }
    const auto& strings = sections[L"strings"];
    auto resolve = [&](std::wstring value) {
        value = unquote(trim(value));
        if (value.size() > 2 && value.front() == L'%' && value.back() == L'%') {
            const auto it = strings.find(lowered(value.substr(1, value.size() - 2)));
            if (it != strings.end()) {
                return unquote(it->second);
            }
        }
        return value;
    };
    auto& version = sections[L"version"];
    inf.className = resolve(version[L"class"]);
    inf.classGuid = resolve(version[L"classguid"]);
    inf.provider = resolve(version[L"provider"]);
    inf.catalog = resolve(version.contains(L"catalogfile") ? version[L"catalogfile"] : version[L"catalogfile.ntamd64"]);
    // DriverVer = mm/dd/yyyy[,x.y.z.w]
    const std::wstring driverVer = resolve(version[L"driverver"]);
    const auto comma = driverVer.find(L',');
    inf.date = trim(driverVer.substr(0, comma));
    inf.version = comma == std::wstring::npos ? std::wstring() : trim(driverVer.substr(comma + 1));
    // %Mfg% = Models, NTamd64, NTarm64.10.0...
    for (const auto& value : manufacturerValues) {
        std::wstringstream parts(value);
        std::wstring part;
        std::getline(parts, part, L','); // models section name
        while (std::getline(parts, part, L',')) {
            const std::wstring decoration = lowered(trim(part));
            for (const wchar_t* arch : {L"amd64", L"arm64", L"x86"}) {
                if (decoration.starts_with(std::wstring(L"nt") + arch) &&
                    std::ranges::find(inf.architectures, arch) == inf.architectures.end()) {
                    inf.architectures.emplace_back(arch);
                }
            }
        }
    }
    return inf;
}

DriverInf parseInf(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseInfText(utf8::decodeText(buffer.str()), path);
}

std::vector<DriverInf> scanDrivers(const std::filesystem::path& folder) {
    std::vector<DriverInf> infs;
    std::map<std::filesystem::path, std::pair<std::uint64_t, int>> folders; // folder → (bytes of files, INF count)
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             folder, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        auto& f = folders[it->path().parent_path()];
        f.first += it->file_size(ec);
        if (lowered(it->path().extension().wstring()) == L".inf") {
            ++f.second;
            infs.push_back(parseInf(it->path()));
        }
    }
    for (auto& inf : infs) {
        const auto& f = folders[inf.path.parent_path()];
        inf.size = f.second > 0 ? f.first / static_cast<std::uint64_t>(f.second) : 0;
    }
    std::ranges::sort(infs, [](const DriverInf& a, const DriverInf& b) {
        const int c = _wcsicmp(a.className.c_str(), b.className.c_str());
        return c != 0 ? c < 0 : _wcsicmp(a.path.filename().c_str(), b.path.filename().c_str()) < 0;
    });
    return infs;
}

} // namespace wl::core
