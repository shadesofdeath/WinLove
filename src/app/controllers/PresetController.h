#pragma once
// P15 logic (docs/pages/15-presets.md): the preset library — *.wlpreset files in
// %LOCALAPPDATA%\WinLove\presets — and what the page shows about a preset: its changes as named items
// (a registry write that is a known setting reads "Reklam kimliği: Kapalı", not the key) and
// the difference between two presets.
#include "app/Localization.h"
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/state/Preset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::app {

class PresetController {
public:
    // One change of a preset, as listed and compared.
    struct Item {
        int category = 0;     // order of the nav pages (kCategory…)
        std::wstring key;     // identity across presets
        std::wstring label;
        std::wstring value;
        // A setting also answers to the key of its first operation: "DiagTrack service: Off" in
        // one preset and a plain "DiagTrack = Manual" in the other are one changed row, not two.
        std::wstring alias;
    };
    struct DiffRow {
        enum class Mark : std::uint8_t { Added, Removed, Changed, Same };
        Mark mark = Mark::Same; // Added: only in B · Removed: only in A
        int category = 0;
        std::wstring label;
        std::wstring a; // empty = not in A
        std::wstring b;
    };
    struct DiffSummary {
        int added = 0;
        int removed = 0;
        int changed = 0;
    };

    PresetController(AppState& state, const ImageSettingsCatalog& settings, const Localization& strings,
                     Language language, std::filesystem::path folder);

    // Next to settings.json (%LOCALAPPDATA%\WinLove\presets), whatever the work folder is: moving
    // the work folder to another disk must not hide the library.
    [[nodiscard]] static std::filesystem::path defaultFolder() { return AppSettings::defaultWorkRoot() / L"presets"; }

    // Reads the library again (sorted by name). Unreadable files are skipped and logged.
    void reload();
    // Replaces the library in memory (render demo, tests): nothing is read or written.
    void adopt(std::vector<Preset> presets);
    [[nodiscard]] const std::vector<Preset>& presets() const noexcept { return m_presets; }

    // The live queue + answer file, saved under `name` (an entry of the same name is replaced).
    [[nodiscard]] Result<void> saveCurrent(const std::wstring& name);
    [[nodiscard]] Result<void> importFile(const std::filesystem::path& file);
    [[nodiscard]] Result<void> exportTo(std::size_t index, const std::filesystem::path& target) const;
    [[nodiscard]] Result<void> remove(std::size_t index);
    // Adds the preset's operations to the queue (needs a mounted image) and takes its answer
    // file. Returns the number of operations queued.
    [[nodiscard]] std::size_t apply(const Preset& preset);
    // The queue as it is now, to compare a preset with.
    [[nodiscard]] Preset current() const;

    [[nodiscard]] std::vector<Item> items(const Preset& preset) const;
    // Rows ordered by category; `includeSame` lists the identical items too.
    [[nodiscard]] std::vector<DiffRow> diff(const Preset& a, const Preset& b, bool includeSame) const;
    [[nodiscard]] static DiffSummary summarize(const std::vector<DiffRow>& rows);
    [[nodiscard]] std::wstring categoryName(int category) const;

    // "Gaming: Slim?" → "Gaming Slim" (a file name Windows accepts).
    [[nodiscard]] static std::wstring fileNameFor(std::wstring_view name);

private:
    AppState& m_state;
    const ImageSettingsCatalog& m_settings;
    const Localization& m_strings;
    Language m_language;
    std::filesystem::path m_folder;
    std::vector<Preset> m_presets;
    bool m_adopted = false; // adopt() was used: reload() keeps the library as it is
};

} // namespace wl::app
