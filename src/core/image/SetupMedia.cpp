#include "core/image/SetupMedia.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "core/image/UdfImage.h"
#include "core/image/WimFile.h"
#include "core/image/wim/WimGapi.h"
#include "core/io/ByteSource.h"

#include <algorithm>
#include <format>
#include <optional>

namespace wl::core {

namespace {

constexpr int kFirstWindows11 = 22000;
constexpr int kNewSetup = 26100; // 24H2: the media that cannot install Windows 10 (measured, D-077)

// The image Setup boots from: the header's boot index, else the last one.
const ImageInfo* bootImage(const WimFile& wim) {
    if (wim.images.empty()) {
        return nullptr;
    }
    const auto it = std::ranges::find(wim.images, static_cast<int>(wim.header.bootIndex), &ImageInfo::index);
    return it != wim.images.end() ? &*it : &wim.images.back();
}

// What a media swap leaves in place: the install image and what the user put there.
bool keptInSources(const std::filesystem::path& name) {
    const std::wstring n = text::lower(name.wstring());
    return n == L"install.wim" || n == L"install.esd" || (n.starts_with(L"install") && n.ends_with(L".swm")) ||
           n == L"$oem$";
}

bool keptAtRoot(const std::filesystem::path& name) {
    return text::iequals(name.wstring(), L"autounattend.xml");
}

// Every file under `from` copied into `to`, except the install images of the other media.
Result<void> copyMedia(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(from, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        const auto relative = std::filesystem::relative(it->path(), from, ec);
        if (ec) {
            break;
        }
        const bool inSources = relative.has_parent_path() && text::iequals(relative.begin()->wstring(), L"sources") &&
                               std::distance(relative.begin(), relative.end()) == 2;
        if (inSources && keptInSources(relative.filename())) {
            continue; // the other media's install image never comes along
        }
        const auto target = to / relative;
        if (it->is_directory(ec)) {
            std::filesystem::create_directories(target, ec);
        } else {
            std::filesystem::create_directories(target.parent_path(), ec);
            std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            break;
        }
    }
    if (ec) {
        return fail(ErrorCode::IoError, L"could not copy the setup media",
                    std::format(L"{} → {} (error {})", from.wstring(), to.wstring(), ec.value()));
    }
    return {};
}

// A Media Creation Tool ESD: "Windows Setup Media" (the files) and two Windows PE images (PE,
// then Setup) that make boot.wim.
Result<void> stageFromEsd(const std::filesystem::path& esd, const std::filesystem::path& staging, const TaskContext& task) {
    auto file = DiskFile::open(nativePath(esd));
    if (!file) {
        return std::unexpected(file.error());
    }
    auto wim = readWim(**file);
    if (!wim) {
        return std::unexpected(wim.error());
    }
    int media = 0;
    std::vector<int> pe;
    for (const auto& image : wim->images) {
        if (media == 0 && text::iequals(image.name, L"Windows Setup Media")) {
            media = image.index;
        } else if (text::iequals(image.installationType, L"WindowsPE")) {
            pe.push_back(image.index);
        }
    }
    if (media == 0 || pe.size() != 2) {
        return fail(ErrorCode::Unsupported,
                    L"not a Media Creation Tool ESD (it needs \"Windows Setup Media\" and two Windows PE images)", esd.wstring());
    }
    const TaskContext apply{task.cancel, [&](double f, std::wstring_view s) { task.report(f * 0.2, s); }};
    if (auto r = applyImage(esd, media, staging, apply); !r) {
        return r;
    }
    const auto boot = staging / L"sources" / L"boot.wim";
    std::error_code ec;
    std::filesystem::remove(boot, ec);
    for (std::size_t i = 0; i < pe.size(); ++i) {
        const TaskContext one{task.cancel, [&](double f, std::wstring_view s) { task.report(0.2 + (static_cast<double>(i) + f) * 0.3, s); }};
        if (auto r = exportImage(esd, pe[i], boot, WimCompression::Lzx, one); !r) {
            return r;
        }
    }
    return setBootImage(boot, 2);
}

} // namespace

int setupMediaBuild(const SourceInfo& source) {
    if (!source.boot) {
        return 0;
    }
    const ImageInfo* image = bootImage(*source.boot);
    return image ? image->build : 0;
}

bool mediaOpensPreviousSetup(const SourceInfo& source) {
    const int build = setupMediaBuild(source);
    return build > 0 && build < kNewSetup;
}

std::vector<int> editionsMediaCannotInstall(const SourceInfo& source) {
    std::vector<int> out;
    if (setupMediaBuild(source) < kNewSetup) {
        return out;
    }
    for (const auto& image : source.install.images) {
        if (image.build > 0 && image.build < kFirstWindows11) {
            out.push_back(image.index);
        }
    }
    return out;
}

Result<void> replaceSetupMedia(const std::filesystem::path& setupFolder, const std::filesystem::path& from,
                               const TaskContext& task) {
    std::error_code ec;
    const auto sources = setupFolder / L"sources";
    auto installed = openSource(setupFolder);
    if (!installed || installed->format != ImageFormat::Folder) {
        return fail(ErrorCode::InvalidArgument, L"not a setup folder with an install image", setupFolder.wstring());
    }
    const auto staging = setupFolder.parent_path() / (setupFolder.filename().wstring() + L".media");
    std::filesystem::remove_all(staging, ec);
    std::filesystem::create_directories(staging, ec);
    auto cleanup = [&] { std::filesystem::remove_all(staging, ec); };

    // 1. The new media, complete, next to the folder.
    log::info("media", std::format(L"setup media of {} from {}", setupFolder.wstring(), from.wstring()));
    Result<void> staged;
    const std::wstring extension = text::lower(from.extension().wstring());
    if (std::filesystem::is_directory(from, ec)) {
        staged = copyMedia(from, staging);
    } else if (extension == L".iso") {
        auto iso = UdfImage::open(from);
        staged = iso ? iso->extractAll(staging, TaskContext{task.cancel, [&](double f, std::wstring_view s) { task.report(f * 0.8, s); }})
                     : Result<void>(std::unexpected(iso.error()));
    } else if (extension == L".esd" || extension == L".wim") {
        staged = stageFromEsd(from, staging, task);
    } else {
        staged = fail(ErrorCode::Unsupported, L"setup media comes from an ISO, a setup folder or a Media Creation Tool ESD",
                      from.wstring());
    }
    if (!staged) {
        cleanup();
        return staged;
    }
    if (task.cancel.cancelled()) {
        cleanup();
        return fail(ErrorCode::Cancelled, L"setup media swap cancelled", setupFolder.wstring());
    }

    // 2. It must be able to install the image: older than 24H2, the same architecture.
    std::optional<ImageInfo> boot;
    {
        // Closed again before the files move.
        auto bootFile = DiskFile::open(nativePath(staging / L"sources" / L"boot.wim"));
        if (bootFile) {
            if (auto bootWim = readWim(**bootFile); bootWim && bootImage(*bootWim)) {
                boot = *bootImage(*bootWim);
            }
        }
    }
    if (!boot || !std::filesystem::exists(staging / L"setup.exe", ec)) {
        cleanup();
        return fail(ErrorCode::Unsupported, L"no setup media there (sources\\boot.wim and setup.exe are needed)", from.wstring());
    }
    if (boot->build >= kNewSetup) {
        cleanup();
        return fail(ErrorCode::Unsupported,
                    std::format(L"this setup media is build {}: 24H2 and newer cannot install Windows 10 (D-077); use a Windows 10 one",
                                boot->build),
                    from.wstring());
    }
    const auto& images = installed->install.images;
    if (!images.empty() && images.front().architecture != Architecture::Unknown &&
        boot->architecture != images.front().architecture) {
        cleanup();
        return fail(ErrorCode::Unsupported,
                    std::format(L"the setup media is {}, the install image {}", architectureName(boot->architecture),
                                architectureName(images.front().architecture)),
                    from.wstring());
    }

    // 3. The old media goes, the install image and the user's additions stay; the new media comes in.
    task.report(0.85, L"swap");
    for (const auto& entry : std::filesystem::directory_iterator(setupFolder, ec)) {
        const auto name = entry.path().filename();
        if (text::iequals(name.wstring(), L"sources")) {
            for (const auto& inner : std::filesystem::directory_iterator(sources, ec)) {
                if (!keptInSources(inner.path().filename())) {
                    std::filesystem::remove_all(inner.path(), ec);
                }
            }
        } else if (!keptAtRoot(name)) {
            std::filesystem::remove_all(entry.path(), ec);
        }
        if (ec) {
            cleanup();
            return fail(ErrorCode::IoError, L"could not remove the old setup media", entry.path().wstring());
        }
    }
    auto copied = copyMedia(staging, setupFolder);
    cleanup();
    if (!copied) {
        return copied;
    }
    log::info("media", std::format(L"setup media is now build {} ({})", boot->build, from.filename().wstring()));
    task.report(1.0, L"swap");
    return {};
}

} // namespace wl::core
