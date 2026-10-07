#pragma once
// Who signed a file, as Windows checks it: its own Authenticode signature (an .msix, an installer) or
// — for Windows' own files, which carry none — a catalog of this PC that lists the file's hash
// (CatRoot). Empty when neither verifies. No revocation check (offline use).
#include <filesystem>
#include <string>

namespace wl::core {

[[nodiscard]] std::wstring embeddedSigner(const std::filesystem::path& file);
[[nodiscard]] std::wstring catalogSigner(const std::filesystem::path& file);
// Either of them.
[[nodiscard]] std::wstring trustedSigner(const std::filesystem::path& file);
// Signed by Microsoft ("Microsoft Windows", "Microsoft Corporation", "Microsoft Windows Production PCA…").
[[nodiscard]] bool signedByMicrosoft(const std::filesystem::path& file);

} // namespace wl::core
