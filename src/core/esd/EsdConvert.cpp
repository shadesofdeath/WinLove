#include "core/esd/EsdConvert.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "core/image/SetupMedia.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/DismExe.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"
#include "core/io/ByteSource.h"
#include "core/iso/IsoBuilder.h"

#include <algorithm>
#include <format>

namespace wl::core::esd {

namespace {

TaskContext phase(const TaskContext& task, double from, double to, const wchar_t* key) {
    return TaskContext{task.cancel, [&task, from, to, key](double f, std::wstring_view) {
                           task.report(from + (to - from) * std::clamp(f, 0.0, 1.0), key);
                       }};
}

Result<std::vector<ImageInfo>> imagesOf(const std::filesystem::path& wim) {
    auto file = DiskFile::open(nativePath(wim));
    if (!file) {
        return std::unexpected(file.error());
    }
    auto parsed = readWim(**file);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return parsed->images;
}

} // namespace

Result<Layout> esdLayout(std::span<const ImageInfo> images) {
    Layout layout;
    for (const auto& image : images) {
        if (layout.media == 0 && text::iequals(image.name, L"Windows Setup Media")) {
            layout.media = image.index;
        } else if (text::iequals(image.installationType, L"WindowsPE")) {
            layout.windowsPe.push_back(image.index);
        } else if (isWindowsEdition(image) && !image.editionId.empty()) {
            EsdEdition e;
            e.index = image.index;
            e.name = image.name;
            e.description = image.description;
            e.editionId = image.editionId;
            e.language = image.defaultLanguage.empty() && !image.languages.empty() ? image.languages.front() : image.defaultLanguage;
            e.architecture = architectureName(image.architecture);
            layout.editions.push_back(std::move(e));
        }
    }
    if (layout.media == 0 || layout.windowsPe.size() != 2 || layout.editions.empty()) {
        return fail(ErrorCode::Unsupported,
                    std::format(L"not a Media Creation Tool ESD (setup media {}, Windows PE images {}, editions {})",
                                layout.media, layout.windowsPe.size(), layout.editions.size()));
    }
    return layout;
}

Result<Layout> readLayout(const std::filesystem::path& esd) {
    auto images = imagesOf(esd);
    if (!images) {
        return std::unexpected(images.error());
    }
    auto layout = esdLayout(*images);
    if (!layout) {
        return fail(layout.error().code, layout.error().message, esd.wstring());
    }
    return layout;
}

std::vector<Selected> selectEditions(std::span<const std::pair<std::filesystem::path, Layout>> esds,
                                     std::span<const std::wstring> wanted, std::vector<std::wstring>* missing) {
    std::vector<Selected> out;
    if (wanted.empty()) {
        if (!esds.empty()) {
            for (const auto& e : esds.front().second.editions) {
                out.push_back({esds.front().first, e});
            }
        }
        return out;
    }
    std::vector<bool> found(wanted.size(), false);
    for (const auto& [path, layout] : esds) {
        for (const auto& e : layout.editions) {
            for (std::size_t w = 0; w < wanted.size(); ++w) {
                if (!found[w] && text::iequals(e.editionId, wanted[w])) {
                    found[w] = true;
                    out.push_back({path, e});
                    break;
                }
            }
        }
    }
    if (missing) {
        for (std::size_t w = 0; w < wanted.size(); ++w) {
            if (!found[w]) {
                missing->push_back(wanted[w]);
            }
        }
    }
    return out;
}

Result<uup::ConvertResult> convertEsd(Dism& dism, const ConvertOptions& options, const TaskContext& task) {
    if (options.esds.empty()) {
        return fail(ErrorCode::InvalidArgument, L"no ESD to convert");
    }
    std::vector<std::pair<std::filesystem::path, Layout>> esds;
    for (const auto& esd : options.esds) {
        auto layout = readLayout(esd);
        if (!layout) {
            return std::unexpected(layout.error());
        }
        esds.emplace_back(nativePath(esd), std::move(*layout));
    }
    std::vector<std::wstring> missing;
    const auto selected = selectEditions(esds, options.editions, &missing);
    if (!missing.empty()) {
        std::wstring names;
        for (const auto& m : missing) {
            names += (names.empty() ? L"" : L", ") + m;
        }
        return fail(ErrorCode::NotFound, L"the ESD does not have the edition(s): " + names, options.esds.front().wstring());
    }
    if (selected.empty()) {
        return fail(ErrorCode::NotFound, L"no edition to export", options.esds.front().wstring());
    }
    const auto work = nativePath(options.workFolder);
    const auto media = nativePath(options.mediaFolder);
    const auto mount = work / L"mount";
    std::error_code ec;
    std::filesystem::create_directories(work, ec);
    // An earlier run's mount would hold the media's install.wim: discarded, never reused.
    if (auto r = uup::releaseWorkMount(dism, work); !r) {
        return std::unexpected(r.error());
    }
    log::info("esd", std::format(L"convert {} ESD(s), {} edition(s) -> {}", esds.size(), selected.size(),
                                 options.output.empty() ? media.wstring() : options.output.wstring()));

    uup::ConvertResult result;
    {
        uup::UupSetFiles labels; // the same ISO label as the UUP path makes
        for (const auto& s : selected) {
            uup::UupEdition e;
            e.editionId = s.edition.editionId;
            e.language = s.edition.language;
            e.architecture = s.edition.architecture;
            labels.editions.push_back(std::move(e));
        }
        result.label = uup::isoLabel(labels);
    }
    const bool service = options.netFx3 || options.resetBase;
    const double s1 = 0.08, s2 = service ? 0.5 : 0.8, s3 = service ? 0.8 : 0.8, s4 = service ? 0.86 : 0.86;

    // 1. Setup media and Microsoft's boot.wim, from the first ESD.
    std::filesystem::remove_all(media, ec);
    if (auto r = setupMediaFromEsd(esds.front().first, media, phase(task, 0.0, s1, L"media")); !r) {
        return std::unexpected(r.error());
    }
    // The Media Creation Tool's own bookkeeping, not part of Microsoft's ISOs (lab 2026-11-11: __chunk_data
    // in the setup media's root).
    std::filesystem::remove(media / L"MediaMeta.xml", ec);
    std::filesystem::remove(media / L"__chunk_data", ec);

    // 2. install.wim: the editions, LZX (the solid ESD cannot be mounted).
    const auto install = media / L"sources" / L"install.wim";
    std::filesystem::remove(install, ec);
    const double n = static_cast<double>(selected.size());
    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto& s = selected[i];
        const TaskContext one = phase(task, s1 + (s2 - s1) * (static_cast<double>(i) / n),
                                      s1 + (s2 - s1) * (static_cast<double>(i + 1) / n), L"export");
        if (auto r = exportImage(s.esd, s.edition.index, install, WimCompression::Lzx, one); !r) {
            return std::unexpected(r.error());
        }
        result.editions.push_back(s.edition.name);
    }

    // 3. Optional servicing, each edition mounted once.
    if (service) {
        const int count = static_cast<int>(selected.size());
        for (int i = 1; i <= count; ++i) {
            const double from = s2 + (s3 - s2) * (static_cast<double>(i - 1) / count);
            const double to = s2 + (s3 - s2) * (static_cast<double>(i) / count);
            if (auto m = mountSafely(dism, install, i, mount, /*readOnly=*/false, phase(task, from, from + (to - from) * 0.1, L"service")); !m) {
                return std::unexpected(m.error());
            }
            auto work1 = [&]() -> Result<void> {
                auto session = dism.openSession(mount);
                if (!session) {
                    return std::unexpected(session.error());
                }
                if (options.netFx3) {
                    if (auto r = (*session)->enableFeature(L"NetFx3", phase(task, from + (to - from) * 0.1, from + (to - from) * 0.5, L"service"),
                                                           {media / L"sources" / L"sxs"});
                        !r) {
                        log::warn("esd", L".NET Framework 3.5 not enabled: " + describe(r.error()));
                        result.warnings.push_back(L".NET Framework 3.5: " + r.error().message);
                    }
                }
                if (options.resetBase) {
                    auto cleaned = runDismExe(**session, L"/Cleanup-Image /StartComponentCleanup /ResetBase", [&](double f) {
                        task.report(from + (to - from) * (0.5 + 0.4 * f), L"cleanup");
                    });
                    if (!cleaned || cleaned->exitCode != 0) {
                        log::warn("esd", L"component cleanup did not run");
                        result.warnings.push_back(L"/ResetBase");
                    }
                }
                return {};
            }();
            const TaskContext unmountTask{CancelToken{}, [&](double f, std::wstring_view) {
                                              task.report(from + (to - from) * (0.9 + 0.1 * f), L"service");
                                          }};
            if (!work1) {
                (void)unmountSafely(dism, mount, /*commit=*/false, unmountTask);
                return std::unexpected(work1.error());
            }
            if (auto u = unmountSafely(dism, mount, /*commit=*/true, unmountTask); !u) {
                return std::unexpected(u.error());
            }
        }
    }

    // 4. Smaller once more: what the commits left behind, or the ESD form.
    if (options.compression == WimCompression::Lzms) {
        if (auto r = recompressWim(install, WimCompression::Lzms, phase(task, s3, s4, L"optimize")); !r) {
            return std::unexpected(r.error());
        }
    } else if (service) {
        if (auto r = optimizeWim(install, phase(task, s3, s4, L"optimize")); !r) {
            return std::unexpected(r.error());
        }
    }
    {
        const auto finalInstall = options.compression == WimCompression::Lzms ? media / L"sources" / L"install.esd" : install;
        auto images = imagesOf(finalInstall);
        if (images && !images->empty()) {
            result.version = images->front().versionString();
        }
    }

    // 5. The ISO.
    if (!options.output.empty()) {
        IsoOptions iso;
        iso.sourceFolder = media;
        iso.output = nativePath(options.output);
        iso.volumeLabel = result.label;
        iso.boot = text::lower(selected.front().edition.architecture) == L"arm64" ? BootMode::UefiOnly : BootMode::UefiAndBios;
        auto built = buildIso(iso, phase(task, s4, 1.0, L"iso"));
        if (!built) {
            return std::unexpected(built.error());
        }
        result.iso = iso.output;
        result.bytes = built->bytes;
        std::filesystem::remove_all(media, ec);
    } else {
        result.media = media;
    }
    std::filesystem::remove_all(work, ec);
    task.report(1.0, L"done");
    log::info("esd", std::format(L"converted: {} ({}), {}", result.iso.empty() ? media.wstring() : result.iso.wstring(),
                                 result.version, result.label));
    return result;
}

} // namespace wl::core::esd
