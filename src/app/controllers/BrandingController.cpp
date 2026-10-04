#include "app/controllers/BrandingController.h"

#include "app/controllers/ImageSettingsController.h"
#include "core/image/Fonts.h"
#include "core/system/Picture.h"

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

std::wstring oemTarget(std::wstring_view field) {
    return std::wstring(core::kOemKey) + L"::" + std::wstring(field);
}

constexpr core::PictureSlot kSlots[] = {core::PictureSlot::Wallpaper, core::PictureSlot::LockScreen,
                                        core::PictureSlot::Account, core::PictureSlot::OemLogo};

} // namespace

std::wstring BrandingController::quoted(std::wstring_view text) {
    std::wstring out = L"\"";
    for (const wchar_t c : text) {
        if (c == L'\\' || c == L'"') {
            out.push_back(L'\\');
        }
        out.push_back(c);
    }
    out.push_back(L'"');
    return out;
}

std::wstring BrandingController::unquoted(std::wstring_view value) {
    if (value.size() < 2 || value.front() != L'"' || value.back() != L'"') {
        return std::wstring(value);
    }
    std::wstring out;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == L'\\' && i + 2 < value.size()) {
            ++i;
        }
        out.push_back(value[i]);
    }
    return out;
}

std::wstring BrandingController::oem(std::wstring_view field) const {
    const auto* op = m_state.changes().find(OpKind::SetRegistryValue, oemTarget(field));
    return op ? unquoted(op->value) : std::wstring();
}

void BrandingController::setOem(std::wstring_view field, const std::wstring& typed) {
    // As Ayarlar › OEM bilgisi does (same operation, two views): no control characters, capped.
    std::wstring text;
    for (const wchar_t c : typed) {
        if (c >= 32) {
            text.push_back(c);
        }
    }
    if (text.size() > ImageSettingsController::kTextLimit) {
        text.resize(ImageSettingsController::kTextLimit);
    }
    const std::wstring target = oemTarget(field);
    if (text.empty()) {
        m_state.unqueue(OpKind::SetRegistryValue, target);
        return;
    }
    if (const auto* op = m_state.changes().find(OpKind::SetRegistryValue, target); op && unquoted(op->value) == text) {
        return;
    }
    m_state.queue(Operation{OpKind::SetRegistryValue, target, quoted(text)});
}

std::optional<std::filesystem::path> BrandingController::picture(core::PictureSlot slot) const {
    if (const auto* op = m_state.changes().find(OpKind::SetPicture, std::wstring(core::pictureSlotKey(slot)))) {
        return std::filesystem::path(op->value);
    }
    return std::nullopt;
}

Result<void> BrandingController::setPicture(core::PictureSlot slot, const std::filesystem::path& file) {
    if (auto size = core::pictureSize(file); !size) {
        return std::unexpected(size.error());
    }
    std::vector<Operation> ops{Operation{OpKind::SetPicture, std::wstring(core::pictureSlotKey(slot)), file.wstring()}};
    if (slot == core::PictureSlot::Wallpaper) {
        for (auto& op : spotlightOffOperations()) {
            ops.push_back(std::move(op));
        }
    }
    m_state.queueMany(std::move(ops));
    return {};
}

void BrandingController::clearPicture(core::PictureSlot slot) {
    std::vector<std::pair<OpKind, std::wstring>> slots{{OpKind::SetPicture, std::wstring(core::pictureSlotKey(slot))}};
    if (slot == core::PictureSlot::Wallpaper) {
        for (const auto& op : spotlightOffOperations()) {
            // Only what the wallpaper put there (the same value); a different one is someone else's.
            if (const auto* queued = m_state.changes().find(op.kind, op.target); queued && queued->value == op.value) {
                slots.emplace_back(op.kind, op.target);
            }
        }
    }
    m_state.unqueueMany(slots);
}

std::vector<Operation> BrandingController::spotlightOffOperations() {
    // D-062: on 24H2+ a new user's desktop can start with Windows Spotlight (Bing pictures), which
    // replaces the image's own wallpaper. The policy keeps Spotlight off the desktop; its "Learn
    // about this picture" icon goes with it. First logon: Windows writes the user's settings then.
    const auto write = [](const wchar_t* key, const wchar_t* name) {
        Operation op{OpKind::SetRegistryFirstLogon, std::wstring(key) + L"::" + name, L"dword:00000001"};
        op.risk = core::ops::Risk::Low;
        return op;
    };
    return {write(L"HKCU\\Software\\Policies\\Microsoft\\Windows\\CloudContent", L"DisableSpotlightCollectionOnDesktop"),
            write(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
                  L"{2cc5ca98-6485-489a-920e-b3e88a6ccce3}")};
}

std::vector<BrandingController::Font> BrandingController::fonts() const {
    std::vector<Font> list;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind != OpKind::AddFont) {
            continue;
        }
        auto it = m_fontNames.find(op.value);
        if (it == m_fontNames.end()) {
            auto info = core::readFontInfo(op.value);
            it = m_fontNames.emplace(op.value, info ? info->registryName() : op.target).first;
        }
        list.push_back(Font{op.target, it->second, op.value, static_cast<std::uint64_t>(std::max<std::int64_t>(op.sizeDelta, 0))});
    }
    return list;
}

std::size_t BrandingController::addFonts(const std::vector<std::filesystem::path>& files, std::vector<std::wstring>* refused) {
    std::vector<Operation> ops;
    for (const auto& file : files) {
        auto info = core::readFontInfo(file);
        if (!core::isFontFile(file) || !info) {
            if (refused) {
                refused->push_back(file.filename().wstring());
            }
            continue;
        }
        m_fontNames[file.wstring()] = info->registryName();
        Operation op{OpKind::AddFont, core::fontFileName(file), file.wstring()};
        std::error_code ec;
        op.sizeDelta = static_cast<std::int64_t>(std::filesystem::file_size(file, ec));
        ops.push_back(std::move(op));
    }
    if (!ops.empty()) {
        m_state.queueMany(ops);
    }
    return ops.size();
}

void BrandingController::removeFont(const std::wstring& file) {
    m_state.unqueue(OpKind::AddFont, file);
}

int BrandingController::changedCount() const {
    int n = 0;
    for (const auto field : core::kOemFields) {
        n += m_state.changes().find(OpKind::SetRegistryValue, oemTarget(field)) ? 1 : 0;
    }
    for (const auto slot : kSlots) {
        n += picture(slot) ? 1 : 0;
    }
    for (const auto& op : m_state.changes().operations()) {
        n += op.kind == OpKind::AddFont ? 1 : 0;
    }
    return n;
}

} // namespace wl::app
