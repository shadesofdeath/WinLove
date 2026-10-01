#include "core/image/Branding.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "core/image/Fonts.h"
#include "core/image/ImageFiles.h"
#include "core/system/Picture.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>

namespace wl::core {

namespace {

// Pictures directly in <mountDir>\<folder> whose lower-case name passes `keep`.
void collect(const std::filesystem::path& mountDir, std::wstring_view folder, std::vector<std::wstring>& out,
             const auto& keep) {
    std::error_code ec;
    std::vector<std::wstring> found;
    for (const auto& entry : std::filesystem::directory_iterator(mountDir / std::wstring(folder), ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (keep(text::lower(name))) {
            found.push_back(std::wstring(folder) + L"\\" + name);
        }
    }
    std::ranges::sort(found);
    out.insert(out.end(), found.begin(), found.end());
}

PictureFormat formatOf(std::wstring_view relative) {
    const std::wstring ext = text::lower(std::filesystem::path(relative).extension().wstring());
    return ext == L".png" ? PictureFormat::Png : ext == L".bmp" ? PictureFormat::Bmp : PictureFormat::Jpeg;
}

RegistryWrite stringValue(std::wstring_view key, std::wstring name, std::wstring_view text) {
    std::wstring quoted = L"\"";
    for (const wchar_t c : text) {
        if (c == L'\\' || c == L'"') {
            quoted.push_back(L'\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back(L'"');
    return *parseRegValue(std::wstring(key), std::move(name), quoted);
}

} // namespace

std::wstring_view pictureSlotKey(PictureSlot slot) noexcept {
    switch (slot) {
    case PictureSlot::Wallpaper: return L"wallpaper";
    case PictureSlot::LockScreen: return L"lockscreen";
    case PictureSlot::Account: return L"account";
    case PictureSlot::OemLogo: return L"oemlogo";
    }
    return L"wallpaper";
}

std::optional<PictureSlot> pictureSlotFromKey(std::wstring_view key) noexcept {
    for (const auto slot : {PictureSlot::Wallpaper, PictureSlot::LockScreen, PictureSlot::Account, PictureSlot::OemLogo}) {
        if (pictureSlotKey(slot) == key) {
            return slot;
        }
    }
    return std::nullopt;
}

std::vector<std::wstring> pictureTargets(const std::filesystem::path& mountDir, PictureSlot slot) {
    std::vector<std::wstring> out;
    const auto jpg = [](const std::wstring& n) { return n.ends_with(L".jpg") || n.ends_with(L".jpeg"); };
    switch (slot) {
    case PictureSlot::Wallpaper:
        collect(mountDir, L"Windows\\Web\\Wallpaper\\Windows", out, jpg);
        collect(mountDir, L"Windows\\Web\\4K\\Wallpaper\\Windows", out, jpg);
        break;
    case PictureSlot::LockScreen:
        collect(mountDir, L"Windows\\Web\\Screen", out, [](const std::wstring& n) { return n == L"img100.jpg"; });
        break;
    case PictureSlot::Account:
        collect(mountDir, L"ProgramData\\Microsoft\\User Account Pictures", out, [](const std::wstring& n) {
            return n.starts_with(L"user") && (n.ends_with(L".png") || n.ends_with(L".bmp"));
        });
        break;
    case PictureSlot::OemLogo: out.emplace_back(kOemLogoFile); break;
    }
    return out;
}

Result<PictureResult> applyPicture(const std::filesystem::path& mountDir, PictureSlot slot,
                                   const std::filesystem::path& source, const TaskContext& task) {
    // The source must be a picture before anything in the image is touched.
    if (auto size = pictureSize(source); !size) {
        return std::unexpected(size.error());
    }
    const auto targets = pictureTargets(mountDir, slot);
    if (targets.empty()) {
        return fail(ErrorCode::NotFound, L"the image has none of the default pictures to replace",
                    std::wstring(pictureSlotKey(slot)));
    }
    PictureResult result;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (auto cancelled = task.cancel.check(L"picture"); !cancelled) {
            return std::unexpected(cancelled.error());
        }
        const std::wstring& relative = targets[i];
        int width = 120;
        int height = 120;
        if (slot != PictureSlot::OemLogo) {
            auto existing = pictureSize(mountDir / relative);
            if (!existing) {
                return std::unexpected(existing.error());
            }
            width = existing->width;
            height = existing->height;
        }
        auto bytes = encodePicture(source, slot == PictureSlot::OemLogo ? PictureFormat::Bmp : formatOf(relative), width,
                                   height);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        if (auto written = replaceImageFile(mountDir, relative, *bytes); !written) {
            return std::unexpected(written.error());
        }
        log::info("branding", std::format(L"{}: {} ({}x{}, {} bytes)", pictureSlotKey(slot), relative, width, height,
                                          bytes->size()));
        ++result.files;
        task.report(static_cast<double>(i + 1) / static_cast<double>(targets.size()), L"picture");
    }
    if (slot == PictureSlot::OemLogo) {
        result.registry.push_back(stringValue(kOemKey, L"Logo", L"C:\\Windows\\System32\\oemlogo.bmp"));
    } else if (slot == PictureSlot::LockScreen) {
        // Honoured where the Personalization CSP is (Enterprise / Education; others keep img100.jpg).
        const std::wstring path = L"C:\\Windows\\Web\\Screen\\img100.jpg";
        result.registry.push_back(stringValue(kPersonalizationCspKey, L"LockScreenImagePath", path));
        result.registry.push_back(stringValue(kPersonalizationCspKey, L"LockScreenImageUrl", path));
        result.registry.push_back(*parseRegValue(std::wstring(kPersonalizationCspKey), L"LockScreenImageStatus", L"dword:00000001"));
    }
    return result;
}

Result<RegistryWrite> applyFont(const std::filesystem::path& mountDir, const std::filesystem::path& source) {
    const auto read = readFileBytes(source);
    if (!read) {
        return fail(ErrorCode::NotFound, L"font file not found", source.wstring());
    }
    const std::string& bytes = *read;
    auto info = parseFont(bytes);
    if (!info) {
        return std::unexpected(info.error());
    }
    const std::wstring file = fontFileName(source);
    if (auto written = replaceImageFile(mountDir, L"Windows\\Fonts\\" + file, bytes); !written) {
        return std::unexpected(written.error());
    }
    log::info("branding", std::format(L"font {} -> Windows\\Fonts\\{}", info->registryName(), file));
    return stringValue(kFontsKey, info->registryName(), file);
}

} // namespace wl::core
