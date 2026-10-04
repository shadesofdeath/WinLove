#pragma once
// D-056 Kişiselleştirme: what the page edits, all of it as queue operations (so presets carry it):
//   OEM fields   SetRegistryValue  OEMInformation::<field> = "text"     (empty = not queued)
//   pictures     SetPicture        target = slot key, value = source file
//   fonts        AddFont           target = file name in Windows\Fonts, value = source file
#include "app/state/AppState.h"
#include "core/image/Branding.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::app {

class BrandingController {
public:
    explicit BrandingController(AppState& state) : m_state(state) {}

    // OEM information (core::kOemFields): the queued text, "" when nothing is queued.
    [[nodiscard]] std::wstring oem(std::wstring_view field) const;
    void setOem(std::wstring_view field, const std::wstring& text);

    [[nodiscard]] std::optional<std::filesystem::path> picture(core::PictureSlot slot) const;
    // Refused (the error) when Windows cannot read the file as a picture.
    [[nodiscard]] Result<void> setPicture(core::PictureSlot slot, const std::filesystem::path& file);
    void clearPicture(core::PictureSlot slot);
    // What a wallpaper brings with it: Windows Spotlight kept off the desktop (D-062).
    [[nodiscard]] static std::vector<core::ops::Operation> spotlightOffOperations();

    struct Font {
        std::wstring file;   // in Windows\Fonts
        std::wstring name;   // registry name ("Segoe UI (TrueType)"); the file name when unreadable
        std::filesystem::path source;
        std::uint64_t size = 0;
    };
    [[nodiscard]] std::vector<Font> fonts() const;
    // Queues the fonts that parse; the others' file names go to `refused`.
    std::size_t addFonts(const std::vector<std::filesystem::path>& files, std::vector<std::wstring>* refused = nullptr);
    void removeFont(const std::wstring& file);

    [[nodiscard]] int changedCount() const; // nav badge

    // .reg string syntax ("…" with \\ and \") ↔ text.
    [[nodiscard]] static std::wstring quoted(std::wstring_view text);
    [[nodiscard]] static std::wstring unquoted(std::wstring_view value);

private:
    AppState& m_state;
    mutable std::map<std::wstring, std::wstring> m_fontNames; // source path → registry name
};

} // namespace wl::app
