#pragma once
// D-066: apps of the Microsoft Store into an image, found by name — what store.rg-adguard.net
// shows, asked of Microsoft's own services directly (rg-adguard sits behind a bot check):
//   1. search     storeedgefd.dsx.mp.microsoft.com/v9.0/manifestSearch  → product ids
//   2. product    displaycatalog.mp.microsoft.com/v7.0/products/<id>      → WuCategoryId, family name
//   3. packages   Windows Update (fe3.delivery.mp.microsoft.com): GetCookie → SyncUpdates for the
//                 category → every package of the app and its frameworks (name, size, SHA-256)
//   4. file URL   GetExtendedUpdateInfo2 (…/client.asmx/secured) for the chosen packages
//   5. download   from *.delivery.mp.microsoft.com, SHA-256 checked, saved as <full name>.<ext>
// Free apps only (no licence is involved: the files are provisioned with /SkipLicense). The SOAP
// bodies are what the Store client sends (anonymous MSA ticket). Parsing is pure and unit-tested.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct StoreSearchResult {
    std::wstring productId; // "9NKSQGP7F2NH"
    std::wstring name;      // "WhatsApp"
    std::wstring publisher;
};

struct StoreProduct {
    std::wstring productId;
    std::wstring title;
    std::wstring publisher;
    std::wstring description;
    std::wstring wuCategoryId;      // Windows Update category of its packages
    std::wstring packageFamilyName; // "5319275A.WhatsAppDesktop_cv1g1gvanyjgm"
    std::uint64_t size = 0;         // the catalogue's largest download
};

struct StorePackage {
    std::wstring fullName;    // "5319275A.WhatsAppDesktop_2.2638.102.0_neutral_~_cv1g1gvanyjgm"
    std::wstring identityName; // "5319275A.WhatsAppDesktop"
    std::wstring extension;   // ".msixbundle", ".appx" …
    std::uint64_t size = 0;
    std::wstring sha256;      // lower-case hex
    std::wstring updateId;
    int revision = 1;
    bool framework = false;   // VCLibs, WindowsAppRuntime …
    // From the full name: version, architecture ("x64", "neutral", …).
    [[nodiscard]] std::wstring version() const;
    [[nodiscard]] std::wstring architecture() const;
    [[nodiscard]] std::wstring fileName() const { return fullName + extension; }
};

// ---- pure (unit-tested) -----------------------------------------------------------------------
[[nodiscard]] Result<std::vector<StoreSearchResult>> parseStoreSearch(std::string_view json);
[[nodiscard]] Result<StoreProduct> parseStoreProduct(std::string_view json);
[[nodiscard]] Result<std::vector<StorePackage>> parseSyncUpdates(std::string_view soap);
[[nodiscard]] std::vector<std::wstring> parseFileUrls(std::string_view soap);
// What goes into an image of `architecture` (x64 | arm64 | x86): the newest version of the app
// itself (its bundle when there is one) and, per framework, the newest version for that
// architecture (x64 images also take the x86 frameworks a bundle may ask for: not needed offline).
[[nodiscard]] std::vector<StorePackage> pickStorePackages(std::span<const StorePackage> all, std::wstring_view familyName,
                                                          std::wstring_view architecture);
[[nodiscard]] bool trustedStoreUrl(std::wstring_view url);

// ---- network ----------------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<StoreSearchResult>> searchStore(std::wstring_view query, const CancelToken& cancel);
[[nodiscard]] Result<StoreProduct> storeProduct(std::wstring_view productId, const CancelToken& cancel);
[[nodiscard]] Result<std::vector<StorePackage>> storePackages(const StoreProduct& product, const CancelToken& cancel);
// Downloads `packages` into `folder` (kept when the SHA-256 already matches); progress over all
// bytes. Returns the saved files, app first.
[[nodiscard]] Result<std::vector<std::filesystem::path>> downloadStorePackages(std::span<const StorePackage> packages,
                                                                               const std::filesystem::path& folder,
                                                                               const TaskContext& task);

} // namespace wl::core
