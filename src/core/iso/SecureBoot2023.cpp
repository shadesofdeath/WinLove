#include "core/iso/SecureBoot2023.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "core/image/wim/WimVerify.h"
#include "core/io/ByteSource.h"
#include "core/system/Files.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <string_view>

namespace wl::core {

namespace {

constexpr wchar_t kEfiEx[] = L"Windows\\Boot\\EFI_EX\\";
constexpr wchar_t kFontsEx[] = L"Windows\\Boot\\Fonts_EX";

Result<void> writeBytes(const std::filesystem::path& file, std::span<const std::byte> data) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        return fail(ErrorCode::IoError, L"cannot write the boot file", file.wstring());
    }
    return {};
}

} // namespace

Result<SecureBoot2023> prepareSecureBoot2023(const std::filesystem::path& mediaInput, const std::filesystem::path& bootWim,
                                             const std::filesystem::path& scratchInput, bool noPrompt) {
    const std::filesystem::path media = nativePath(mediaInput);
    const std::filesystem::path scratch = nativePath(scratchInput);
    auto wim = DiskFile::open(bootWim.empty() ? media / L"sources" / L"boot.wim" : nativePath(bootWim));
    if (!wim) {
        return std::unexpected(wim.error());
    }
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch, ec);
    SecureBoot2023 out;
    // Copies `inWim` (boot.wim image 1) to scratch\<name> and puts it at `onMedia` in the ISO.
    auto take = [&](std::wstring_view inWim, const std::wstring& onMedia, bool required) -> Result<bool> {
        auto data = wimFileData(**wim, 1, inWim);
        if (!data) {
            if (data.error().code == ErrorCode::NotFound && !required) {
                return false;
            }
            if (data.error().code == ErrorCode::NotFound) {
                return fail(ErrorCode::NotFound,
                            L"boot.wim has no boot files signed by Windows UEFI CA 2023: it needs a cumulative update "
                            L"of 2024-04 or later (add one on the Updates page with the setup media option, then build again)",
                            std::wstring(inWim));
            }
            return std::unexpected(data.error());
        }
        const auto file = scratch / std::filesystem::path(onMedia).filename();
        if (auto r = writeBytes(file, *data); !r) {
            return std::unexpected(r.error());
        }
        const bool isNew = !std::filesystem::exists(media / onMedia, ec);
        out.files.push_back({onMedia, file, isNew});
        return true;
    };

    const bool arm = std::filesystem::exists(media / L"efi" / L"boot" / L"bootaa64.efi", ec);
    if (auto r = take(std::wstring(kEfiEx) + L"bootmgfw_EX.efi",
                      arm ? L"efi\\boot\\bootaa64.efi" : L"efi\\boot\\bootx64.efi", true);
        !r) {
        return std::unexpected(r.error());
    }
    out.bootManagerVersion = fileVersion(out.files.back().file);
    if (auto r = take(std::wstring(kEfiEx) + L"bootmgr_EX.efi", L"bootmgr.efi", false); !r) {
        return std::unexpected(r.error());
    }
    // The El Torito image: also on the media as efi\microsoft\boot\efisys_ex.bin (what Microsoft's script leaves).
    const std::wstring efisys = noPrompt ? L"efisys_noprompt_EX.bin" : L"efisys_EX.bin";
    if (auto r = take(L"Windows\\Boot\\DVD_EX\\EFI\\en-US\\" + efisys, L"efi\\microsoft\\boot\\efisys_ex.bin", true); !r) {
        return std::unexpected(r.error());
    }
    out.efiBootImage = out.files.back().file;
    if (auto r = take(L"Windows\\Boot\\EFI\\boot.stl", L"efi\\microsoft\\boot\\boot.stl", false); !r) {
        return std::unexpected(r.error());
    }
    // Fonts: <name>_EX.ttf → <name>.ttf (the console font has no _EX one and stays).
    auto fonts = wimFolderNames(**wim, 1, kFontsEx);
    if (fonts) {
        for (const auto& name : *fonts) {
            if (!text::iendsWith(name, L"_EX.ttf")) {
                continue;
            }
            const std::wstring plain = name.substr(0, name.size() - 7) + L".ttf";
            const std::filesystem::path font = scratch / L"fonts";
            auto data = wimFileData(**wim, 1, std::wstring(kFontsEx) + L"\\" + name);
            if (!data) {
                return std::unexpected(data.error());
            }
            if (auto r = writeBytes(font / plain, *data); !r) {
                return std::unexpected(r.error());
            }
            const std::wstring onMedia = L"efi\\microsoft\\boot\\fonts\\" + plain;
            out.files.push_back({onMedia, font / plain, !std::filesystem::exists(media / onMedia, ec)});
        }
    }
    log::info("iso", std::format(L"Secure Boot 2023: {} boot file(s) from boot.wim, boot manager {}", out.files.size(),
                                 out.bootManagerVersion));
    return out;
}

bool dbHasCa2023(std::span<const std::byte> db) {
    // The certificate's subject and issuer name are plain ASCII in the DER encoding.
    constexpr std::string_view kName = "Windows UEFI CA 2023";
    const auto* begin = reinterpret_cast<const char*>(db.data());
    return std::string_view(begin, db.size()).find(kName) != std::string_view::npos;
}

std::optional<bool> firmwareTrustsCa2023() {
    if (!enablePrivilege(SE_SYSTEM_ENVIRONMENT_NAME)) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(64 * 1024);
    // EFI_IMAGE_SECURITY_DATABASE_GUID
    const DWORD got = GetFirmwareEnvironmentVariableW(L"db", L"{d719b2cb-3d3a-4596-a3bc-dad00e67656f}", buffer.data(),
                                                      static_cast<DWORD>(buffer.size()));
    if (got == 0) {
        return std::nullopt; // BIOS boot, Secure Boot not set up, or no privilege
    }
    buffer.resize(got);
    return dbHasCa2023(buffer);
}

} // namespace wl::core
