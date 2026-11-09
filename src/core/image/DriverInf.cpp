#include "core/image/DriverInf.h"

#include "base/File.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/system/Files.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

namespace wl::core {

namespace {

std::wstring trim(std::wstring_view s) {
    return std::wstring(text::trim(s));
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
    std::vector<std::pair<std::wstring, std::wstring>> lineKeys; // (section, key as written), in order
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
            section = text::lower(trim(std::wstring_view(t).substr(1, t.size() - 2)));
            continue;
        }
        const auto eq = t.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        const std::wstring key = text::lower(trim(std::wstring_view(t).substr(0, eq)));
        const std::wstring value = trim(std::wstring_view(t).substr(eq + 1));
        sections[section][key] = value;
        lineKeys.emplace_back(section, trim(std::wstring_view(t).substr(0, eq)));
        if (section == L"manufacturer") {
            manufacturerValues.push_back(value);
        }
    }
    const auto& strings = sections[L"strings"];
    auto resolve = [&](std::wstring value) {
        value = unquote(trim(value));
        if (value.size() > 2 && value.front() == L'%' && value.back() == L'%') {
            const auto it = strings.find(text::lower(value.substr(1, value.size() - 2)));
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
            const std::wstring decoration = text::lower(trim(part));
            for (const wchar_t* arch : {L"amd64", L"arm64", L"x86"}) {
                if (decoration.starts_with(std::wstring(L"nt") + arch) &&
                    std::ranges::find(inf.architectures, arch) == inf.architectures.end()) {
                    inf.architectures.emplace_back(arch);
                }
            }
        }
    }
    // D-112: the device names — the keys of the models sections (base name and every decorated one).
    std::vector<std::pair<std::wstring, std::wstring>> models; // section → decoration
    for (const auto& value : manufacturerValues) {
        std::wstringstream parts(value);
        std::wstring base;
        std::getline(parts, base, L',');
        base = text::lower(trim(base));
        if (base.empty()) {
            continue;
        }
        models.emplace_back(base, L"");
        std::wstring part;
        while (std::getline(parts, part, L',')) {
            const std::wstring decoration = text::lower(trim(part));
            models.emplace_back(base + L"." + decoration, decoration);
        }
    }
    for (const auto& [name, key] : lineKeys) {
        const auto model = std::ranges::find(models, name, &std::pair<std::wstring, std::wstring>::first);
        if (model == models.end()) {
            continue;
        }
        std::wstring device = resolve(key);
        if (device.empty()) {
            continue;
        }
        inf.models.emplace_back(model->second, device);
        if (std::ranges::find(inf.devices, device) == inf.devices.end()) {
            inf.devices.push_back(std::move(device));
        }
    }
    // Models sections without an NTamd64/NTarm64 decoration are only used on x86 Windows
    // (64-bit Windows requires the decoration).
    if (!manufacturerValues.empty() && inf.architectures.empty()) {
        inf.architectures.emplace_back(L"x86");
    }
    return inf;
}

std::vector<std::wstring> DriverInf::devicesFor(std::wstring_view arch) const {
    // 64-bit Windows reads only the models sections decorated for it; x86 also the undecorated ones.
    const std::wstring want = L"nt" + std::wstring(arch);
    std::vector<std::wstring> out;
    for (const auto& [decoration, name] : models) {
        const bool fits = decoration.starts_with(want) || (arch == L"x86" && (decoration.empty() || decoration == L"nt" ||
                                                                              decoration.starts_with(L"nt.")));
        if (fits && std::ranges::find(out, name) == out.end()) {
            out.push_back(name);
        }
    }
    return out;
}

DriverInf parseInf(const std::filesystem::path& path) {
    return parseInfText(utf8::decodeText(readFileBytes(path).value_or(std::string())), path);
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
        f.first += treeBytes(it->path()); // an unreadable size counts 0, not uintmax_t(-1)
        if (text::lower(it->path().extension().wstring()) == L".inf") {
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
