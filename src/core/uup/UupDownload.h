#pragma once
// D-093: the files of a UUP set onto the disk. Several at a time (Microsoft's CDN gives each
// connection its own pace), each one checked against its SHA-256 (SHA-1 when the API published
// none) before it counts. A second run continues: a file already there with the right hash is kept,
// a "<name>.part" continues where it stopped. The links expire (about a quarter of an hour, then
// HTTP 403): `refresh` asks the API for new ones, once per expiry.
#include "core/tasks/Task.h"
#include "core/uup/UupCatalog.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace wl::core::uup {

// Where a listed name goes under `folder`: "appx/Foo/Bar.msix" → folder\appx\Foo\Bar.msix; a name
// that would climb out (.., a drive) stays a flat file name.
[[nodiscard]] std::filesystem::path localPath(const std::filesystem::path& folder, std::wstring_view name);

// Progress: fraction of all bytes; the stage names the file in work ("download" / "verify").
[[nodiscard]] Result<void> downloadFiles(std::vector<File> files, const std::filesystem::path& folder,
                                         const std::function<Result<std::vector<File>>()>& refresh,
                                         const TaskContext& task, int connections = 4);

} // namespace wl::core::uup
