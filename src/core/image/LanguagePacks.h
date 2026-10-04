#pragma once
// D-053 / D-061: language packs and language features, recognised by their file names. Two
// namings exist for the same packages:
//   Microsoft's "Languages and Optional Features" media (LoF ISO):
//     Microsoft-Windows-Client-Language-Pack_x64_tr-tr.cab                                  the language pack
//     Microsoft-Windows-LanguageFeatures-Basic-tr-tr-Package~31bf3856ad364e35~amd64~~.cab    spelling, typing
//     Microsoft-Windows-Notepad-System-FoD-Package~31bf3856ad364e35~amd64~tr-TR~.cab         a component's language
//   Windows Update (UUP, as uupdump.net lists them):
//     Microsoft-Windows-Client-LanguagePack-Package-amd64-tr-TR.esd                         the pack, an ESD
//     Microsoft-Windows-LanguageFeatures-Basic-tr-tr-Package-amd64.cab
//     Microsoft-Windows-Notepad-System-FoD-Package-amd64-tr-TR.cab
//     …-Package-amd64_cc2063c8.cab, …-Package.cab                                          express metadata: skipped
// DISM installs a language feature as a capability and resolves its dependencies (Speech needs
// Basic and TextToSpeech) from the cab's own folder BY THE LoF NAME: a UUP-named cab fails with
// 0x800F0912 (CBS_E_ONDEMAND_LOCALSOURCE_NOT_FOUND) — cbsFileName() gives the name it looks for
// (LanguageInstall.h stages the file under it). The UUP language pack is an ESD holding the
// expanded package: it is applied to a folder that DISM takes as the package (ENGINE.md).
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct LanguagePackFile {
    // Also the install order: the pack first, then what the features depend on.
    enum class Kind : std::uint8_t { LanguagePack, Basic, Fonts, Handwriting, Ocr, TextToSpeech, Speech, Satellite, Other };
    std::filesystem::path path;
    Kind kind = Kind::Other;
    std::wstring language;     // "tr-TR" (empty for fonts)
    std::wstring architecture; // the image's: "x64", "arm64", "x86" (amd64 / wow64 → x64)
    std::wstring packageArch;  // the package's own: "amd64", "wow64", "arm64", "x86"
    std::wstring component;    // Satellite: the package it localises ("Microsoft-Windows-Notepad-System-FoD-Package");
                               // Fonts: the script ("Jpan")
    std::uint64_t size = 0;
};

// Kind::Other when the name is not one of Microsoft's language files (express metadata included).
[[nodiscard]] LanguagePackFile classifyLanguageFile(const std::filesystem::path& file); // + its size on disk
[[nodiscard]] LanguagePackFile classifyLanguageName(const std::filesystem::path& file); // the name only, no I/O
// An installed package of an image ("Microsoft-Windows-LanguageFeatures-Basic-tr-tr-Package~31bf3856ad364e35~amd64~~10.0.26100.1"):
// what language file it came from (path empty).
[[nodiscard]] LanguagePackFile classifyPackageIdentity(std::wstring_view identity);
[[nodiscard]] bool isLanguageFile(const std::filesystem::path& file);
// UUP's express (PSF) metadata next to a cab: "…-Package-amd64_80a67e0b.cab", "…-Package.cab".
// Not a package — DISM answers 0x80070002.
[[nodiscard]] bool isExpressMetadata(const std::filesystem::path& file);
// Every language file under `folder` (recursive), in install order per language.
[[nodiscard]] std::vector<LanguagePackFile> scanLanguageFiles(const std::filesystem::path& folder);
// "tr-tr" → "tr-TR", "sr-latn-rs" → "sr-Latn-RS".
[[nodiscard]] std::wstring canonicalLanguageTag(std::wstring_view tag);

// The LoF name DISM looks for in a source folder ("…-Package~31bf3856ad364e35~amd64~~.cab");
// empty for the language pack itself (it has no capability) and for other files.
[[nodiscard]] std::wstring cbsFileName(const LanguagePackFile& file);
// Kinds the feature needs installed first (DISM: Speech → Basic + TextToSpeech, the rest → Basic).
[[nodiscard]] std::vector<LanguagePackFile::Kind> featureDependencies(LanguagePackFile::Kind kind);
// Font scripts a language's UI needs ("ja-JP" → {"Jpan"}); empty for Latin / Cyrillic / Greek ones.
[[nodiscard]] std::vector<std::wstring> requiredFontScripts(std::wstring_view language);

// Package identities of the image ("Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~wow64~~10.0.26100.1"):
// a component's language file fits when the image has the component (language-neutral, same arch).
[[nodiscard]] bool satelliteFits(const LanguagePackFile& file, std::span<const std::wstring> installedPackages);

} // namespace wl::core
