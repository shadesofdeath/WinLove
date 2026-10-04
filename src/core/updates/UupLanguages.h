#pragma once
// D-061: the language files of an image's own Windows build, found automatically — what NTLite's
// "download languages" does. Microsoft publishes the language packs and features of every build
// only through Windows Update (UUP); uupdump.net indexes those and its JSON API lists every file
// of a build with size, SHA-1 / SHA-256 and a Windows Update download address:
//   1. api.uupdump.net/listid.php?search=26200.8037  → the build's update id(s) per architecture
//   2. api.uupdump.net/get.php?id=<uuid>              → its files (12 000+), with links
//   3. the files of the chosen languages are downloaded straight from Microsoft's servers
//      (*.microsoft.com / *.windowsupdate.com; nothing else is accepted) and checked against the
//      SHA-256 the list gives; each is saved under the name DISM needs (cbsFileName).
// uupdump.net only hands out the list; the bytes come from Microsoft. Parsing is pure and
// unit-tested; nothing here needs admin.
#include "base/Result.h"
#include "core/image/LanguagePacks.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct UupBuild {
    std::wstring uuid;
    std::wstring title; // "Windows 11, version 25H2 (26200.8037)"
    int build = 0, revision = 0;
    std::wstring arch;  // "amd64" | "arm64" | "x86"
    std::int64_t created = 0;
};

struct UupFile {
    std::wstring name;
    std::uint64_t size = 0;
    std::wstring sha1, sha256; // lower-case hex
    std::wstring url;          // empty when the list was asked for without links
};

// A file of a language, classified by its name.
struct UupLanguageFile {
    LanguagePackFile file; // path = the UUP file name, size from the list
    UupFile source;
};

// Everything one language has in a build: the pack, its features, the fonts its script needs and
// the languages of components (satellites; whether the image has the component is the caller's).
struct UupLanguage {
    std::wstring language; // "tr-TR"
    std::vector<UupLanguageFile> files; // install order
    [[nodiscard]] const UupLanguageFile* find(LanguagePackFile::Kind kind) const;
};

// ---- pure (unit-tested) -----------------------------------------------------------------------
[[nodiscard]] Result<std::vector<UupBuild>> parseUupBuilds(std::string_view json);
// The build for the image: same build and revision, else the newest release of the same build
// (language files are the build's base version, the same for every revision); same architecture.
[[nodiscard]] std::optional<UupBuild> pickUupBuild(std::span<const UupBuild> builds, int build, int revision,
                                                   std::wstring_view architecture /* x64 | arm64 | x86 */);
[[nodiscard]] Result<std::vector<UupFile>> parseUupFiles(std::string_view json);
// The languages that have a language pack in `files`, by tag, for the image's architecture.
[[nodiscard]] std::vector<UupLanguage> uupLanguages(std::span<const UupFile> files, std::wstring_view architecture);
// Microsoft's download hosts (http is fine: the SHA-256 is checked).
[[nodiscard]] bool trustedUupUrl(std::wstring_view url);
// The name a file is saved under: cbsFileName (what DISM looks for), else the UUP name.
[[nodiscard]] std::wstring uupSaveName(const UupLanguageFile& file);

// ---- network ----------------------------------------------------------------------------------
[[nodiscard]] Result<UupBuild> findUupBuild(int build, int revision, std::wstring_view architecture, const CancelToken& cancel);
[[nodiscard]] Result<std::vector<UupFile>> uupFiles(std::wstring_view uuid, const CancelToken& cancel);
// Downloads `files` into `folder` (a file already there with the right SHA-256 is kept), each
// checked; progress over all bytes. Returns the saved paths, in the order given.
[[nodiscard]] Result<std::vector<std::filesystem::path>> downloadUupFiles(std::span<const UupLanguageFile> files,
                                                                          const std::filesystem::path& folder,
                                                                          const TaskContext& task);

} // namespace wl::core
