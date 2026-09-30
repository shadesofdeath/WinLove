#pragma once
// D-045: reads which catalog operations the mounted image already has, so P11 / P12 show the
// image as it is instead of the Windows default: every registry write of the tweak and settings
// catalogs (offreg.dll, no hive is loaded), the text files of settings, and the string values of
// text settings. Once per mount — queued ahead of the preload on the engine thread (~0.4 s for a
// 25H2 image) — and again when an Uygula run ends with the image still mounted.
// Results land in AppState::imageValues(); AppState::imageHas() answers per operation.
#include "app/state/AppState.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wl::app {

class ImageSettingsCatalog;
class TweakCatalog;

struct ImageValueProbes {
    std::vector<core::RegistryWrite> writes;                   // does the image have this write
    std::vector<std::pair<std::wstring, std::wstring>> files; // path in the image → text
    std::vector<core::RegistryWrite> texts;                    // string values to read (text settings)

    [[nodiscard]] static ImageValueProbes from(const TweakCatalog& tweaks, const ImageSettingsCatalog& settings);
};

class ImageValuesController {
public:
    // The read (engine thread). Replaceable in unit tests; the default opens the hive files.
    using Reader = std::function<Result<AppState::ImageValues>(const std::filesystem::path& mountDir,
                                                               const ImageValueProbes& probes)>;
    [[nodiscard]] static Result<AppState::ImageValues> readImage(const std::filesystem::path& mountDir,
                                                                 const ImageValueProbes& probes);

    ImageValuesController(AppState& state, ImageValueProbes probes, std::function<void(std::function<void()>)> postToUi,
                          Reader reader = &ImageValuesController::readImage);
    ~ImageValuesController();

    // No-op without a mount or when this mount's values are there / being read (unless forced).
    void load(bool force = false);

private:
    AppState& m_state;
    ImageValueProbes m_probes;
    std::function<void(std::function<void()>)> m_post;
    Reader m_reader;
    std::size_t m_subscription = 0;
    bool m_applying = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// A write that takes the image from `write` back to the Windows default: a set value is deleted,
// a created key is deleted. Deletions cannot be undone (what was there is not known): nullopt.
[[nodiscard]] std::optional<core::RegistryWrite> revertWrite(const core::RegistryWrite& write);
// A deletion looks the same before and after it ran (and on an image that never had the value),
// so an option made only of deletions is never recognised as "in the image".
[[nodiscard]] bool assertsSomething(const core::RegistryWrite& write) noexcept;

} // namespace wl::app
