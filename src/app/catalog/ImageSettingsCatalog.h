#pragma once
// resources/catalog/settings.json: the P12 "Ayarlar / Tweaks" form. tab → section → setting →
// options. An option is a list of registry writes (.reg value syntax, validated with
// core::parseRegValue at load), service start types and text files put into the image
// (core/image/ImageFiles.h); exactly one option per setting has none of them: what Windows does
// on its own, for which nothing is written.
// Two controls take a value from the user instead of offering options: "text" (the value is
// written as a string to every key / name listed) and "file" (a JPEG of this PC is copied to the
// fixed path "copy" in the image, and the listed writes — which point Windows at it — go with it).
// For both, an empty value is the Windows default.
#include "app/Localization.h"
#include "base/Result.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/ops/ChangeSet.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wl::app {

struct LocalizedText {
    std::wstring tr;
    std::wstring en;
    [[nodiscard]] const std::wstring& get(Language language) const { return language == Language::Turkish ? tr : en; }
};

struct ImageSettingTab {
    std::string id;
    LocalizedText title;
};

struct ImageSettingSection {
    std::string id;
    std::string tab;
    LocalizedText title;
};

struct ImageSettingOption {
    std::string id;     // toggles: "off" / "on"
    LocalizedText label; // dropdown / radio text (empty for toggles)
    std::vector<core::RegistryWrite> writes;
    std::vector<std::pair<std::wstring, core::StartType>> services; // service name → start type
    std::vector<std::pair<std::wstring, std::wstring>> files;       // path in the image → text
    std::wstring copyTo;                                            // "file" control: where the picked file goes
    [[nodiscard]] bool isDefault() const noexcept {
        return writes.empty() && services.empty() && files.empty() && copyTo.empty();
    }
};

struct ImageSetting {
    enum class Control : std::uint8_t { Toggle, Dropdown, Radio, Text, File };
    std::string id;
    std::string section;
    Control control = Control::Toggle;
    LocalizedText label;
    LocalizedText hint; // optional caption right of a toggle
    core::ops::Risk risk = core::ops::Risk::Low;
    bool firstLogon = false; // "apply": "firstLogon" → SetRegistryFirstLogon
    std::vector<ImageSettingOption> options; // toggle: [0] off, [1] on · text / file: [0] default, [1] with a value
    int defaultOption = 0;
    int recommended = -1; // option "Önerilenleri uygula" picks; -1 = leave alone
};

class ImageSettingsCatalog {
public:
    // Settings that are malformed (bad write, unknown section / start type, no or several default
    // options) are skipped and logged, not fatal.
    [[nodiscard]] static Result<ImageSettingsCatalog> parse(std::string_view json);

    [[nodiscard]] const std::vector<ImageSettingTab>& tabs() const noexcept { return m_tabs; }
    [[nodiscard]] const std::vector<ImageSettingSection>& sections() const noexcept { return m_sections; }
    [[nodiscard]] const std::vector<ImageSetting>& settings() const noexcept { return m_settings; }

private:
    std::vector<ImageSettingTab> m_tabs;
    std::vector<ImageSettingSection> m_sections;
    std::vector<ImageSetting> m_settings;
};

} // namespace wl::app
