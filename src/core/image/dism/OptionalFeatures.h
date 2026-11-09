#pragma once
// P04 data: Windows optional features + Features on Demand (capabilities) of a mounted image,
// with the display names and sizes DISM reads from the image itself (so they are in the image's
// language). Capabilities that are not present are left out: adding one offline needs the FoD
// source media, which WinLove does not handle yet.
#include "core/image/dism/Dism.h"

#include <map>
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
    // Capability whose package is marked permanence="permanent" in its .mum: DISM refuses to
    // remove it (0x800F0825), e.g. the Defender for Endpoint client.
    bool permanent = false;
    // D-114 (the reference tool's Özellikler tree): the feature this one hangs under ("NetFx4-AdvSrvs" for
    // "NetFx4Extended-ASPNET45"), from the packages' manifests; empty for a root.
    std::wstring parent;

    // Enabled / installed (or about to be): the "on" side of the toggle.
    [[nodiscard]] bool isOn() const noexcept;
};

// Capability names ("Microsoft.Windows.Sense.Client") a package manifest (.mum text) declares, when
// the manifest marks its package permanent; empty otherwise.
[[nodiscard]] std::vector<std::wstring> permanentCapabilitiesIn(std::string_view mum);
// The permanent capabilities of a mounted image: the manifests of its visible, installed packages
// (Windows\servicing\Packages). Needs the image's SOFTWARE hive (elevated); empty when unreadable.
[[nodiscard]] std::vector<std::wstring> readPermanentCapabilities(const std::filesystem::path& mountDir);

// D-114: feature → parent feature, as the image's package manifests declare it: an <update> that
// is applicable only with another one (<applicable><detectUpdate><update name="P"/>), or a server
// component's Parent="<UniqueName>" mapped back to the update that deploys it.
[[nodiscard]] std::map<std::wstring, std::wstring> featureParentsIn(std::string_view mum);
[[nodiscard]] std::map<std::wstring, std::wstring> readFeatureParents(const std::filesystem::path& mountDir);

// Reads everything in one DISM session; reports progress per item (details are one call each).
[[nodiscard]] Result<std::vector<OptionalFeature>> readOptionalFeatures(Dism& dism, const std::filesystem::path& mountDir,
                                                                        const TaskContext& task);

} // namespace wl::core
