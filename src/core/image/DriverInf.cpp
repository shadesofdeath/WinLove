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
    std::vector<std::wstring> lineValues;                        // the value of each of them
    std::wstring ifTypeValue;                                    // D-134: an AddReg line's "*IfType" data
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
            // D-134: "HKR, Ndi\Interfaces, ..." / "HKR,, *IfType, 0x00010001, 71" — the last field.
            if (ifTypeValue.empty() && text::lower(t).find(L"*iftype") != std::wstring::npos) {
                const auto comma = t.rfind(L',');
                if (comma != std::wstring::npos) {
                    ifTypeValue = trim(std::wstring_view(t).substr(comma + 1));
                }
            }
            continue;
        }
        const std::wstring key = text::lower(trim(std::wstring_view(t).substr(0, eq)));
        const std::wstring value = trim(std::wstring_view(t).substr(eq + 1));
        if (key == L"*iftype" && ifTypeValue.empty()) {
            ifTypeValue = value; // NetAdapterCx INFs: "*IfType = 71" in a section of its own
        }
        sections[section][key] = value;
        lineKeys.emplace_back(section, trim(std::wstring_view(t).substr(0, eq)));
        lineValues.push_back(value);
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
    // CatalogFile, else the first per-architecture one (CatalogFile.NTamd64 / .NTarm64 / .NTx86).
    for (const wchar_t* key : {L"catalogfile", L"catalogfile.ntamd64", L"catalogfile.ntarm64", L"catalogfile.ntx86"}) {
        if (const auto it = version.find(key); it != version.end() && !it->second.empty()) {
            inf.catalog = resolve(it->second);
            break;
        }
    }
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
    for (std::size_t at = 0; at < lineKeys.size(); ++at) {
        const auto& [name, key] = lineKeys[at];
        const auto model = std::ranges::find(models, name, &std::pair<std::wstring, std::wstring>::first);
        if (model == models.end()) {
            continue;
        }
        // D-134: "%Dev% = Install, PCI\VEN_…[, compatible IDs…]" — every ID after the install section.
        {
            std::wstringstream fields(lineValues[at]);
            std::wstring field;
            std::getline(fields, field, L','); // the install section
            while (std::getline(fields, field, L',')) {
                std::wstring id = unquote(trim(field));
                if (!id.empty() && std::ranges::none_of(inf.hardwareIds, [&](const std::wstring& known) {
                        return text::iequals(known, id);
                    })) {
                    inf.hardwareIds.push_back(std::move(id));
                }
            }
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
    if (!ifTypeValue.empty()) {
        const std::wstring number = resolve(ifTypeValue);
        inf.ifType = static_cast<int>(std::wcstol(number.c_str(), nullptr, 0)); // "71" or "0x47"
    }
    return inf;
}

SetupRole setupRole(const DriverInf& inf) {
    const std::wstring cls = text::lower(inf.className);
    if (cls == L"scsiadapter" || cls == L"hdc") {
        return SetupRole::Storage;
    }
    if (cls != L"net") {
        return SetupRole::None;
    }
    constexpr int kIfTypeWifi = 71; // IF_TYPE_IEEE80211
    if (inf.ifType == kIfTypeWifi) {
        return SetupRole::Wireless;
    }
    // A card Windows PE finds is enumerated by a bus; "*VMnetAdapter1", "ROOT\…", "{GUID}\…" are
    // adapters some software creates on the PC it runs on.
    static constexpr std::wstring_view kBuses[] = {L"pci\\", L"usb\\", L"sd\\", L"sdio\\", L"acpi\\", L"vmbus\\", L"pcmcia\\"};
    const bool onBus = std::ranges::any_of(inf.hardwareIds, [](const std::wstring& id) {
        const std::wstring lower = text::lower(id);
        return std::ranges::any_of(kBuses, [&](std::wstring_view bus) { return lower.starts_with(bus); });
    });
    if (!inf.hardwareIds.empty() && !onBus) {
        return SetupRole::Virtual;
    }
    return SetupRole::Network;
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
