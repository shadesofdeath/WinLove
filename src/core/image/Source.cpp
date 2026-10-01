#include "core/image/Source.h"

#include "base/Log.h"
#include "base/Path.h"

#include <format>

namespace wl::core {

namespace {

constexpr const wchar_t* kInstallCandidates[] = {L"sources/install.wim", L"sources/install.esd",
                                                 L"sources/install.swm"};

} // namespace

Result<SourceInfo> openSource(const std::filesystem::path& input) {
    const std::filesystem::path path = nativePath(input);
    SourceInfo info;
    info.path = path;
    std::error_code ec;
    info.format = std::filesystem::is_directory(path, ec) ? ImageFormat::Folder : formatFromPath(path.wstring());

    switch (info.format) {
    case ImageFormat::Iso: {
        auto iso = UdfImage::open(path);
        if (!iso) {
            return std::unexpected(iso.error());
        }
        info.volumeLabel = std::wstring(iso->volumeLabel());
        std::optional<UdfImage::Node> install;
        for (const auto* candidate : kInstallCandidates) {
            if (auto node = iso->find(candidate)) {
                info.installImage = candidate;
                install = std::move(*node);
                break;
            }
        }
        if (!install) {
            return fail(ErrorCode::NotFound, L"no sources/install.wim|esd|swm in ISO (not a Windows setup ISO?)",
                        path.wstring());
        }
        info.installImageSize = install->size;
        auto wim = readWim(*iso->openFile(*install));
        if (!wim) {
            return std::unexpected(wim.error());
        }
        info.install = std::move(*wim);
        if (auto boot = iso->find(L"sources/boot.wim")) {
            if (auto bootWim = readWim(*iso->openFile(*boot))) {
                info.boot = std::move(*bootWim);
            }
        }
        break;
    }
    case ImageFormat::Wim:
    case ImageFormat::Esd:
    case ImageFormat::Swm: {
        auto file = DiskFile::open(path);
        if (!file) {
            return std::unexpected(file.error());
        }
        info.installImage = path.filename().wstring();
        info.installImageSize = (*file)->size();
        auto wim = readWim(**file);
        if (!wim) {
            return std::unexpected(wim.error());
        }
        info.install = std::move(*wim);
        break;
    }
    case ImageFormat::Folder: {
        // Extracted setup media: <folder>\sources\install.wim|esd|swm
        std::optional<std::filesystem::path> found;
        for (const auto* candidate : kInstallCandidates) {
            const auto file = path / candidate;
            if (std::filesystem::is_regular_file(file, ec)) {
                info.installImage = candidate;
                found = file;
                break;
            }
        }
        if (!found) {
            return fail(ErrorCode::NotFound, L"folder has no sources\\install.wim|esd|swm", path.wstring());
        }
        auto file = DiskFile::open(*found);
        if (!file) {
            return std::unexpected(file.error());
        }
        info.installImageSize = (*file)->size();
        auto wim = readWim(**file);
        if (!wim) {
            return std::unexpected(wim.error());
        }
        info.install = std::move(*wim);
        if (auto boot = DiskFile::open(path / L"sources/boot.wim")) {
            if (auto bootWim = readWim(**boot)) {
                info.boot = std::move(*bootWim);
            }
        }
        break;
    }
    case ImageFormat::Vhd:
    case ImageFormat::Vhdx:
        return fail(ErrorCode::Unsupported, L"VHD/VHDX sources arrive with a later step", path.wstring());
    case ImageFormat::Unknown:
        return fail(ErrorCode::Unsupported, L"unknown source type (expected .iso .wim .esd .swm)", path.wstring());
    }
    log::info("source", std::format(L"opened {} ({}, {} images in {})", path.wstring(), formatName(info.format),
                                    info.install.images.size(), info.installImage));
    return info;
}

Result<std::shared_ptr<const ByteSource>> openInstallImage(const SourceInfo& source) {
    if (source.format == ImageFormat::Iso) {
        auto iso = UdfImage::open(source.path);
        if (!iso) {
            return std::unexpected(iso.error());
        }
        auto node = iso->find(source.installImage);
        if (!node) {
            return std::unexpected(node.error());
        }
        return iso->openFile(std::move(*node));
    }
    auto file = DiskFile::open(source.format == ImageFormat::Folder ? nativePath(source.path / source.installImage)
                                                                    : nativePath(source.path));
    if (!file) {
        return std::unexpected(file.error());
    }
    return std::shared_ptr<const ByteSource>(std::move(*file));
}

Result<std::filesystem::path> installImageFile(const SourceInfo& source, const std::filesystem::path& scratch, bool& extracted,
                                               const TaskContext& task) {
    extracted = false;
    switch (source.format) {
    case ImageFormat::Wim:
    case ImageFormat::Esd:
    case ImageFormat::Swm: return nativePath(source.path);
    case ImageFormat::Folder: return nativePath(source.path / source.installImage);
    case ImageFormat::Iso: {
        auto iso = UdfImage::open(source.path);
        if (!iso) {
            return std::unexpected(iso.error());
        }
        auto node = iso->find(source.installImage);
        if (!node) {
            return std::unexpected(node.error());
        }
        if (source.install.header.totalParts > 1) {
            return fail(ErrorCode::Unsupported, L"a split image inside an ISO: extract the ISO first", source.path.wstring());
        }
        std::error_code ec;
        std::filesystem::create_directories(scratch, ec);
        const auto file = scratch / std::filesystem::path(source.installImage).filename();
        if (auto r = iso->extract(*node, file, task); !r) {
            return std::unexpected(r.error());
        }
        extracted = true;
        return file;
    }
    default: return fail(ErrorCode::Unsupported, L"no install image to read", source.path.wstring());
    }
}

} // namespace wl::core
