#pragma once
// What WinLove knows about one image (index) inside a WIM/ESD — from the WIM XML metadata.
#include <cstdint>
#include <string>
#include <vector>

namespace wl::core {

enum class Architecture : std::uint8_t { Unknown, X86, X64, Arm, Arm64 };

struct ImageInfo {
    int index = 0;
    std::wstring name;
    std::wstring description;
    std::wstring displayName;
    std::wstring editionId;        // "Professional"
    std::wstring installationType; // "Client", "Server", "WindowsPE"
    Architecture architecture = Architecture::Unknown;
    int major = 0;
    int minor = 0;
    int build = 0;
    int spBuild = 0;
    std::vector<std::wstring> languages;
    std::wstring defaultLanguage;
    std::uint64_t totalBytes = 0;  // expanded size
    std::uint64_t fileCount = 0;
    std::uint64_t directoryCount = 0;

    [[nodiscard]] std::wstring versionString() const; // "10.0.26200.6584"
};

[[nodiscard]] const wchar_t* architectureName(Architecture arch) noexcept; // "x64"

} // namespace wl::core
