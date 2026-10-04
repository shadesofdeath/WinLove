#include "app/controllers/LanguageController.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/UpdatePackage.h"
#include "core/image/dism/Dism.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
#include <atomic>
#include <map>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

namespace {
constexpr wchar_t kIntl[] = L"intl";

bool sameTag(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

std::wstring regString(HKEY key, const wchar_t* name) {
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    std::wstring text = buffer;
    if (text.starts_with(L"@")) { // "@%SystemRoot%\system32\input.dll,-5000"
        wchar_t resolved[512] = {};
        if (SUCCEEDED(SHLoadIndirectString(text.c_str(), resolved, static_cast<UINT>(std::size(resolved)), nullptr))) {
            return resolved;
        }
    }
    return text;
}
} // namespace

LanguageController::LanguageController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {}

LanguageController::~LanguageController() {
    *m_alive = false;
}

void LanguageController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.imageIntl();
    if (!force && current && current->mountDir == mounted->mountDir && current->status != AppState::ImageIntl::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Loading, mountDir, {}, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    struct Read {
        core::ImageIntl intl;
        std::vector<std::wstring> packages;
    };
    m_state.engine().run<Read>(
        [mountDir](const core::TaskContext&) -> Result<Read> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            auto intl = core::readIntl(**session);
            if (!intl) {
                return std::unexpected(intl.error());
            }
            Read read{std::move(*intl), {}};
            // The installed packages (~3 s): a failure costs only the per-language details.
            if (auto packages = (*session)->packages()) {
                for (auto& p : *packages) {
                    if (p.state == core::ServicingState::Installed) {
                        read.packages.push_back(std::move(p.name));
                    }
                }
            } else {
                log::warn("app", describe(packages.error()));
            }
            return read;
        },
        [this, post, alive, mountDir](Result<Read> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& mounted = m_state.mounted();
                if (!mounted || mounted->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Failed, mountDir, {}, {}, result.error()});
                    return;
                }
                m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Ready, mountDir, std::move(result->intl),
                                                         std::move(result->packages), {}});
            });
        });
}

std::wstring LanguageController::imageArchitecture() const {
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (mounted && source) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted->index) {
                return core::architectureName(image.architecture);
            }
        }
    }
    return L"x64";
}

std::vector<core::LanguagePackFile> LanguageController::fitting(const std::vector<core::LanguagePackFile>& files) const {
    const std::wstring arch = imageArchitecture();
    std::vector<core::LanguagePackFile> out;
    const auto& idx = index();
    const bool known = !imagePackages().empty();
    for (const auto& f : files) {
        if (!f.architecture.empty() && f.architecture != arch) {
            continue;
        }
        // A component's language only for a component the image has (D-061); everything while the
        // package list is not read.
        if (f.kind == core::LanguagePackFile::Kind::Satellite && known &&
            !idx.neutral.contains(text::lower(f.component + L"~" + f.packageArch))) {
            continue;
        }
        out.push_back(f);
    }
    return out;
}

Operation LanguageController::operationFor(const core::LanguagePackFile& file) {
    Operation op{OpKind::AddPackage, file.path.wstring(), core::updateKindKey(core::UpdateKind::Language)};
    op.risk = Risk::Low;
    op.sizeDelta = static_cast<std::int64_t>(file.size) * 2;
    return op;
}

bool LanguageController::queued(const core::LanguagePackFile& file) const {
    return m_state.changes().find(OpKind::AddPackage, file.path.wstring()) != nullptr;
}

void LanguageController::queuePacks(const std::vector<core::LanguagePackFile>& files) {
    std::vector<Operation> ops;
    for (const auto& f : files) {
        if (!queued(f)) {
            ops.push_back(operationFor(f));
        }
    }
    m_state.queueMany(std::move(ops));
}

std::vector<core::LanguagePackFile> LanguageController::withDependencies(std::vector<core::LanguagePackFile> chosen,
                                                                         const std::vector<core::LanguagePackFile>& available) const {
    using Kind = core::LanguagePackFile::Kind;
    const auto& idx = index();
    auto have = [&](const std::wstring& language, Kind kind) {
        if (std::ranges::any_of(chosen, [&](const core::LanguagePackFile& f) { return f.kind == kind && sameTag(f.language, language); })) {
            return true;
        }
        if (kind == Kind::LanguagePack) {
            return imageHasLanguage(language);
        }
        const auto it = idx.features.find(text::lower(language));
        return it != idx.features.end() && std::ranges::find(it->second, kind) != it->second.end();
    };
    auto take = [&](const std::wstring& language, Kind kind) {
        if (have(language, kind)) {
            return;
        }
        const auto it = std::ranges::find_if(available, [&](const core::LanguagePackFile& f) {
            return f.kind == kind && sameTag(f.language, language);
        });
        if (it != available.end()) {
            chosen.push_back(*it);
        }
    };
    for (std::size_t i = 0; i < chosen.size(); ++i) { // grows while it runs: a dependency's own ones too
        const auto f = chosen[i];
        if (f.language.empty()) {
            continue;
        }
        if (f.kind != Kind::LanguagePack) {
            take(f.language, Kind::LanguagePack);
        }
        for (const Kind dep : core::featureDependencies(f.kind)) {
            take(f.language, dep);
        }
        if (f.kind == Kind::LanguagePack) {
            for (const auto& script : core::requiredFontScripts(f.language)) {
                const bool installed = idx.fonts.contains(text::lower(script));
                const bool picked = std::ranges::any_of(chosen, [&](const core::LanguagePackFile& c) {
                    return c.kind == Kind::Fonts && _wcsicmp(c.component.c_str(), script.c_str()) == 0;
                });
                const auto it = std::ranges::find_if(available, [&](const core::LanguagePackFile& a) {
                    return a.kind == Kind::Fonts && _wcsicmp(a.component.c_str(), script.c_str()) == 0;
                });
                if (!installed && !picked && it != available.end()) {
                    chosen.push_back(*it);
                }
            }
        }
    }
    return chosen;
}

std::vector<core::LanguagePackFile> LanguageController::queuedPacks() const {
    std::vector<core::LanguagePackFile> out;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::AddPackage && op.value == L"language") {
            auto f = core::classifyLanguageFile(op.target);
            if (f.size == 0 && op.sizeDelta > 0) {
                f.size = static_cast<std::uint64_t>(op.sizeDelta / 2); // what was queued (the file may be offline now)
            }
            out.push_back(std::move(f));
        }
    }
    return out;
}

void LanguageController::unqueuePack(const std::filesystem::path& file) {
    m_state.unqueue(OpKind::AddPackage, file.wstring());
}

core::IntlSettings LanguageController::settings() const {
    if (const auto* op = m_state.changes().find(OpKind::SetIntl, kIntl)) {
        if (auto s = core::intlFromJson(utf8::fromWide(op->value))) {
            return *s;
        }
    }
    return {};
}

void LanguageController::setSettings(const core::IntlSettings& settings) {
    if (settings.empty()) {
        m_state.unqueue(OpKind::SetIntl, kIntl);
        return;
    }
    Operation op{OpKind::SetIntl, kIntl, utf8::toWide(core::intlToJson(settings))};
    op.risk = Risk::Low;
    m_state.queue(std::move(op));
}

std::vector<std::wstring> LanguageController::uiLanguages() const {
    std::vector<std::wstring> langs;
    if (const auto& intl = m_state.imageIntl(); intl && intl->status == AppState::ImageIntl::Status::Ready) {
        langs = intl->intl.languages;
    }
    for (const auto& f : queuedPacks()) {
        if (f.kind == core::LanguagePackFile::Kind::LanguagePack && !f.language.empty() &&
            std::ranges::none_of(langs, [&](const std::wstring& l) { return sameTag(l, f.language); })) {
            langs.push_back(f.language);
        }
    }
    return langs;
}

namespace {
std::atomic<bool> g_englishNames{false};
} // namespace

void LanguageController::setNameLanguage(Language language) {
    g_englishNames = language == Language::English;
}

std::wstring LanguageController::localeName(std::wstring_view tag) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH * 4] = {};
    const std::wstring t(tag);
    const LCTYPE type = g_englishNames ? LOCALE_SENGLISHDISPLAYNAME : LOCALE_SLOCALIZEDDISPLAYNAME;
    if (GetLocaleInfoEx(t.c_str(), type, name, static_cast<int>(std::size(name))) > 0) {
        return name;
    }
    return t;
}

const std::vector<IntlChoice>& LanguageController::locales() {
    // One list per name language (the app can switch language while it runs).
    static std::vector<IntlChoice> lists[2];
    auto& kLocales = lists[g_englishNames ? 1 : 0];
    if (!kLocales.empty()) {
        return kLocales;
    }
    kLocales = [] {
        std::vector<IntlChoice> list;
        EnumSystemLocalesEx(
            [](LPWSTR name, DWORD, LPARAM param) -> BOOL {
                auto* out = reinterpret_cast<std::vector<IntlChoice>*>(param);
                const std::wstring tag = name;
                // Specific locales only ("tr-TR"), not the neutral ones ("tr") or the invariant one.
                if (tag.find(L'-') != std::wstring::npos && core::isLanguageTag(tag)) {
                    out->push_back({tag, localeName(tag)});
                }
                return TRUE;
            },
            LOCALE_WINDOWS, reinterpret_cast<LPARAM>(&list), nullptr);
        std::ranges::sort(list, [](const IntlChoice& a, const IntlChoice& b) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.display.c_str(), -1, b.display.c_str(),
                                   -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });
        return list;
    }();
    return kLocales;
}

const std::vector<IntlChoice>& LanguageController::keyboards() {
    static const std::vector<IntlChoice> kKeyboards = [] {
        std::vector<IntlChoice> list;
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0, KEY_READ, &root) !=
            ERROR_SUCCESS) {
            return list;
        }
        wchar_t klid[64];
        for (DWORD i = 0;; ++i) {
            DWORD length = static_cast<DWORD>(std::size(klid));
            if (RegEnumKeyExW(root, i, klid, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                break;
            }
            const std::wstring id = klid;
            // Plain layouts only ("0000041f", "0001041f"); IMEs ("e0010411") and TIP-only entries are left out.
            if (id.size() != 8 || !(id[0] == L'0')) {
                continue;
            }
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, klid, 0, KEY_READ, &key) != ERROR_SUCCESS) {
                continue;
            }
            std::wstring text = regString(key, L"Layout Display Name");
            if (text.empty()) {
                text = regString(key, L"Layout Text");
            }
            RegCloseKey(key);
            std::wstring lang = id.substr(4);
            for (auto& c : lang) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            std::wstring value = lang + L":" + id;
            for (auto& c : value) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            list.push_back({value, text.empty() ? id : text});
        }
        RegCloseKey(root);
        std::ranges::sort(list, [](const IntlChoice& a, const IntlChoice& b) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.display.c_str(), -1, b.display.c_str(),
                                   -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });
        return list;
    }();
    return kKeyboards;
}

const std::vector<IntlChoice>& LanguageController::timeZones() {
    static const std::vector<IntlChoice> kZones = [] {
        struct Zone {
            IntlChoice choice;
            long bias;
        };
        std::vector<Zone> zones;
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones", 0, KEY_READ,
                          &root) != ERROR_SUCCESS) {
            return std::vector<IntlChoice>{};
        }
        wchar_t name[256];
        for (DWORD i = 0;; ++i) {
            DWORD length = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                break;
            }
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS) {
                continue;
            }
            const std::wstring display = regString(key, L"Display");
            long bias = 0;
            BYTE tzi[44] = {};
            DWORD size = sizeof(tzi);
            DWORD type = 0;
            if (RegQueryValueExW(key, L"TZI", nullptr, &type, tzi, &size) == ERROR_SUCCESS && size >= 4) {
                std::memcpy(&bias, tzi, sizeof(bias));
            }
            RegCloseKey(key);
            if (core::isTimeZoneId(name)) {
                zones.push_back({{name, display.empty() ? std::wstring(name) : display}, bias});
            }
        }
        RegCloseKey(root);
        std::ranges::stable_sort(zones, [](const Zone& a, const Zone& b) { return a.bias > b.bias; });
        std::vector<IntlChoice> list;
        for (auto& z : zones) {
            list.push_back(std::move(z.choice));
        }
        return list;
    }();
    return kZones;
}

bool LanguageRow::has(Kind k) const {
    return std::ranges::find(installed, k) != installed.end() || std::ranges::find(queued, k) != queued.end();
}

std::span<const std::wstring> LanguageController::imagePackages() const {
    const auto& intl = m_state.imageIntl();
    if (intl && intl->status == AppState::ImageIntl::Status::Ready && m_state.mounted() && intl->mountDir == m_state.mounted()->mountDir) {
        return intl->packages;
    }
    return {};
}

bool LanguageController::imageHasLanguage(std::wstring_view language) const {
    const auto& intl = m_state.imageIntl();
    return intl && intl->status == AppState::ImageIntl::Status::Ready &&
           std::ranges::any_of(intl->intl.languages, [&](const std::wstring& l) { return sameTag(l, language); });
}

const LanguageController::PackageIndex& LanguageController::index() const {
    using Kind = core::LanguagePackFile::Kind;
    const auto packages = imagePackages();
    if (m_index.data == packages.data() && m_index.size == packages.size()) {
        return m_index;
    }
    m_index = PackageIndex{packages.data(), packages.size()};
    for (const auto& identity : packages) {
        const auto f = core::classifyPackageIdentity(identity);
        if (f.kind == Kind::Other) {
            // A language-neutral package: "Name~31bf3856ad364e35~arch~~version".
            const auto token = identity.find(L"~31bf3856ad364e35~");
            if (token != std::wstring::npos) {
                const std::wstring rest = identity.substr(token + 18);
                const auto tilde = rest.find(L'~');
                if (tilde != std::wstring::npos && rest.size() > tilde + 1 && rest[tilde + 1] == L'~') {
                    m_index.neutral.insert(text::lower(identity.substr(0, token) + L"~" + rest.substr(0, tilde)));
                }
            }
            continue;
        }
        const std::wstring lang = text::lower(f.language);
        switch (f.kind) {
        case Kind::Fonts: m_index.fonts.insert(text::lower(f.component)); break;
        case Kind::Satellite:
            m_index.localized.insert(text::lower(f.component + L"~" + f.packageArch + L"~" + f.language));
            ++m_index.components[lang];
            break;
        case Kind::LanguagePack: break;
        default: {
            auto& kinds = m_index.features[lang];
            if (std::ranges::find(kinds, f.kind) == kinds.end()) {
                kinds.push_back(f.kind);
            }
        }
        }
    }
    return m_index;
}

std::vector<LanguageRow> LanguageController::rows() const {
    using Kind = core::LanguagePackFile::Kind;
    std::vector<LanguageRow> rows;
    auto row = [&](const std::wstring& tag) -> LanguageRow& {
        for (auto& r : rows) {
            if (sameTag(r.language, tag)) {
                return r;
            }
        }
        rows.push_back(LanguageRow{tag});
        return rows.back();
    };
    if (const auto& intl = m_state.imageIntl(); intl && intl->status == AppState::ImageIntl::Status::Ready) {
        const auto& idx = index();
        for (const auto& l : intl->intl.languages) {
            auto& r = row(l);
            r.inImage = true;
            r.ui = sameTag(l, intl->intl.current.uiLanguage);
            if (const auto it = idx.features.find(text::lower(l)); it != idx.features.end()) {
                r.installed = it->second;
            }
            if (const auto it = idx.components.find(text::lower(l)); it != idx.components.end()) {
                r.componentsInImage = it->second;
            }
        }
    }
    for (const auto& f : queuedPacks()) {
        if (f.language.empty()) {
            continue; // fonts: part of the language that needs them, not a row
        }
        auto& r = row(f.language);
        r.queuedBytes += f.size;
        if (f.kind == Kind::Satellite) {
            ++r.componentsQueued;
        } else if (std::ranges::find(r.queued, f.kind) == r.queued.end()) {
            r.queued.push_back(f.kind);
        }
    }
    for (auto& r : rows) {
        std::ranges::sort(r.installed);
        std::ranges::sort(r.queued);
    }
    return rows;
}

void LanguageController::unqueueLanguage(std::wstring_view language) {
    const std::wstring tag(language);
    // The fonts its script needs go with it unless another queued language needs them too.
    const auto scripts = core::requiredFontScripts(tag);
    std::vector<std::wstring> otherScripts;
    for (const auto& f : queuedPacks()) {
        if (!f.language.empty() && !sameTag(f.language, tag) && f.kind == core::LanguagePackFile::Kind::LanguagePack) {
            for (auto& s : core::requiredFontScripts(f.language)) {
                otherScripts.push_back(std::move(s));
            }
        }
    }
    m_state.unqueueIf([&](const Operation& op) {
        if (op.kind != OpKind::AddPackage || op.value != L"language") {
            return false;
        }
        const auto f = core::classifyLanguageName(op.target);
        if (f.kind == core::LanguagePackFile::Kind::Fonts) {
            const auto mine = [&](const std::wstring& s) { return _wcsicmp(s.c_str(), f.component.c_str()) == 0; };
            return std::ranges::any_of(scripts, mine) && std::ranges::none_of(otherScripts, mine);
        }
        return sameTag(f.language, tag);
    });
    // A display language nothing provides any more is no choice.
    auto settings = this->settings();
    if (sameTag(settings.uiLanguage, tag) && !imageHasLanguage(tag)) {
        settings.uiLanguage.clear();
        setSettings(settings);
    }
}

std::vector<core::UupLanguageFile> LanguageController::pick(const core::UupLanguage& language, const LanguageParts& parts) const {
    using Kind = core::LanguagePackFile::Kind;
    const bool inImage = imageHasLanguage(language.language);
    const auto& idx = index();
    const auto features = idx.features.find(text::lower(language.language));
    auto installed = [&](Kind k) {
        return features != idx.features.end() && std::ranges::find(features->second, k) != features->second.end();
    };
    auto wanted = [&](Kind k) {
        switch (k) {
        case Kind::LanguagePack: return !inImage;
        case Kind::Basic:
        case Kind::Fonts: return true;
        case Kind::Handwriting: return parts.handwriting;
        case Kind::Ocr: return parts.ocr;
        case Kind::TextToSpeech: return parts.textToSpeech || parts.speech; // Speech needs it
        case Kind::Speech: return parts.speech;
        case Kind::Satellite: return parts.components;
        default: return false;
        }
    };
    std::vector<core::UupLanguageFile> files;
    for (const auto& f : language.files) {
        const Kind k = f.file.kind;
        if (!wanted(k) || installed(k)) {
            continue;
        }
        if (k == Kind::Fonts && idx.fonts.contains(text::lower(f.file.component))) {
            continue; // the image has these fonts
        }
        if (k == Kind::Satellite) {
            const std::wstring component = text::lower(f.file.component + L"~" + f.file.packageArch);
            if (!idx.neutral.contains(component) || idx.localized.contains(component + L"~" + text::lower(f.file.language))) {
                continue; // a component the image does not have, or has in this language already
            }
        }
        files.push_back(f);
    }
    return files;
}

void LanguageController::queueFiles(const std::vector<std::filesystem::path>& files, const std::wstring& uiLanguage) {
    std::vector<core::LanguagePackFile> packs;
    for (const auto& path : files) {
        auto f = core::classifyLanguageFile(path);
        if (f.kind != core::LanguagePackFile::Kind::Other) {
            packs.push_back(std::move(f));
        }
    }
    queuePacks(packs);
    if (!uiLanguage.empty()) {
        auto settings = this->settings();
        settings.uiLanguage = uiLanguage;
        setSettings(settings);
    }
}

bool LanguageController::cumulativeUpdateAdvised() const {
    bool languages = false;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::AddPackage && op.value == L"lcu") {
            return false; // the planner puts it after the languages
        }
        languages = languages || (op.kind == OpKind::AddPackage && op.value == L"language");
    }
    if (!languages) {
        return false;
    }
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (mounted && source) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted->index) {
                return image.spBuild > 1;
            }
        }
    }
    return false;
}

int LanguageController::changedCount() const {
    // Languages added (not their files: one language is ~20) + the region settings.
    int n = 0;
    for (const auto& r : rows()) {
        n += !r.queued.empty() || r.componentsQueued > 0 ? 1 : 0;
    }
    return n + (m_state.changes().find(OpKind::SetIntl, kIntl) ? 1 : 0);
}

} // namespace wl::app
