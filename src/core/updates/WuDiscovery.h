#pragma once
// D-114: which updates Windows Update itself offers an image's build — asked of FE3, the public
// Windows Update client service (fe3.delivery.mp.microsoft.com, ClientWebService), the way Windows
// Update and Windows Setup ask it. The reference tool's own list comes from its licence server (a feed built
// from the same service; not used here): this asks the source directly.
//   GetCookie → SyncUpdates three times, each with the image's build, architecture and edition:
//     · App=WU_OS, product Client.OS.rs2.<arch>      the cumulative update and the preview one
//     · App=Setup360 DUScan=1, same product           the media-refresh set: checkpoint, enablement,
//                                                     Safe OS and Setup dynamic updates, .NET CU
//     · product Microsoft.NETFX.<arch>                the .NET Framework cumulative update
//   The KB numbers of the package files they list (Windows11.0-KB…-x64.msu / .cab, …NDP481.cab).
// Files are then found and verified through the Update Catalog (UpdateCatalog.h: SHA-256).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct WuQuery {
    int build = 0;                       // 26200
    std::wstring architecture = L"amd64"; // amd64 | arm64 | x86
    std::wstring edition = L"Professional";
    int sku = 48;
};

// "ge_release" for 26100/26200/26300 …; empty for a build Windows Update has no branch name for.
[[nodiscard]] std::wstring wuBranch(int build);
// The KB numbers ("KB5127648") of the update packages a SyncUpdates answer lists, in order, once each.
[[nodiscard]] std::vector<std::wstring> wuPackageKbs(std::string_view syncReply);
// Network (three syncs); KBs newest-looking first as Windows Update lists them.
[[nodiscard]] Result<std::vector<std::wstring>> discoverWindowsUpdateKbs(const WuQuery& query, const CancelToken& cancel);

} // namespace wl::core
