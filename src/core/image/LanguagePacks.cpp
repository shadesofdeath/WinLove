#include "core/image/LanguagePacks.h"

#include "base/Text.h"

#include <algorithm>
#include <cwctype>

namespace wl::core {

namespace {

std::wstring archFrom(std::wstring_view token) {
    const std::wstring t = text::lower(token);
    if (t == L"amd64" || t == L"x64") {
        return L"x64";
    }
    if (t == L"arm64") {
        return L"arm64";
    }
    if (t == L"x86" || t == L"wow64") {
        return L"x86";
    }
    return {};
}

// "tr-tr-Package~…" → "tr-tr": the language part of a feature name, up to "-package".
std::wstring languageBefore(std::wstring_view rest) {
    const auto end = rest.find(L"-package");
    return end == std::wstring_view::npos ? std::wstring() : std::wstring(rest.substr(0, end));
}

} // namespace

std::wstring canonicalLanguageTag(std::wstring_view tag) {
    std::wstring out;
    std::size_t start = 0;
    int part = 0;
    while (start <= tag.size()) {
        const std::size_t end = std::min(tag.find(L'-', start), tag.size());
        std::wstring p(tag.substr(start, end - start));
        if (part == 0) {
            p = text::lower(p);
        } else if (p.size() == 4) { // script: "Latn"
            p = text::lower(p);
            if (!p.empty()) {
                p[0] = static_cast<wchar_t>(std::towupper(p[0]));
            }
        } else { // region: "TR"
            for (auto& c : p) {
                c = static_cast<wchar_t>(std::towupper(c));
            }
        }
        out += (part ? L"-" : L"") + p;
        ++part;
        start = end + 1;
    }
    return out;
}

LanguagePackFile classifyLanguageFile(const std::filesystem::path& file) {
    LanguagePackFile f;
    f.path = file;
    std::error_code ec;
    f.size = std::filesystem::file_size(file, ec);
    if (ec) {
        f.size = 0; // file_size gives -1 on failure
    }
    if (text::lower(file.extension().wstring()) != L".cab") {
        return f;
    }
    const std::wstring name = text::lower(file.stem().wstring());
    constexpr std::wstring_view lp = L"microsoft-windows-client-language-pack_";
    constexpr std::wstring_view feature = L"microsoft-windows-languagefeatures-";
    if (name.starts_with(lp)) {
        // "…_x64_tr-tr"
        const std::wstring rest = name.substr(lp.size());
        const auto underscore = rest.find(L'_');
        if (underscore != std::wstring::npos) {
            f.architecture = archFrom(rest.substr(0, underscore));
            f.language = canonicalLanguageTag(rest.substr(underscore + 1));
            f.kind = LanguagePackFile::Kind::LanguagePack;
        }
        return f;
    }
    if (name.starts_with(feature)) {
        const std::wstring rest = name.substr(feature.size()); // "basic-tr-tr-package~31bf…~amd64~~"
        struct Known {
            std::wstring_view prefix;
            LanguagePackFile::Kind kind;
        };
        static constexpr Known kFeatures[] = {
            {L"basic-", LanguagePackFile::Kind::Basic},
            {L"handwriting-", LanguagePackFile::Kind::Handwriting},
            {L"ocr-", LanguagePackFile::Kind::Ocr},
            {L"speech-", LanguagePackFile::Kind::Speech},
            {L"texttospeech-", LanguagePackFile::Kind::TextToSpeech},
            {L"fonts-", LanguagePackFile::Kind::Fonts},
        };
        for (const auto& known : kFeatures) {
            if (rest.starts_with(known.prefix)) {
                f.kind = known.kind;
                if (known.kind != LanguagePackFile::Kind::Fonts) {
                    const std::wstring lang = languageBefore(rest.substr(known.prefix.size()));
                    f.language = lang.empty() ? std::wstring() : canonicalLanguageTag(lang);
                }
                break;
            }
        }
        // "~amd64~~": the architecture between the last tildes.
        const auto tilde = rest.find(L'~');
        if (tilde != std::wstring::npos) {
            std::wstring tail = rest.substr(tilde + 1); // "31bf3856ad364e35~amd64~~"
            const auto second = tail.find(L'~');
            if (second != std::wstring::npos) {
                const auto third = tail.find(L'~', second + 1);
                f.architecture = archFrom(tail.substr(second + 1, third == std::wstring::npos ? std::wstring::npos : third - second - 1));
            }
        }
    }
    return f;
}

bool isLanguageFile(const std::filesystem::path& file) {
    return classifyLanguageFile(file).kind != LanguagePackFile::Kind::Other;
}

std::vector<LanguagePackFile> scanLanguageFiles(const std::filesystem::path& folder) {
    std::vector<LanguagePackFile> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            auto f = classifyLanguageFile(it->path());
            if (f.kind != LanguagePackFile::Kind::Other) {
                files.push_back(std::move(f));
            }
        }
    }
    std::ranges::stable_sort(files, [](const LanguagePackFile& a, const LanguagePackFile& b) {
        if (a.language != b.language) {
            return a.language < b.language;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return files;
}

} // namespace wl::core
