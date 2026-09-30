#include "app/controllers/LanguageController.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/UpdatePackage.h"
#include "core/image/dism/Dism.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
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
    m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Loading, mountDir, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<core::ImageIntl>(
        [mountDir](const core::TaskContext&) -> Result<core::ImageIntl> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            return core::readIntl(**session);
        },
        [this, post, alive, mountDir](Result<core::ImageIntl> result) {
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
                    m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                m_state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Ready, mountDir, std::move(*result), {}});
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
    for (const auto& f : files) {
        if (f.architecture.empty() || f.architecture == arch) {
            out.push_back(f);
        }
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

std::wstring LanguageController::localeName(std::wstring_view tag) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH * 4] = {};
    const std::wstring t(tag);
    if (GetLocaleInfoEx(t.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, name, static_cast<int>(std::size(name))) > 0) {
        return name;
    }
    return t;
}

const std::vector<IntlChoice>& LanguageController::locales() {
    static const std::vector<IntlChoice> kLocales = [] {
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

int LanguageController::changedCount() const {
    int n = 0;
    for (const auto& op : m_state.changes().operations()) {
        n += (op.kind == OpKind::AddPackage && op.value == L"language") || op.kind == OpKind::SetIntl ? 1 : 0;
    }
    return n;
}

} // namespace wl::app
