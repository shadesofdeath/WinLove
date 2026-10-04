#include "app/catalog/IconCatalog.h"

#include "base/Text.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>

namespace wl::app {

namespace {

using G = IconSlot::Group;
using namespace std::string_view_literals;

constexpr std::array kThisPc{L"thispc"sv, L"computer"sv, L"mycomputer"sv, L"pc"sv, L"bilgisayar"sv, L"bubilgisayar"sv};
constexpr std::array kUser{L"userfiles"sv, L"user"sv, L"userfolder"sv, L"documents"sv, L"kullanici"sv, L"kullaniciklasoru"sv};
constexpr std::array kNetwork{L"network"sv, L"networkplaces"sv, L"ag"sv};
constexpr std::array kRecycleEmpty{L"recyclebinempty"sv, L"recycleempty"sv, L"trashempty"sv, L"binempty"sv, L"recyclebin"sv,
                                   L"trash"sv, L"geridonusumkutusu"sv, L"geridonusumbos"sv};
constexpr std::array kRecycleFull{L"recyclebinfull"sv, L"recyclefull"sv, L"trashfull"sv, L"binfull"sv, L"geridonusumdolu"sv};
constexpr std::array kControlPanel{L"controlpanel"sv, L"control"sv, L"denetimmasasi"sv};
constexpr std::array kFolder{L"folder"sv, L"folderclosed"sv, L"klasor"sv};
constexpr std::array kFolderOpen{L"folderopen"sv, L"openfolder"sv, L"acikklasor"sv};
constexpr std::array kDrive{L"drive"sv, L"harddrive"sv, L"hdd"sv, L"disk"sv, L"localdisk"sv, L"fixeddrive"sv, L"surucu"sv};
constexpr std::array kSystemDrive{L"systemdrive"sv, L"windowsdrive"sv, L"drivec"sv, L"cdrive"sv, L"sistemsurucusu"sv};
constexpr std::array kRemovable{L"removable"sv, L"removabledrive"sv, L"usb"sv, L"usbdrive"sv, L"flash"sv};
constexpr std::array kOptical{L"cd"sv, L"dvd"sv, L"cdrom"sv, L"optical"sv, L"cddrive"sv, L"dvddrive"sv};
constexpr std::array kNetworkDrive{L"networkdrive"sv, L"netdrive"sv, L"agsurucusu"sv};
constexpr std::array kShortcut{L"shortcut"sv, L"shortcutarrow"sv, L"arrow"sv, L"kisayol"sv};

const std::array kSlots{
    IconSlot{"this-pc", G::Desktop, Str::IconsSlotThisPc, L"imageres.dll", -109, L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", L"", L"", false, kThisPc},
    IconSlot{"user-files", G::Desktop, Str::IconsSlotUserFiles, L"imageres.dll", -123, L"{59031A47-3F72-44A7-89C5-5595FE6B30EE}", L"", L"", false, kUser},
    IconSlot{"network", G::Desktop, Str::IconsSlotNetwork, L"imageres.dll", -25, L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", L"", L"", false, kNetwork},
    IconSlot{"recycle-empty", G::Desktop, Str::IconsSlotRecycleEmpty, L"imageres.dll", -55, L"{645FF040-5081-101B-9F08-00AA002F954E}", L"Empty", L"", false, kRecycleEmpty},
    IconSlot{"recycle-full", G::Desktop, Str::IconsSlotRecycleFull, L"imageres.dll", -54, L"{645FF040-5081-101B-9F08-00AA002F954E}", L"Full", L"", false, kRecycleFull},
    IconSlot{"control-panel", G::Desktop, Str::IconsSlotControlPanel, L"imageres.dll", -27, L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}", L"", L"", false, kControlPanel},
    IconSlot{"folder", G::Explorer, Str::IconsSlotFolder, L"shell32.dll", 3, L"", L"", L"3", false, kFolder},
    IconSlot{"folder-open", G::Explorer, Str::IconsSlotFolderOpen, L"shell32.dll", 4, L"", L"", L"4", false, kFolderOpen},
    IconSlot{"drive", G::Explorer, Str::IconsSlotDrive, L"shell32.dll", 8, L"", L"", L"8", false, kDrive},
    IconSlot{"system-drive", G::Explorer, Str::IconsSlotSystemDrive, L"imageres.dll", -36, L"", L"", L"", true, kSystemDrive},
    IconSlot{"removable", G::Explorer, Str::IconsSlotRemovable, L"shell32.dll", 7, L"", L"", L"7", false, kRemovable},
    IconSlot{"optical", G::Explorer, Str::IconsSlotOptical, L"shell32.dll", 11, L"", L"", L"11", false, kOptical},
    IconSlot{"network-drive", G::Explorer, Str::IconsSlotNetworkDrive, L"shell32.dll", 9, L"", L"", L"9", false, kNetworkDrive},
    IconSlot{"shortcut", G::Explorer, Str::IconsSlotShortcut, L"shell32.dll", 29, L"", L"", L"29", false, kShortcut},
};

constexpr std::uint64_t kIconLimit = 4ull << 20;

} // namespace

std::span<const IconSlot> iconSlots() noexcept {
    return kSlots;
}

const IconSlot* findIconSlot(std::string_view id) noexcept {
    const auto it = std::ranges::find(kSlots, id, &IconSlot::id);
    return it == kSlots.end() ? nullptr : &*it;
}

std::wstring iconAliasKey(std::wstring_view stem) {
    // Lower case, letters and digits only; Turkish letters folded ("Geri Dönüşüm" → "geridonusum").
    std::wstring key;
    for (wchar_t c : text::lower(stem)) {
        switch (c) {
        case L'ç': c = L'c'; break;
        case L'ğ': c = L'g'; break;
        case L'ı': c = L'i'; break;
        case L'ö': c = L'o'; break;
        case L'ş': c = L's'; break;
        case L'ü': c = L'u'; break;
        default: break;
        }
        if (std::iswalnum(c) && c < 128) {
            key.push_back(c);
        }
    }
    return key;
}

bool isIconFile(const std::filesystem::path& file) {
    if (text::lower(file.extension().wstring()) != L".ico") {
        return false;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size < 6 + 16 || size > kIconLimit) {
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    unsigned char header[6] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    // ICONDIR: reserved 0, type 1 (icon), count > 0.
    return in.gcount() == 6 && header[0] == 0 && header[1] == 0 && header[2] == 1 && header[3] == 0 && (header[4] | header[5]) != 0;
}

Result<IconPack> readIconPack(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::InvalidArgument, L"not a folder", folder.wstring());
    }
    IconPack pack;
    pack.name = folder.filename().wstring();
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 1) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && isIconFile(it->path())) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);

    // The manifest names files for slots; what it leaves out is matched by name.
    const auto manifest = folder / L"iconpack.json";
    if (std::filesystem::is_regular_file(manifest, ec)) {
        std::ifstream in(manifest, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto doc = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions=*/false);
        if (doc.is_discarded() || !doc.is_object()) {
            return fail(ErrorCode::ParseError, L"iconpack.json is not valid JSON", manifest.wstring());
        }
        if (const auto name = doc.find("name"); name != doc.end() && name->is_string()) {
            pack.name = utf8::toWide(name->get<std::string>());
        }
        if (const auto author = doc.find("author"); author != doc.end() && author->is_string()) {
            pack.author = utf8::toWide(author->get<std::string>());
        }
        if (const auto icons = doc.find("icons"); icons != doc.end() && icons->is_object()) {
            for (const auto& [slot, file] : icons->items()) {
                if (!findIconSlot(slot) || !file.is_string()) {
                    continue;
                }
                const auto path = (folder / utf8::toWide(file.get<std::string>())).lexically_normal();
                // Inside the pack only ("..\..\x.ico" is not the pack's).
                const auto relative = path.lexically_relative(folder);
                if (!relative.empty() && *relative.begin() != L".." && isIconFile(path)) {
                    pack.icons[slot] = path;
                }
            }
        }
    }
    for (const auto& file : files) {
        const std::wstring key = iconAliasKey(file.stem().wstring());
        bool taken = std::ranges::any_of(pack.icons, [&](const auto& entry) { return entry.second == file; });
        for (const auto& slot : kSlots) {
            if (taken) {
                break;
            }
            const bool match = key == iconAliasKey(utf8::toWide(std::string(slot.id))) ||
                               std::ranges::any_of(slot.aliases, [&](std::wstring_view alias) { return key == alias; });
            if (match && !pack.icons.contains(std::string(slot.id))) {
                pack.icons[std::string(slot.id)] = file;
                taken = true;
            }
        }
        if (!taken) {
            pack.unmatched.push_back(file.filename().wstring());
        }
    }
    return pack;
}

} // namespace wl::app
