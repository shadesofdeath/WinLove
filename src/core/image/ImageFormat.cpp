#include "core/image/ImageFormat.h"

#include <array>
#include <cwctype>
#include <utility>

namespace wl::core {

namespace {

bool endsWithNoCase(std::wstring_view text, std::wstring_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    const auto tail = text.substr(text.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::towlower(tail[i]) != std::towlower(suffix[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

ImageFormat formatFromPath(std::wstring_view path) noexcept {
    static constexpr std::array<std::pair<std::wstring_view, ImageFormat>, 6> kExtensions = {{
        {L".iso", ImageFormat::Iso},
        {L".wim", ImageFormat::Wim},
        {L".esd", ImageFormat::Esd},
        {L".swm", ImageFormat::Swm},
        {L".vhdx", ImageFormat::Vhdx},
        {L".vhd", ImageFormat::Vhd},
    }};
    for (const auto& [extension, format] : kExtensions) {
        if (endsWithNoCase(path, extension)) {
            return format;
        }
    }
    return ImageFormat::Unknown;
}

const wchar_t* formatName(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Unknown: return L"Unknown";
    case ImageFormat::Iso: return L"ISO";
    case ImageFormat::Wim: return L"WIM";
    case ImageFormat::Esd: return L"ESD";
    case ImageFormat::Swm: return L"SWM";
    case ImageFormat::Vhd: return L"VHD";
    case ImageFormat::Vhdx: return L"VHDX";
    }
    return L"?";
}

} // namespace wl::core
