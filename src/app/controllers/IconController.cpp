#include "app/controllers/IconController.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/RegistryEdit.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

core::RegistryWrite expandString(std::wstring key, std::wstring name, std::wstring_view text) {
    core::RegistryWrite write;
    write.kind = core::RegistryWrite::Kind::Set;
    write.key = std::move(key);
    write.name = std::move(name);
    write.type = REG_EXPAND_SZ;
    write.data.assign((text.size() + 1) * sizeof(wchar_t), 0);
    std::memcpy(write.data.data(), text.data(), text.size() * sizeof(wchar_t));
    return write;
}

Operation registryOp(OpKind kind, const core::RegistryWrite& write) {
    Operation op{kind, core::registryTarget(write), core::formatRegValue(write)};
    op.risk = core::ops::Risk::Low;
    return op;
}

// A 16×16 icon, 32-bit, every pixel transparent: what "no shortcut arrow" shows.
std::string blankIconBytes() {
    std::string ico;
    auto put16 = [&](std::uint16_t v) { ico.append(reinterpret_cast<const char*>(&v), 2); };
    auto put32 = [&](std::uint32_t v) { ico.append(reinterpret_cast<const char*>(&v), 4); };
    constexpr std::uint32_t kPixels = 16 * 16 * 4, kMask = 16 * 4; // 32 bpp + 1 bpp mask (rows padded to 4 bytes)
    constexpr std::uint32_t kImage = 40 + kPixels + kMask;
    put16(0); put16(1); put16(1);                    // ICONDIR
    ico.push_back(16); ico.push_back(16); ico.push_back(0); ico.push_back(0); // ICONDIRENTRY
    put16(1); put16(32); put32(kImage); put32(6 + 16);
    put32(40); put32(16); put32(32); put16(1); put16(32); put32(0); put32(kPixels + kMask); // BITMAPINFOHEADER
    put32(0); put32(0); put32(0); put32(0);
    ico.append(kPixels, '\0');                       // transparent (alpha 0)
    ico.append(kMask, '\xff');                       // mask: all transparent
    return ico;
}

} // namespace

std::wstring IconController::imageFile(const IconSlot& slot) {
    return L"ProgramData\\WinLove\\Icons\\" + utf8::toWide(std::string(slot.id)) + L".ico";
}

Operation IconController::themeKeepsIcons() {
    core::RegistryWrite write;
    write.kind = core::RegistryWrite::Kind::Set;
    write.key = L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes";
    write.name = L"ThemeChangesDesktopIcons";
    write.type = REG_DWORD;
    write.data.assign(4, 0);
    return registryOp(OpKind::SetRegistryFirstLogon, write);
}

std::vector<Operation> IconController::operationsFor(const IconSlot& slot, const std::filesystem::path& icon) {
    std::vector<Operation> ops;
    Operation copy{OpKind::CopyFile, imageFile(slot), icon.wstring()};
    copy.risk = core::ops::Risk::Low;
    std::error_code ec;
    if (const auto size = std::filesystem::file_size(icon, ec); !ec) {
        copy.sizeDelta = static_cast<std::int64_t>(size);
    }
    ops.push_back(std::move(copy));
    const std::wstring value = L"%ProgramData%\\WinLove\\Icons\\" + utf8::toWide(std::string(slot.id)) + L".ico,0";
    if (!slot.clsid.empty()) {
        const std::wstring machine = L"HKLM\\SOFTWARE\\Classes\\CLSID\\" + std::wstring(slot.clsid) + L"\\DefaultIcon";
        const std::wstring user = L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\" + std::wstring(slot.clsid) + L"\\DefaultIcon";
        std::vector<std::wstring> names{std::wstring(slot.clsidValue)};
        if (slot.clsidValue == L"Empty") {
            names.push_back(L""); // the default value is what an empty bin shows first
        }
        for (const auto& name : names) {
            ops.push_back(registryOp(OpKind::SetRegistryValue, expandString(machine, name, value)));
            ops.push_back(registryOp(OpKind::SetRegistryFirstLogon, expandString(user, name, value)));
        }
    }
    if (!slot.shellIcon.empty()) {
        ops.push_back(registryOp(OpKind::SetRegistryValue,
                                 expandString(L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons",
                                              std::wstring(slot.shellIcon), value)));
    }
    if (slot.systemDrive) {
        ops.push_back(registryOp(OpKind::SetRegistryValue,
                                 expandString(L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\DriveIcons\\C\\DefaultIcon", L"", value)));
    }
    return ops;
}

std::optional<std::filesystem::path> IconController::assigned(const IconSlot& slot) const {
    if (const auto* op = m_state.changes().find(OpKind::CopyFile, imageFile(slot))) {
        return std::filesystem::path(op->value);
    }
    return std::nullopt;
}

Result<void> IconController::assign(const IconSlot& slot, const std::filesystem::path& icon) {
    if (!isIconFile(icon)) {
        return fail(ErrorCode::InvalidArgument, L"not an icon file (.ico)", icon.wstring());
    }
    auto ops = operationsFor(slot, icon);
    if (slot.group == IconSlot::Group::Desktop) {
        ops.push_back(themeKeepsIcons());
    }
    m_state.queueMany(std::move(ops));
    return {};
}

void IconController::reset(const IconSlot& slot) {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    for (const auto& op : operationsFor(slot, {})) {
        slots.emplace_back(op.kind, op.target);
    }
    // The theme value stays while another desktop icon needs it.
    const bool otherDesktop = std::ranges::any_of(iconSlots(), [&](const IconSlot& s) {
        return s.group == IconSlot::Group::Desktop && s.id != slot.id && assigned(s);
    });
    if (slot.group == IconSlot::Group::Desktop && !otherDesktop) {
        const auto theme = themeKeepsIcons();
        slots.emplace_back(theme.kind, theme.target);
    }
    m_state.unqueueMany(slots);
}

void IconController::resetAll() {
    for (const auto& slot : iconSlots()) {
        reset(slot);
    }
}

Result<IconController::PackResult> IconController::applyPack(const std::filesystem::path& folder) {
    auto pack = readIconPack(folder);
    if (!pack) {
        return std::unexpected(pack.error());
    }
    PackResult result{pack->name, 0, pack->unmatched};
    for (const auto& [id, file] : pack->icons) {
        if (const auto* slot = findIconSlot(id); slot && assign(*slot, file)) {
            ++result.matched;
        }
    }
    log::info("icons", std::format(L"icon pack \"{}\": {} slot(s), {} file(s) not matched", result.name, result.matched,
                                   result.unmatched.size()));
    return result;
}

std::filesystem::path IconController::blankIcon() const {
    return m_state.settings().workRoot / L"icons" / L"blank.ico";
}

bool IconController::shortcutArrowRemoved() const {
    const auto* slot = findIconSlot("shortcut");
    const auto icon = slot ? assigned(*slot) : std::nullopt;
    return icon && *icon == blankIcon();
}

Result<void> IconController::setShortcutArrowRemoved(bool removed) {
    const auto* slot = findIconSlot("shortcut");
    if (!slot) {
        return fail(ErrorCode::NotFound, L"no shortcut slot");
    }
    if (!removed) {
        if (shortcutArrowRemoved()) {
            reset(*slot);
        }
        return {};
    }
    const auto file = blankIcon();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        const std::string bytes = blankIconBytes();
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return fail(ErrorCode::IoError, L"cannot write the blank icon", file.wstring());
        }
    }
    return assign(*slot, file);
}

std::pair<std::filesystem::path, int> IconController::defaultIcon(const IconSlot& slot) const {
    if (const auto& mounted = m_state.mounted()) {
        const auto inImage = mounted->mountDir / L"Windows" / L"System32" / std::wstring(slot.defaultFile);
        std::error_code ec;
        if (std::filesystem::is_regular_file(inImage, ec)) {
            return {inImage, slot.defaultIndex};
        }
    }
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    return {std::filesystem::path(system) / std::wstring(slot.defaultFile), slot.defaultIndex};
}

int IconController::changedCount() const {
    return static_cast<int>(std::ranges::count_if(iconSlots(), [&](const IconSlot& s) { return assigned(s).has_value(); }));
}

} // namespace wl::app
