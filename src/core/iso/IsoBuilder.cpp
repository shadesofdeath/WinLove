#include "core/iso/IsoBuilder.h"

#include "base/Utf8.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/Source.h"
#include "core/image/wim/WimGapi.h"
#include "core/system/Com.h"
#include "core/system/Files.h"

#include <windows.h>

#include <bcrypt.h>
#include <ole2.h> // WIN32_LEAN_AND_MEAN drops OLE; imapi2fs.h needs it
#include <imapi2fs.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <vector>

namespace wl::core {

namespace {

using Microsoft::WRL::ComPtr;

std::unexpected<Error> comFail(HRESULT hr, std::wstring what) {
    return fail(ErrorCode::IoError, L"IMAPI2FS: " + what, L"iso", static_cast<std::int32_t>(hr));
}

std::filesystem::path biosBoot(const std::filesystem::path& folder) {
    return folder / L"boot" / L"etfsboot.com";
}

std::filesystem::path uefiBoot(const std::filesystem::path& folder, bool noPrompt) {
    return folder / L"efi" / L"microsoft" / L"boot" / (noPrompt ? L"efisys_noprompt.bin" : L"efisys.bin");
}

// One El Torito entry: the boot image file as a stream, platform, no emulation.
Result<void> addBootEntry(const std::filesystem::path& file, PlatformId platform, SAFEARRAY* array, LONG slot) {
    ComPtr<IStream> stream;
    HRESULT hr = SHCreateStreamOnFileEx(file.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE,
                                        nullptr, stream.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return comFail(hr, L"open boot image " + file.wstring());
    }
    ComPtr<IBootOptions> boot;
    hr = CoCreateInstance(__uuidof(BootOptions), nullptr, CLSCTX_ALL, __uuidof(IBootOptions),
                          reinterpret_cast<void**>(boot.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        return comFail(hr, L"create boot options");
    }
    if (FAILED(hr = boot->AssignBootImage(stream.Get())) || FAILED(hr = boot->put_PlatformId(platform)) ||
        FAILED(hr = boot->put_Emulation(EmulationNone))) {
        return comFail(hr, L"configure boot image " + file.filename().wstring());
    }
    VARIANT v{};
    v.vt = VT_DISPATCH;
    v.pdispVal = boot.Get();
    hr = SafeArrayPutElement(array, &slot, &v); // AddRefs the dispatch
    if (FAILED(hr)) {
        return comFail(hr, L"boot options array");
    }
    return {};
}

} // namespace

Result<void> checkBootFiles(const std::filesystem::path& folder, BootMode mode, bool noPrompt) {
    std::error_code ec;
    if (mode != BootMode::UefiOnly && !std::filesystem::is_regular_file(biosBoot(folder), ec)) {
        return fail(ErrorCode::NotFound, L"BIOS boot image missing", biosBoot(folder).wstring());
    }
    if (mode != BootMode::BiosOnly && !std::filesystem::is_regular_file(uefiBoot(folder, noPrompt), ec)) {
        return fail(ErrorCode::NotFound, L"UEFI boot image missing", uefiBoot(folder, noPrompt).wstring());
    }
    return {};
}

std::uint64_t folderSize(const std::filesystem::path& folder) {
    return treeBytes(folder);
}

Result<IsoResult> buildIso(const IsoOptions& options, const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path folder = nativePath(options.sourceFolder);
    const std::filesystem::path output = nativePath(options.output);
    if (auto r = checkBootFiles(folder, options.boot, options.noPrompt); !r) {
        return std::unexpected(r.error());
    }
    ComScope com;
    ComPtr<IFileSystemImage2> image;
    HRESULT hr = CoCreateInstance(__uuidof(MsftFileSystemImage), nullptr, CLSCTX_ALL, __uuidof(IFileSystemImage2),
                                  reinterpret_cast<void**>(image.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        return comFail(hr, L"create file system image");
    }
    // Windows setup media: UDF only (install.wim is larger than ISO 9660's 4 GB file limit).
    // A DVD-sized default would cap the image; BD-R DL leaves room for any setup folder.
    if (FAILED(hr = image->ChooseImageDefaultsForMediaType(IMAPI_MEDIA_TYPE_BDR)) ||
        FAILED(hr = image->put_FileSystemsToCreate(FsiFileSystemUDF)) || FAILED(hr = image->put_UDFRevision(0x102))) {
        return comFail(hr, L"configure file systems");
    }
    image->put_FreeMediaBlocks(0); // 0: no media size limit
    std::wstring label = options.volumeLabel.empty() ? L"WINLOVE" : options.volumeLabel.substr(0, 32);
    BSTR labelB = SysAllocString(label.c_str());
    hr = image->put_VolumeName(labelB);
    SysFreeString(labelB);
    if (FAILED(hr)) {
        return comFail(hr, L"volume label");
    }

    // El Torito: BIOS first (entry used by legacy firmware), then UEFI.
    const bool bios = options.boot != BootMode::UefiOnly;
    const bool uefi = options.boot != BootMode::BiosOnly;
    SAFEARRAY* array = SafeArrayCreateVector(VT_VARIANT, 0, (bios ? 1u : 0u) + (uefi ? 1u : 0u));
    LONG slot = 0;
    Result<void> bootResult{};
    if (bios) {
        bootResult = addBootEntry(biosBoot(folder), PlatformX86, array, slot++);
    }
    if (bootResult && uefi) {
        bootResult = addBootEntry(uefiBoot(folder, options.noPrompt), PlatformEFI, array, slot++);
    }
    if (!bootResult) {
        SafeArrayDestroy(array);
        return std::unexpected(bootResult.error());
    }
    hr = image->put_BootImageOptionsArray(array);
    SafeArrayDestroy(array);
    if (FAILED(hr)) {
        return comFail(hr, L"boot catalog");
    }

    log::info("iso", L"adding " + folder.wstring());
    task.report(0.0, L"scan");
    ComPtr<IFsiDirectoryItem> root;
    if (FAILED(hr = image->get_Root(root.ReleaseAndGetAddressOf()))) {
        return comFail(hr, L"root directory");
    }
    BSTR source = SysAllocString(folder.c_str());
    hr = root->AddTree(source, VARIANT_FALSE);
    SysFreeString(source);
    if (FAILED(hr)) {
        return comFail(hr, L"add " + folder.wstring());
    }
    for (const auto& file : options.rootFiles) {
        ComPtr<IStream> content;
        content.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(file.content.data()),
                                           static_cast<UINT>(file.content.size())));
        if (!content.Get()) {
            return comFail(E_OUTOFMEMORY, L"stream for " + file.name);
        }
        BSTR name = SysAllocString(file.name.c_str());
        root->Remove(name); // the folder's own copy, if any (fails when there is none: fine)
        hr = root->AddFile(name, content.Get());
        SysFreeString(name);
        if (FAILED(hr)) {
            return comFail(hr, L"add " + file.name);
        }
        log::info("iso", std::format(L"added {} ({} bytes) to the image root", file.name, file.content.size()));
    }
    for (const auto& file : options.replacedFiles) {
        ComPtr<IStream> content;
        hr = SHCreateStreamOnFileEx(nativePath(file.file).c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL,
                                    FALSE, nullptr, content.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            return comFail(hr, L"open " + file.file.wstring());
        }
        if (file.isNew) {
            // Its folder may be new as well (sources\en-US of a Setup image): made one level at a time.
            for (std::size_t at = file.path.find(L'\\'); at != std::wstring::npos; at = file.path.find(L'\\', at + 1)) {
                BSTR dir = SysAllocString(file.path.substr(0, at).c_str());
                (void)root->AddDirectory(dir); // fails when it is there already
                SysFreeString(dir);
            }
        }
        BSTR path = SysAllocString(file.path.c_str());
        hr = root->Remove(path); // it replaces a file of the folder: one that is not there is a mistake…
        if (FAILED(hr) && file.isNew) {
            hr = S_OK; // …unless it is new
        }
        if (SUCCEEDED(hr)) {
            hr = root->AddFile(path, content.Get());
        }
        SysFreeString(path);
        if (FAILED(hr)) {
            return comFail(hr, L"replace " + file.path);
        }
        log::info("iso", std::format(L"{} comes from {}", file.path, file.file.wstring()));
    }

    ComPtr<IFileSystemImageResult> result;
    if (FAILED(hr = image->CreateResultImage(result.ReleaseAndGetAddressOf()))) {
        return comFail(hr, L"create result image");
    }
    ComPtr<IStream> stream;
    LONG blocks = 0;
    LONG blockSize = 2048;
    if (FAILED(hr = result->get_ImageStream(stream.ReleaseAndGetAddressOf())) || FAILED(hr = result->get_TotalBlocks(&blocks)) ||
        FAILED(hr = result->get_BlockSize(&blockSize))) {
        return comFail(hr, L"image stream");
    }
    const std::uint64_t total = static_cast<std::uint64_t>(blocks) * static_cast<std::uint64_t>(blockSize);
    log::info("iso", std::format(L"writing {} ({} bytes)", output.wstring(), total));

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    const auto partial = std::filesystem::path(output.wstring() + L".part");
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::IoError, L"cannot create the ISO file", partial.wstring());
        }
        std::vector<char> buffer(4 * 1024 * 1024);
        std::uint64_t written = 0;
        for (;;) {
            if (task.cancel.cancelled()) {
                out.close();
                std::filesystem::remove(partial, ec);
                return fail(ErrorCode::Cancelled, L"ISO build cancelled", output.wstring());
            }
            ULONG read = 0;
            hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &read);
            if (FAILED(hr)) {
                out.close();
                std::filesystem::remove(partial, ec);
                return comFail(hr, L"read image stream");
            }
            if (read == 0) {
                break;
            }
            out.write(buffer.data(), read);
            if (!out) {
                out.close();
                std::filesystem::remove(partial, ec);
                return fail(ErrorCode::IoError, L"write failed (disk full?)", partial.wstring());
            }
            written += read;
            task.report(total ? static_cast<double>(written) / static_cast<double>(total) * (options.writeSha256 ? 0.85 : 1.0)
                              : 0.0,
                        L"write");
        }
    }
    std::filesystem::remove(output, ec);
    std::filesystem::rename(partial, output, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot rename the ISO into place", output.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
    }

    IsoResult iso;
    iso.bytes = treeBytes(output);
    if (options.writeSha256) {
        const TaskContext hashTask{task.cancel, [&](double f, std::wstring_view) { task.report(0.85 + 0.15 * f, L"sha256"); }};
        auto hash = sha256File(output, hashTask);
        if (!hash) {
            return std::unexpected(hash.error());
        }
        iso.sha256 = *hash;
        // UTF-8 bytes: a wofstream in the "C" locale fails on non-ASCII names ("Türkçe.iso").
        std::ofstream sum(std::filesystem::path(output.wstring() + L".sha256"), std::ios::binary);
        sum << utf8::fromWide(iso.sha256 + L" *" + output.filename().wstring()) << "\n";
        if (!sum) {
            log::warn("iso", L"could not write the .sha256 file next to " + output.wstring());
        }
    }
    task.report(1.0, L"done");
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    log::info("iso", std::format(L"ISO ready: {} ({} bytes, {} ms)", output.wstring(), iso.bytes, ms.count()));
    return iso;
}

Result<void> repackInstallImage(const std::filesystem::path& folderInput, WimCompression compression,
                                const TaskContext& task) {
    const std::filesystem::path folder = nativePath(folderInput);
    auto source = openSource(folder);
    if (!source) {
        return std::unexpected(source.error());
    }
    const std::filesystem::path current = nativePath(folder / source->installImage);
    const bool toEsd = compression == WimCompression::Lzms;
    const std::filesystem::path target = folder / L"sources" / (toEsd ? L"install.esd" : L"install.wim");
    const std::filesystem::path fresh = target.wstring() + L".new";
    std::error_code ec;
    std::filesystem::remove(fresh, ec);
    const auto& images = source->install.images;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const double base = static_cast<double>(i) / static_cast<double>(images.size());
        const TaskContext one{task.cancel, [&](double f, std::wstring_view) {
                                  task.report(base + f / static_cast<double>(images.size()), L"repack");
                              }};
        log::info("iso", std::format(L"repack [{}/{}] {}", i + 1, images.size(), images[i].name));
        if (auto r = exportImage(current, images[i].index, fresh, compression, one); !r) {
            std::filesystem::remove(fresh, ec);
            return std::unexpected(r.error());
        }
    }
    // Swap through a rename: the old file goes away only once the new one is in place, and comes
    // back when the new one cannot be moved there (it used to be deleted first).
    if (auto swapped = swapIntoPlace(current, fresh, target); !swapped) {
        return swapped;
    }
    task.report(1.0, L"repack");
    return {};
}

} // namespace wl::core
