#include "core/uup/UupApps.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/AppxInstall.h"
#include "core/system/Process.h"
#include "core/uup/UupDownload.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <map>
#include <set>

namespace wl::core::uup {

namespace {

// Not on N editions: the media apps and the codecs.
constexpr std::array kMediaApps{
    L"microsoft.zunemusic", L"microsoft.zunevideo", L"microsoft.yourphone", L"microsoft.windowssoundrecorder",
    L"microsoft.gamingapp", L"microsoft.xboxgamingoverlay", L"microsoft.xbox.tcui", L"microsoft.webmediaextensions",
    L"microsoft.rawimageextension", L"microsoft.heifimageextension", L"microsoft.hevcvideoextension",
    L"microsoft.vp9videoextensions", L"microsoft.webpimageextension", L"microsoft.dolbyaudioextensions",
    L"microsoft.avcencodervideoextension", L"microsoft.mpeg2videoextension", L"microsoft.av1videoextension",
};
// Surface Hub (Windows Team, edition id PPIPro) only.
constexpr std::array kTeamApps{
    L"microsoft.whiteboard", L"microsoft.microsoftskydrive", L"microsoft.microsoftteamsforsurfacehub",
    L"microsoftcorporationii.mailforsurfacehub", L"microsoft.microsoftpowerbiforwindows", L"microsoft.skypeapp",
    L"microsoft.office.excel", L"microsoft.office.powerpoint", L"microsoft.office.word",
};

std::wstring nameOf(std::wstring_view id) {
    return text::lower(id.substr(0, id.find(L'_')));
}

std::wstring wide(const char* s) {
    return utf8::toWide(s ? s : "");
}

// Base64 → lower-case hex (the database's PayloadHash is a base64 SHA-256).
std::wstring base64ToHex(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> bytes;
    int bits = 0;
    std::uint32_t buffer = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0) {
            continue; // '=' padding, whitespace
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring hex;
    for (const auto b : bytes) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
    }
    return hex;
}

Result<void> expand(const std::filesystem::path& cab, const std::filesystem::path& folder, std::wstring_view files) {
    auto tool = systemTool(L"expand.exe");
    if (!tool) {
        return std::unexpected(tool.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto run = runProcess(std::format(L"\"{}\" \"{}\" -F:{} \"{}\"", *tool, cab.wstring(), files, folder.wstring()),
                          [](std::string_view) {});
    if (!run) {
        return std::unexpected(run.error());
    }
    if (*run != 0) {
        return fail(ErrorCode::IoError, std::format(L"expand.exe could not unpack it (exit {})", *run), cab.wstring());
    }
    return {};
}

} // namespace

std::wstring appKey(std::wstring_view appId) {
    std::wstring name = text::lower(appId.substr(0, appId.find(L'_')));
    for (const wchar_t* prefix : {L"microsoftcorporationii.", L"microsoftwindows.client.", L"microsoftwindows.",
                                  L"microsoft.windows.", L"microsoft.", L"clipchamp."}) {
        if (name.starts_with(prefix)) {
            name = name.substr(std::wstring_view(prefix).size());
            break;
        }
    }
    std::erase(name, L'.');
    return name;
}

AppGroup appGroup(std::wstring_view appId) {
    static constexpr std::array kEssential{L"windowsstore", L"storepurchaseapp", L"desktopappinstaller", L"sechealthui",
                                           L"windowsnotepad", L"windowsterminal", L"windowscalculator", L"photos",
                                           L"paint", L"screensketch", L"windowscamera", L"windowsalarms",
                                           L"windowssoundrecorder", L"microsoftstickynotes"};
    static constexpr std::array kMedia{L"zunemusic", L"zunevideo", L"yourphone", L"gamingapp", L"xboxgamingoverlay",
                                       L"xboxtcui", L"xboxidentityprovider", L"xboxspeechtotextoverlay",
                                       L"microsoftsolitairecollection", L"clipchamp"};
    const std::wstring key = appKey(appId);
    if (std::ranges::any_of(kEssential, [&](const wchar_t* k) { return key == k; })) {
        return AppGroup::Essential;
    }
    if (std::ranges::any_of(kMedia, [&](const wchar_t* k) { return key == k; })) {
        return AppGroup::Media;
    }
    if (key.ends_with(L"extension") || key.ends_with(L"extensions")) {
        return AppGroup::Codecs;
    }
    return AppGroup::Other;
}

bool appRequired(std::wstring_view appId) {
    const std::wstring key = appKey(appId);
    return key == L"sechealthui" || key == L"desktopappinstaller";
}

void excludeApps(std::vector<AppFeature>& plan, const std::vector<std::wstring>& excluded) {
    std::erase_if(plan, [&](const AppFeature& f) {
        return !f.framework && !appRequired(f.id) &&
               std::ranges::any_of(excluded, [&](const std::wstring& e) { return text::iequals(e, f.id); });
    });
}

Result<std::vector<AppFeature>> parseAppCompDb(std::string_view xml) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return fail(ErrorCode::ParseError, L"the app database is not XML");
    }
    const auto root = doc.child("CompDB");
    if (!root) {
        return fail(ErrorCode::ParseError, L"the app database has no CompDB");
    }
    // The payload of every package: its file and hash.
    struct Payload {
        std::wstring fileName, sha256;
        std::uint64_t size = 0;
    };
    std::map<std::wstring, Payload> payloads;
    for (const auto& package : root.child("Packages").children("Package")) {
        const auto item = package.child("Payload").child("PayloadItem");
        if (!item) {
            continue;
        }
        std::wstring path = wide(item.attribute("Path").as_string());
        const auto slash = path.find_last_of(L"\\/");
        Payload p;
        p.fileName = slash == std::wstring::npos ? path : path.substr(slash + 1);
        p.sha256 = base64ToHex(item.attribute("PayloadHash").as_string());
        p.size = item.attribute("PayloadSize").as_ullong();
        payloads[wide(package.attribute("ID").as_string())] = std::move(p);
    }
    std::vector<AppFeature> features;
    for (const auto& f : root.child("Features").children("Feature")) {
        AppFeature feature;
        feature.id = wide(f.attribute("FeatureID").as_string());
        const std::string type = f.attribute("Type").as_string();
        feature.framework = type == "MSIXFramework";
        for (const auto& dep : f.child("Dependencies").children("Feature")) {
            feature.dependencies.push_back(wide(dep.attribute("FeatureID").as_string()));
        }
        for (const auto& info : f.child("CustomInformation").children("CustomInfo")) {
            if (std::string_view(info.attribute("Key").as_string()) == "licensedata") {
                feature.license = info.child_value();
            }
        }
        for (const auto& package : f.child("Packages").children("Package")) {
            AppPackage p;
            p.id = wide(package.attribute("ID").as_string());
            const std::string kind = package.attribute("PackageType").as_string();
            p.bundle = kind == "MSIXBundlePackage" || kind == "AppxBundlePackage";
            if (const auto it = payloads.find(p.id); it != payloads.end()) {
                p.fileName = it->second.fileName;
                p.sha256 = it->second.sha256;
                p.size = it->second.size;
            }
            feature.packages.push_back(std::move(p));
        }
        if (!feature.id.empty()) {
            features.push_back(std::move(feature));
        }
    }
    return features;
}

bool appForEdition(std::wstring_view appId, std::wstring_view editionId) {
    const std::wstring name = nameOf(appId);
    const std::wstring edition = text::lower(editionId);
    const bool team = edition == L"ppipro";
    if (std::ranges::any_of(kTeamApps, [&](const wchar_t* t) { return name == t; })) {
        return team;
    }
    // N editions: CoreN, ProfessionalN, EducationN, EnterpriseN, ProfessionalWorkstationN …
    const bool n = !editionId.empty() && editionId.back() == L'N';
    if (n && std::ranges::any_of(kMediaApps, [&](const wchar_t* m) { return name == m; })) {
        return false;
    }
    return true;
}

bool packageForArchitecture(std::wstring_view packageId, std::wstring_view architecture) {
    // Name_Version_Architecture_ResourceId_PublisherId
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= packageId.size(); ++i) {
        if (i == packageId.size() || packageId[i] == L'_') {
            parts.push_back(packageId.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 3) {
        return true;
    }
    const std::wstring arch = text::lower(parts[2]);
    const std::wstring image = text::lower(architecture);
    if (arch == L"neutral") {
        return true;
    }
    if (image == L"amd64" || image == L"x64") {
        return arch == L"x64" || arch == L"x86";
    }
    if (image == L"arm64") {
        return arch == L"arm64" || arch == L"x64" || arch == L"x86";
    }
    return arch == L"x86";
}

std::vector<AppFeature> appsFor(const std::vector<AppFeature>& all, const std::vector<std::wstring>& editions,
                                std::wstring_view architecture) {
    std::set<std::wstring> wanted;
    for (const auto& f : all) {
        if (f.framework) {
            continue;
        }
        if (std::ranges::any_of(editions, [&](const std::wstring& e) { return appForEdition(f.id, e); })) {
            wanted.insert(f.id);
            for (const auto& d : f.dependencies) {
                wanted.insert(d);
            }
        }
    }
    std::vector<AppFeature> out;
    for (const auto& f : all) {
        if (!wanted.contains(f.id)) {
            continue;
        }
        AppFeature trimmed = f;
        std::erase_if(trimmed.packages, [&](const AppPackage& p) { return !packageForArchitecture(p.id, architecture); });
        if (!trimmed.packages.empty()) {
            out.push_back(std::move(trimmed));
        }
    }
    // Frameworks first: the apps need them.
    std::ranges::stable_partition(out, [](const AppFeature& f) { return f.framework; });
    return out;
}

std::wstring appFolderName(const AppFeature& app) {
    return app.framework ? std::wstring(L"Frameworks") : app.id;
}

std::vector<File> appFiles(const std::vector<AppFeature>& apps, const std::vector<File>& available,
                           std::vector<std::wstring>* missing) {
    std::map<std::wstring, const File*> byHash;
    for (const auto& f : available) {
        if (!f.sha256.empty()) {
            byHash[f.sha256] = &f;
        }
    }
    std::vector<File> files;
    std::set<std::wstring> seen;
    for (const auto& app : apps) {
        for (const auto& p : app.packages) {
            const auto it = byHash.find(p.sha256);
            if (p.sha256.empty() || p.fileName.empty() || it == byHash.end()) {
                if (missing) {
                    missing->push_back(p.id);
                }
                continue;
            }
            File f = *it->second;
            f.name = appFolderName(app) + L"\\" + p.fileName;
            f.kind = FileKind::App;
            if (seen.insert(text::lower(f.name)).second) {
                files.push_back(std::move(f));
            }
        }
    }
    return files;
}

Result<std::vector<AppFeature>> readAppCompDb(const std::filesystem::path& cab, const std::filesystem::path& scratch) {
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    constexpr wchar_t kName[] = L"DesktopTargetCompDB_App_Neutral.xml.cab";
    if (auto r = expand(cab, scratch / L"outer", kName); !r) {
        return std::unexpected(r.error());
    }
    std::filesystem::path inner;
    for (const auto& e : std::filesystem::directory_iterator(scratch / L"outer", ec)) {
        if (text::iequals(e.path().filename().wstring(), kName)) {
            inner = e.path();
        }
    }
    if (inner.empty()) {
        return fail(ErrorCode::NotFound, L"the set has no app database", cab.wstring());
    }
    if (auto r = expand(inner, scratch / L"inner", L"*"); !r) {
        return std::unexpected(r.error());
    }
    std::filesystem::path xml;
    for (const auto& e : std::filesystem::directory_iterator(scratch / L"inner", ec)) {
        if (e.is_regular_file(ec)) {
            xml = e.path();
        }
    }
    std::ifstream in(xml, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = parseAppCompDb(text);
    std::filesystem::remove_all(scratch, ec);
    return parsed;
}

Result<void> writeLicenses(const std::vector<AppFeature>& apps, const std::filesystem::path& appsFolder) {
    for (const auto& app : apps) {
        if (app.framework || app.license.empty()) {
            continue;
        }
        const auto folder = appsFolder / appFolderName(app);
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        std::ofstream out(folder / L"License.xml", std::ios::binary | std::ios::trunc);
        out.write(app.license.data(), static_cast<std::streamsize>(app.license.size()));
        if (!out) {
            return fail(ErrorCode::IoError, L"cannot write the app's licence", (folder / L"License.xml").wstring());
        }
    }
    return {};
}

std::filesystem::path aggregatedMetadata(const std::filesystem::path& folder) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (e.is_regular_file(ec) && text::iendsWith(e.path().filename().wstring(), L".AggregatedMetadata.cab")) {
            return e.path();
        }
    }
    return {};
}

Result<AppDownload> planAppDownload(std::wstring_view id, const std::filesystem::path& setFolder,
                                   const std::vector<std::wstring>& editions, std::wstring_view architecture,
                                   const CancelToken& cancel, const std::vector<std::wstring>& excluded) {
    const auto cab = aggregatedMetadata(setFolder);
    if (cab.empty()) {
        return fail(ErrorCode::NotFound, L"the set has no AggregatedMetadata.cab (no app database)", setFolder.wstring());
    }
    auto all = readAppCompDb(cab, setFolder / L"appdb.tmp");
    if (!all) {
        return std::unexpected(all.error());
    }
    AppDownload out;
    out.plan = appsFor(*all, editions, architecture);
    excludeApps(out.plan, excluded);
    const std::vector<std::wstring> appEdition{L"app"};
    auto set = listFiles(id, L"neutral", appEdition, /*links=*/true, cancel);
    if (!set) {
        return std::unexpected(set.error());
    }
    out.files = appFiles(out.plan, set->files, &out.missing);
    for (auto& f : out.files) {
        f.name = L"apps\\" + f.name;
    }
    for (const auto& m : out.missing) {
        log::info("uup", L"app package not in the set (left out): " + m);
    }
    if (auto r = writeLicenses(out.plan, setFolder / L"apps"); !r) {
        return std::unexpected(r.error());
    }
    return out;
}

Result<std::vector<AppFeature>> setApps(std::wstring_view id, std::wstring_view language,
                                        const std::vector<std::wstring>& editions, std::wstring_view architecture,
                                        const std::filesystem::path& setFolder, const CancelToken& cancel) {
    if (aggregatedMetadata(setFolder).empty()) {
        auto set = listFiles(id, language, editions, /*links=*/true, cancel);
        if (!set) {
            return std::unexpected(set.error());
        }
        std::vector<File> meta;
        for (const auto& f : set->files) {
            if (text::iendsWith(f.name, L".AggregatedMetadata.cab")) {
                meta.push_back(f);
            }
        }
        if (meta.empty()) {
            return fail(ErrorCode::NotFound, L"this build has no app database (no Store apps to pick)", std::wstring(id));
        }
        if (auto r = downloadFiles(meta, setFolder, {}, TaskContext{cancel, {}}); !r) {
            return std::unexpected(r.error());
        }
    }
    auto all = readAppCompDb(aggregatedMetadata(setFolder), setFolder / L"appdb.tmp");
    if (!all) {
        return std::unexpected(all.error());
    }
    auto plan = appsFor(*all, editions, architecture);
    std::erase_if(plan, [](const AppFeature& f) { return f.framework; });
    return plan;
}

Result<std::size_t> downloadApps(std::wstring_view id, const std::filesystem::path& setFolder,
                                 const std::vector<std::wstring>& editions, std::wstring_view architecture,
                                 const TaskContext& task) {
    auto planned = planAppDownload(id, setFolder, editions, architecture, task.cancel);
    if (!planned) {
        return std::unexpected(planned.error());
    }
    auto refresh = [&]() -> Result<std::vector<File>> {
        auto again = planAppDownload(id, setFolder, editions, architecture, task.cancel);
        if (!again) {
            return std::unexpected(again.error());
        }
        return std::move(again->files);
    };
    if (auto r = downloadFiles(planned->files, setFolder, refresh, task); !r) {
        return std::unexpected(r.error());
    }
    return static_cast<std::size_t>(std::ranges::count_if(planned->plan, [](const AppFeature& a) { return !a.framework; }));
}

std::vector<std::wstring> provisionApps(DismSession& session, const std::vector<AppFeature>& apps,
                                        const std::filesystem::path& appsFolder, const TaskContext& task) {
    std::vector<std::wstring> warnings;
    std::error_code ec;
    const double n = static_cast<double>(std::max<std::size_t>(apps.size(), 1));
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (task.cancel.cancelled()) {
            break;
        }
        task.report(static_cast<double>(i) / n, L"apps");
        const auto& app = apps[i];
        const auto folder = appsFolder / appFolderName(app);
        const TaskContext quiet{task.cancel, {}};
        if (app.framework) {
            for (const auto& p : app.packages) {
                const auto file = folder / p.fileName;
                if (p.fileName.empty() || !std::filesystem::is_regular_file(file, ec)) {
                    continue;
                }
                AppxInstall install;
                install.package = file;
                install.framework = true;
                if (auto r = provisionAppx(session, install, quiet); !r) {
                    log::warn("uup", std::format(L"framework {}: {}", p.fileName, describe(r.error())));
                }
            }
            continue;
        }
        // The bundle when there is one (it finds its packages next to it), else the single package.
        const auto main = std::ranges::find_if(app.packages, [](const AppPackage& p) { return p.bundle; });
        const AppPackage* chosen = main != app.packages.end() ? &*main : app.packages.empty() ? nullptr : &app.packages.front();
        const auto file = chosen ? folder / chosen->fileName : std::filesystem::path();
        if (!chosen || chosen->fileName.empty() || !std::filesystem::is_regular_file(file, ec)) {
            warnings.push_back(app.id + L": not in the set");
            continue;
        }
        AppxInstall install;
        install.package = file;
        if (std::filesystem::is_regular_file(folder / L"License.xml", ec)) {
            install.license = folder / L"License.xml";
        }
        if (auto r = provisionAppx(session, install, quiet); !r) {
            log::warn("uup", std::format(L"app {}: {}", app.id, describe(r.error())));
            warnings.push_back(app.id + L": " + r.error().message);
        }
    }
    task.report(1.0, L"apps");
    return warnings;
}

} // namespace wl::core::uup
