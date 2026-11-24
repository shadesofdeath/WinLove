#include "core/image/MediaDrivers.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/WimFile.h"

#include <algorithm>
#include <format>
#include <map>

namespace wl::core {

namespace {

constexpr std::wstring_view kInfFolder = L"Windows\\INF";
constexpr std::wstring_view kRepository = L"Windows\\System32\\DriverStore\\FileRepository";

bool isPublishedInf(const std::wstring& name) {
    const std::wstring lower = text::lower(name);
    return lower.size() > 7 && lower.starts_with(L"oem") && lower.ends_with(L".inf") &&
           std::all_of(lower.begin() + 3, lower.end() - 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

std::wstring hashKey(const std::array<std::uint8_t, 20>& hash) {
    std::wstring key;
    for (const auto b : hash) {
        key += std::format(L"{:02x}", b);
    }
    return key;
}

} // namespace

std::vector<WimDriver> thirdPartyDrivers(const WimEdition& edition) {
    std::vector<WimDriver> out;
    std::vector<std::wstring> published;
    for (auto& name : edition.names(kInfFolder)) {
        if (isPublishedInf(name)) {
            published.push_back(std::move(name));
        }
    }
    if (published.empty()) {
        return out; // the usual case: one listing, nothing else read
    }
    // The driver store's INF of each package folder, by content: "pvscsi.inf_amd64_<hash>" holds
    // "pvscsi.inf". Hundreds of inbox folders, all in the file list already in memory.
    std::map<std::wstring, std::wstring> originalByHash;
    for (const auto& folder : edition.names(kRepository)) {
        const auto dot = text::lower(folder).find(L".inf_");
        if (dot == std::wstring::npos) {
            continue;
        }
        const std::wstring inf = folder.substr(0, dot + 4);
        if (const auto hash = edition.hash(std::wstring(kRepository) + L"\\" + folder + L"\\" + inf)) {
            originalByHash.emplace(hashKey(*hash), inf);
        }
    }
    std::ranges::sort(published, [](const std::wstring& a, const std::wstring& b) {
        return _wtoi(a.c_str() + 3) < _wtoi(b.c_str() + 3); // oem2 before oem10
    });
    for (const auto& name : published) {
        const std::wstring path = std::wstring(kInfFolder) + L"\\" + name;
        WimDriver driver;
        driver.publishedName = name;
        if (const auto hash = edition.hash(path)) {
            if (const auto it = originalByHash.find(hashKey(*hash)); it != originalByHash.end()) {
                driver.originalName = it->second;
            }
        }
        auto bytes = edition.data(path);
        if (!bytes) {
            log::warn("drivers", L"cannot read " + path + L": " + describe(bytes.error()));
        }
        const std::string_view raw = bytes ? std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size())
                                           : std::string_view();
        driver.inf = parseInfText(utf8::decodeText(raw), driver.name());
        out.push_back(std::move(driver));
    }
    return out;
}

Result<MediaDrivers> readMediaDrivers(const SourceInfo& source) {
    MediaDrivers media;
    if (source.boot && !source.boot->images.empty()) {
        const auto& boot = *source.boot;
        media.bootIndex = boot.header.bootIndex > 0 ? static_cast<int>(boot.header.bootIndex) : boot.images.back().index;
        for (const auto& image : boot.images) {
            if (image.index == media.bootIndex) {
                media.architecture = image.architecture;
            }
        }
        auto bytes = openBootImage(source);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto edition = WimEdition::open(**bytes, media.bootIndex);
        if (!edition) {
            return std::unexpected(edition.error());
        }
        media.boot = thirdPartyDrivers(*edition);
    }
    if (media.architecture == Architecture::Unknown && !source.install.images.empty()) {
        media.architecture = source.install.images.front().architecture;
    }
    // The install image: an ESD (solid LZMS) has no file list this reader can open — then nothing
    // is known about it, which is not the same as "no drivers".
    if (!source.install.header.solid && source.install.header.compression != WimCompression::Lzms) {
        auto bytes = openInstallImage(source);
        if (bytes) {
            media.installRead = true;
            for (const auto& image : source.install.images) {
                auto edition = WimEdition::open(**bytes, image.index);
                if (!edition) {
                    log::warn("drivers", std::format(L"install image edition {}: {}", image.index, describe(edition.error())));
                    media.installRead = false;
                    continue;
                }
                for (auto& driver : thirdPartyDrivers(*edition)) {
                    if (!containsDriver(media.install, driver.inf)) {
                        media.install.push_back(std::move(driver));
                    }
                }
            }
        } else {
            log::warn("drivers", L"cannot open the install image: " + describe(bytes.error()));
        }
    }
    log::info("drivers", std::format(L"setup media drivers: boot.wim [{}] {}, install image {}{}", media.bootIndex, media.boot.size(),
                                     media.install.size(), media.installRead ? L"" : L" (not read)"));
    return media;
}

bool containsDriver(const std::vector<WimDriver>& in, const DriverInf& inf) {
    const std::wstring name = inf.path.filename().wstring();
    return std::ranges::any_of(in, [&](const WimDriver& d) {
        if (d.inf.version != inf.version) {
            return false;
        }
        if (text::iequals(d.inf.path.filename().wstring(), name)) {
            return true;
        }
        // Its original name unknown: the same catalog file is the same package.
        return d.originalName.empty() && !inf.catalog.empty() && text::iequals(d.inf.catalog, inf.catalog);
    });
}

} // namespace wl::core
