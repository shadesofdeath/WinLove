#include "core/image/Source.h"

#include "base/Log.h"

#include <format>

namespace wl::core {

namespace {

constexpr const wchar_t* kInstallCandidates[] = {L"sources/install.wim", L"sources/install.esd",
                                                 L"sources/install.swm"};

} // namespace

Result<SourceInfo> openSource(const std::filesystem::path& path) {
    SourceInfo info;
    info.path = path;
    info.format = formatFromPath(path.wstring());

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

} // namespace wl::core
