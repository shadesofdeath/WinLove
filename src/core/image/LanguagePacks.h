#pragma once
// D-053: language packs and language features from Microsoft's "Languages and Optional Features"
// media (or the langpacks folder of older ones), recognised by their file names:
//   Microsoft-Windows-Client-Language-Pack_x64_tr-tr.cab                         the language pack
//   Microsoft-Windows-LanguageFeatures-Basic-tr-tr-Package~31bf3856ad364e35~amd64~~.cab   spelling, typing
//   …-Handwriting-… -OCR-… -Speech-… -TextToSpeech-…                             the rest of the language
// They go into the image as AddPackage operations of kind "language" (DismAddPackage takes the
// cabs), ordered after the servicing stack and before the cumulative update; the UI language is
// then set with SetIntl (Intl.h).
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct LanguagePackFile {
    enum class Kind : std::uint8_t { LanguagePack, Basic, Handwriting, Ocr, Speech, TextToSpeech, Fonts, Other };
    std::filesystem::path path;
    Kind kind = Kind::Other;
    std::wstring language;     // "tr-TR" (empty for fonts)
    std::wstring architecture; // "x64", "arm64", "x86" (amd64 → x64)
    std::uint64_t size = 0;
};

// Empty `language` and Kind::Other when the name is not one of Microsoft's language files.
[[nodiscard]] LanguagePackFile classifyLanguageFile(const std::filesystem::path& file);
[[nodiscard]] bool isLanguageFile(const std::filesystem::path& file);
// Every language file under `folder` (recursive), language pack first, then the features.
[[nodiscard]] std::vector<LanguagePackFile> scanLanguageFiles(const std::filesystem::path& folder);
// "tr-tr" → "tr-TR", "sr-latn-rs" → "sr-Latn-RS".
[[nodiscard]] std::wstring canonicalLanguageTag(std::wstring_view tag);

} // namespace wl::core
