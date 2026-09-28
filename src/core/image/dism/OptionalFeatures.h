#pragma once
// P04 data: Windows optional features + Features on Demand (capabilities) of a mounted image,
// with the display names and sizes DISM reads from the image itself (so they are in the image's
// language). Capabilities that are not present are left out: adding one offline needs the FoD
// source media, which WinLove does not handle yet.
#include "core/image/dism/Dism.h"

#include <vector>

namespace wl::core {

struct OptionalFeature {
    enum class Kind : std::uint8_t { Feature, Capability };
    Kind kind = Kind::Feature;
    std::wstring name;          // "Microsoft-Windows-Subsystem-Linux", "OpenSSH.Client~~~~0.0.1.0"
    std::wstring displayName;   // falls back to name
    std::wstring description;
    ServicingState state = ServicingState::NotPresent;
    std::uint64_t size = 0;     // install size (capabilities); 0 = unknown
    bool restartRequired = false;

    // Enabled / installed (or about to be): the "on" side of the toggle.
    [[nodiscard]] bool isOn() const noexcept;
};

// Reads everything in one DISM session; reports progress per item (details are one call each).
[[nodiscard]] Result<std::vector<OptionalFeature>> readOptionalFeatures(Dism& dism, const std::filesystem::path& mountDir,
                                                                        const TaskContext& task);

} // namespace wl::core
