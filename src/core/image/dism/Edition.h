#pragma once
// The edition of a mounted image and the ones it can be turned into: a Home image carries what
// Pro, Education … need, and DISM switches it over offline. The DISM API has no call for it, so
// this is Windows' own
//   dism.exe /Image:<mount> /Get-CurrentEdition | /Get-TargetEditions | /Set-Edition:<id>
// A change is one-way (there is no going back to a lower edition) and belongs at the start of a
// run, before anything leaves pending operations in the image.
#include "core/image/ImageInfo.h"
#include "core/image/dism/Dism.h"
#include "core/image/wim/WimGapi.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct ImageEditions {
    std::wstring current;              // "Core"
    std::vector<std::wstring> targets; // what /Set-Edition accepts, in DISM's order

    [[nodiscard]] bool operator==(const ImageEditions&) const = default;
};

// From dism.exe's /English output: "Current Edition : Core", "Target Edition : Professional".
[[nodiscard]] ImageEditions parseEditions(std::string_view currentOutput, std::string_view targetsOutput);
// Letters and digits only: what goes onto the command line after /Set-Edition:.
[[nodiscard]] bool isEditionId(std::wstring_view text) noexcept;
// "Professional" + build 26200 → "Windows 11 Pro": the name an image gets after the change.
// Ids without a known name give "Windows 11 <id>".
[[nodiscard]] std::wstring editionDisplayName(std::wstring_view editionId, int build);

// What the WIM should say about an edition once its image has become `newEditionId`. The commit
// records the new edition id by itself but leaves the texts: an image that was "Windows 11 Home"
// would go on being listed as Home. A name (description) still at Microsoft's default for the old
// edition becomes the new edition's; one the user gave stays. FLAGS always follow the edition.
[[nodiscard]] ImageText textAfterEditionChange(const ImageInfo& image, std::wstring_view newEditionId);

[[nodiscard]] Result<ImageEditions> readEditions(DismSession& session);
// Not cancellable once dism.exe runs.
[[nodiscard]] Result<void> setEdition(DismSession& session, std::wstring_view editionId, const TaskContext& task);

} // namespace wl::core
