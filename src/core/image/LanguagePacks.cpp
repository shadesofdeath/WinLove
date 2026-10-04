#include "core/image/LanguagePacks.h"

#include "base/Text.h"

#include <algorithm>
#include <cwctype>

namespace wl::core {

namespace {

constexpr std::wstring_view kToken = L"~31bf3856ad364e35~";

std::wstring archFrom(std::wstring_view token) {
    const std::wstring t = text::lower(token);
    if (t == L"amd64" || t == L"x64" || t == L"wow64") {
        return L"x64";
    }
    if (t == L"arm64") {
        return L"arm64";
    }
    if (t == L"x86") {
        return L"x86";
    }
    return {};
}

bool isArchToken(std::wstring_view t) {
    return t == L"amd64" || t == L"wow64" || t == L"arm64" || t == L"x86";
}

bool isHex(std::wstring_view s) {
    return std::ranges::all_of(s, [](wchar_t c) { return std::iswxdigit(c) != 0; });
}

// "tr-tr-Package~…" / "tr-tr-package-amd64" → "tr-tr": the language part of a feature name.
std::wstring languageBefore(std::wstring_view rest) {
    const auto end = rest.find(L"-package");
    return end == std::wstring_view::npos ? std::wstring() : std::wstring(rest.substr(0, end));
}

// Splits "<name>~31bf3856ad364e35~<arch>~<lang>~<version>" (the LoF / CBS identity form).
struct Identity {
    std::wstring name, arch, language;
};
bool splitIdentity(std::wstring_view stem, Identity& out) {
    const auto token = text::lower(stem).find(kToken);
    if (token == std::wstring::npos) {
        return false;
    }
    out.name = std::wstring(stem.substr(0, token));
    std::wstring_view rest = stem.substr(token + kToken.size()); // "amd64~en-US~"
    const auto t1 = rest.find(L'~');
    out.arch = text::lower(rest.substr(0, t1));
    if (t1 == std::wstring_view::npos) {
        return !out.arch.empty();
    }
    rest = rest.substr(t1 + 1);
    out.language = std::wstring(rest.substr(0, rest.find(L'~')));
    return true;
}

void classifyFeature(LanguagePackFile& f, std::wstring_view rest /* lower, after "…languagefeatures-" */) {
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
        if (!rest.starts_with(known.prefix)) {
            continue;
        }
        const std::wstring lang = languageBefore(rest.substr(known.prefix.size()));
        if (lang.empty()) {
            return;
        }
        f.kind = known.kind;
        if (known.kind == LanguagePackFile::Kind::Fonts) {
            std::wstring script = lang; // "jpan" → "Jpan"
            script[0] = static_cast<wchar_t>(std::towupper(script[0]));
            f.component = script;
        } else {
            f.language = canonicalLanguageTag(lang);
        }
        return;
    }
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

bool isExpressMetadata(const std::filesystem::path& file) {
    if (text::lower(file.extension().wstring()) != L".cab") {
        return false;
    }
    const std::wstring stem = text::lower(file.stem().wstring());
    if (stem.find(kToken) != std::wstring::npos) {
        return false; // a LoF name
    }
    if (stem.ends_with(L"-package")) {
        return stem.starts_with(L"microsoft-windows-languagefeatures-"); // "…-Package.cab": no architecture
    }
    const auto underscore = stem.rfind(L'_');
    return underscore != std::wstring::npos && stem.size() - underscore == 9 && isHex(std::wstring_view(stem).substr(underscore + 1)) &&
           stem.find(L"-package-") != std::wstring::npos;
}

LanguagePackFile classifyLanguageFile(const std::filesystem::path& file) {
    LanguagePackFile f = classifyLanguageName(file);
    std::error_code ec;
    f.size = std::filesystem::file_size(file, ec);
    if (ec) {
        f.size = 0; // file_size gives -1 on failure
    }
    return f;
}

LanguagePackFile classifyPackageIdentity(std::wstring_view identity) {
    LanguagePackFile f = classifyLanguageName(std::wstring(identity) + L".cab");
    f.path.clear();
    return f;
}

LanguagePackFile classifyLanguageName(const std::filesystem::path& file) {
    LanguagePackFile f;
    f.path = file;
    const std::wstring ext = text::lower(file.extension().wstring());
    if ((ext != L".cab" && ext != L".esd") || isExpressMetadata(file)) {
        return f;
    }
    const std::wstring stem = file.stem().wstring();
    const std::wstring name = text::lower(stem);

    // The language pack. LoF: "…client-language-pack_x64_tr-tr.cab".
    constexpr std::wstring_view lofPack = L"microsoft-windows-client-language-pack_";
    if (name.starts_with(lofPack)) {
        const std::wstring rest = name.substr(lofPack.size());
        const auto underscore = rest.find(L'_');
        if (ext == L".cab" && underscore != std::wstring::npos) {
            f.packageArch = rest.substr(0, underscore) == L"x64" ? L"amd64" : rest.substr(0, underscore);
            f.architecture = archFrom(rest.substr(0, underscore));
            f.language = canonicalLanguageTag(rest.substr(underscore + 1));
            f.kind = LanguagePackFile::Kind::LanguagePack;
        }
        return f;
    }
    // UUP: "…client-languagepack-package-amd64-tr-tr.esd" (older builds: "…-package_tr-tr-amd64-tr-tr").
    constexpr std::wstring_view uupPack = L"microsoft-windows-client-languagepack-package";
    if (name.starts_with(uupPack)) {
        if (name.find(kToken) != std::wstring::npos) { // a CBS identity name: "…package~31bf…~amd64~tr-tr~…"
            Identity id;
            if (splitIdentity(stem, id) && !id.language.empty()) {
                f.packageArch = id.arch;
                f.architecture = archFrom(id.arch);
                f.language = canonicalLanguageTag(id.language);
                f.kind = LanguagePackFile::Kind::LanguagePack;
            }
            return f;
        }
        for (const std::wstring_view arch : {L"-amd64-", L"-arm64-", L"-x86-"}) {
            const auto at = name.rfind(arch);
            if (at != std::wstring::npos && at + arch.size() < name.size()) {
                f.packageArch = std::wstring(arch.substr(1, arch.size() - 2));
                f.architecture = archFrom(f.packageArch);
                f.language = canonicalLanguageTag(name.substr(at + arch.size()));
                f.kind = LanguagePackFile::Kind::LanguagePack;
                break;
            }
        }
        return f;
    }
    if (ext != L".cab") {
        return f;
    }

    // Language features: "…languagefeatures-basic-tr-tr-package~31bf…~amd64~~" or "…-package-amd64".
    constexpr std::wstring_view feature = L"microsoft-windows-languagefeatures-";
    if (name.starts_with(feature)) {
        classifyFeature(f, std::wstring_view(name).substr(feature.size()));
        if (f.kind == LanguagePackFile::Kind::Other) {
            return f;
        }
        Identity id;
        if (splitIdentity(stem, id)) {
            f.packageArch = id.arch;
        } else if (const auto dash = name.rfind(L'-'); dash != std::wstring::npos && isArchToken(std::wstring_view(name).substr(dash + 1))) {
            f.packageArch = name.substr(dash + 1);
        }
        f.architecture = archFrom(f.packageArch);
        if (f.packageArch.empty()) {
            f.kind = LanguagePackFile::Kind::Other;
        }
        return f;
    }

    // A component's language resources (satellite). LoF: "<name>~31bf…~amd64~en-US~";
    // UUP: "<name>-package-amd64-en-us". The language-neutral package itself is no language file.
    Identity id;
    if (splitIdentity(stem, id)) {
        if (!id.language.empty() && text::lower(id.name).ends_with(L"-package") && isArchToken(id.arch)) {
            f.kind = LanguagePackFile::Kind::Satellite;
            f.component = id.name;
            f.packageArch = id.arch;
            f.architecture = archFrom(id.arch);
            f.language = canonicalLanguageTag(id.language);
        }
        return f;
    }
    for (const std::wstring_view arch : {L"-package-amd64-", L"-package-wow64-", L"-package-arm64-", L"-package-x86-"}) {
        const auto at = name.rfind(arch);
        if (at == std::wstring::npos || at + arch.size() >= name.size()) {
            continue;
        }
        const std::wstring lang = name.substr(at + arch.size());
        // "en-us", "sr-latn-rs": letters and dashes only, a dash inside.
        if (lang.find(L'-') == std::wstring::npos || !std::ranges::all_of(lang, [](wchar_t c) { return c == L'-' || std::iswalpha(c); })) {
            continue;
        }
        f.kind = LanguagePackFile::Kind::Satellite;
        f.component = stem.substr(0, at + 8); // "…-Package" in the file's own spelling
        f.packageArch = std::wstring(arch.substr(9, arch.size() - 10));
        f.architecture = archFrom(f.packageArch);
        f.language = canonicalLanguageTag(lang);
        break;
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
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        return a.path.filename() < b.path.filename();
    });
    return files;
}

std::wstring cbsFileName(const LanguagePackFile& file) {
    using K = LanguagePackFile::Kind;
    if (file.kind == K::LanguagePack || file.kind == K::Other || file.packageArch.empty()) {
        return {};
    }
    const std::wstring stem = file.path.stem().wstring();
    if (file.kind == K::Satellite) {
        return file.component + std::wstring(kToken) + file.packageArch + L"~" + file.language + L"~.cab";
    }
    // A feature: the name up to "-Package", in the file's own spelling.
    const auto end = text::lower(stem).find(L"-package");
    if (end == std::wstring::npos) {
        return {};
    }
    return stem.substr(0, end + 8) + std::wstring(kToken) + file.packageArch + L"~~.cab";
}

std::vector<LanguagePackFile::Kind> featureDependencies(LanguagePackFile::Kind kind) {
    using K = LanguagePackFile::Kind;
    switch (kind) {
    case K::Speech: return {K::Basic, K::TextToSpeech};
    case K::Handwriting:
    case K::Ocr:
    case K::TextToSpeech: return {K::Basic};
    default: return {};
    }
}

std::vector<std::wstring> requiredFontScripts(std::wstring_view language) {
    // Microsoft's table of font capabilities ("Language.Fonts.<Script>") by language.
    struct Entry {
        std::wstring_view prefix; // "ja", "zh-cn": a language, or a language-region
        std::wstring_view script;
    };
    static constexpr Entry kFonts[] = {
        {L"ar", L"Arab"},   {L"fa", L"Arab"},   {L"ur", L"Arab"},   {L"ps", L"Arab"},   {L"sd-arab", L"Arab"},
        {L"ug", L"Arab"},   {L"ku-arab", L"Arab"}, {L"pa-arab", L"Arab"}, {L"bn", L"Beng"}, {L"as", L"Beng"},
        {L"chr", L"Cher"},  {L"hi", L"Deva"},   {L"mr", L"Deva"},   {L"ne", L"Deva"},   {L"kok", L"Deva"},
        {L"am", L"Ethi"},   {L"ti", L"Ethi"},   {L"gu", L"Gujr"},   {L"pa", L"Guru"},   {L"zh-cn", L"Hans"},
        {L"zh-sg", L"Hans"}, {L"zh-tw", L"Hant"}, {L"zh-hk", L"Hant"}, {L"zh-mo", L"Hant"}, {L"he", L"Hebr"},
        {L"ja", L"Jpan"},   {L"km", L"Khmr"},   {L"kn", L"Knda"},   {L"ko", L"Kore"},   {L"lo", L"Laoo"},
        {L"ml", L"Mlym"},   {L"or", L"Orya"},   {L"si", L"Sinh"},   {L"syr", L"Syrc"},  {L"ta", L"Taml"},
        {L"te", L"Telu"},   {L"th", L"Thai"},
    };
    const std::wstring tag = text::lower(language);
    std::vector<std::wstring> scripts;
    for (const auto& e : kFonts) {
        // "pa" must not take "pa-arab-pk" (Arab): the longest matching prefix wins.
        if (tag == e.prefix || tag.starts_with(std::wstring(e.prefix) + L"-")) {
            if (std::ranges::none_of(kFonts, [&](const Entry& longer) {
                    return longer.prefix.size() > e.prefix.size() && longer.prefix.starts_with(e.prefix) &&
                           (tag == longer.prefix || tag.starts_with(std::wstring(longer.prefix) + L"-"));
                })) {
                scripts.emplace_back(e.script);
            }
        }
    }
    return scripts;
}

bool satelliteFits(const LanguagePackFile& file, std::span<const std::wstring> installedPackages) {
    if (file.kind != LanguagePackFile::Kind::Satellite) {
        return false;
    }
    const std::wstring component = text::lower(file.component);
    const std::wstring arch = text::lower(file.packageArch);
    return std::ranges::any_of(installedPackages, [&](const std::wstring& identity) {
        Identity id;
        return splitIdentity(identity, id) && id.language.empty() && id.arch == arch && text::lower(id.name) == component;
    });
}

} // namespace wl::core
