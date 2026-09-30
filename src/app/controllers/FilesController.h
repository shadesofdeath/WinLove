#pragma once
// D-051 logic of the Dosyalar page: files and folders of this PC copied into the image. Each is
// one CopyTree operation — target = where it ends up ("Tools\Sysinternals", shown as
// C:\Tools\Sysinternals), value = the source here. The places offered first are where people
// put things for every user; any other place under the image root works too, except what only
// Windows may write (ImageFiles.h: validateTreeTarget).
#include "app/Localization.h"
#include "app/state/AppState.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::app {

class FilesController {
public:
    struct Place {
        std::wstring folder; // in the image, from its root ("Users\Public\Desktop"); "" = the root
        Str label;
    };
    [[nodiscard]] static const std::vector<Place>& places();

    // "C:\Tools", "c:/tools/", "Tools" → "Tools"; empty when it is not under the system drive.
    [[nodiscard]] static std::wstring imageFolderFrom(std::wstring_view typed);
    // What the installed system shows: "C:\" + folder.
    [[nodiscard]] static std::wstring displayPath(std::wstring_view target);

    // The operation that copies `source` into `folder` of the image (keeping its name).
    [[nodiscard]] static Result<core::ops::Operation> operationFor(const std::filesystem::path& source,
                                                                   std::wstring_view folder);

    explicit FilesController(AppState& state) : m_state(state) {}
    // Queues every source into `folder`; returns how many were queued (errors are logged and returned).
    [[nodiscard]] std::pair<int, std::vector<Error>> add(const std::vector<std::filesystem::path>& sources,
                                                         std::wstring_view folder);
    [[nodiscard]] std::vector<core::ops::Operation> queued() const;
    [[nodiscard]] int count() const;

private:
    AppState& m_state;
};

} // namespace wl::app
