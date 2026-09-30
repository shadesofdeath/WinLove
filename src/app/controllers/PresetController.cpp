#include "app/controllers/PresetController.h"

#include "app/controllers/ImageSettingsController.h"
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/postsetup/PostSetup.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <set>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

// Categories in nav order (the diff is sorted by it).
enum Category : int { kComponents, kFeatures, kUpdates, kDrivers, kRegistry, kServices, kTweaks, kUnattended, kPostSetup };

constexpr Str kCategoryNames[] = {Str::NavComponents, Str::NavFeatures, Str::NavUpdates,    Str::NavDrivers, Str::NavRegistry,
                                  Str::NavServices,   Str::NavTweaks,   Str::NavUnattended, Str::NavPostsetup};

std::wstring lowered(std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

std::wstring slotKey(OpKind kind, const std::wstring& target) {
    return std::to_wstring(static_cast<int>(kind)) + L'|' + lowered(target);
}

// The key a registry / service operation has when it is listed on its own.
std::wstring plainKey(const Operation& op) {
    return (op.kind == OpKind::SetServiceStart ? L"service|" : L"registry|") + lowered(op.target);
}

Str startLabel(core::StartType start) {
    switch (start) {
    case core::StartType::Boot: return Str::ServicesStartBoot;
    case core::StartType::System: return Str::ServicesStartSystem;
    case core::StartType::Auto: return Str::ServicesStartAuto;
    case core::StartType::AutoDelayed: return Str::ServicesStartAutoDelayed;
    case core::StartType::Disabled: return Str::ServicesStartDisabled;
    default: return Str::ServicesStartManual;
    }
}

Str stepTypeName(core::PostSetupStep::Type type) {
    switch (type) {
    case core::PostSetupStep::Type::Winget: return Str::PostsetupTypesWinget;
    case core::PostSetupStep::Type::Copy: return Str::PostsetupTypesCopy;
    default: return Str::PostsetupTypesCommand;
    }
}

} // namespace

PresetController::PresetController(AppState& state, const ImageSettingsCatalog& settings, const Localization& strings,
                                   Language language, std::filesystem::path folder)
    : m_state(state), m_settings(settings), m_strings(strings), m_language(language), m_folder(std::move(folder)) {}

std::wstring PresetController::categoryName(int category) const {
    return m_strings.get(kCategoryNames[std::clamp(category, 0, static_cast<int>(std::size(kCategoryNames)) - 1)]);
}

std::wstring PresetController::fileNameFor(std::wstring_view name) {
    std::wstring out;
    for (const wchar_t c : name) {
        if (c >= 32 && std::wstring_view(L"<>:\"/\\|?*").find(c) == std::wstring_view::npos) {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) {
        out.pop_back();
    }
    while (!out.empty() && out.front() == L' ') {
        out.erase(out.begin());
    }
    return out.empty() ? L"preset" : out;
}

void PresetController::reload() {
    if (m_adopted) {
        return;
    }
    m_presets.clear();
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(m_folder, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".wlpreset") != 0) {
            continue;
        }
        auto preset = readPreset(it->path());
        if (!preset) {
            log::warn("app", describe(preset.error()));
            continue;
        }
        m_presets.push_back(std::move(*preset));
    }
    std::ranges::sort(m_presets, [](const Preset& a, const Preset& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
}

void PresetController::adopt(std::vector<Preset> presets) {
    m_presets = std::move(presets);
    m_adopted = true;
}

Result<void> PresetController::saveCurrent(const std::wstring& name) {
    Preset preset = presetFromState(m_state, name);
    if (auto r = writePreset(m_folder / (fileNameFor(name) + L".wlpreset"), preset); !r) {
        return r;
    }
    reload();
    return {};
}

Result<void> PresetController::importFile(const std::filesystem::path& file) {
    // Read first: a file that is not a preset never reaches the library.
    auto preset = readPreset(file);
    if (!preset) {
        return std::unexpected(preset.error());
    }
    if (auto r = writePreset(m_folder / (fileNameFor(preset->name) + L".wlpreset"), *preset); !r) {
        return r;
    }
    reload();
    return {};
}

Result<void> PresetController::exportTo(std::size_t index, const std::filesystem::path& target) const {
    if (index >= m_presets.size()) {
        return fail(ErrorCode::InvalidArgument, L"no such preset");
    }
    return writePreset(target, m_presets[index]);
}

Result<void> PresetController::remove(std::size_t index) {
    if (index >= m_presets.size()) {
        return fail(ErrorCode::InvalidArgument, L"no such preset");
    }
    std::error_code ec;
    const auto file = m_presets[index].file;
    if (!file.empty() && !std::filesystem::remove(file, ec)) {
        return fail(ErrorCode::IoError, L"could not delete the preset", file.wstring());
    }
    reload();
    return {};
}

std::size_t PresetController::apply(const Preset& preset) {
    if (preset.unattend) {
        m_state.setUnattend(*preset.unattend);
    }
    if (!m_state.mounted() || preset.changes.empty()) {
        return 0;
    }
    m_state.queueMany(preset.changes.operations());
    return preset.changes.size();
}

Preset PresetController::current() const {
    return presetFromState(m_state, m_strings.get(Str::PresetsCurrentQueue));
}

std::vector<PresetController::Item> PresetController::items(const Preset& preset) const {
    std::vector<Item> result;
    auto s = [&](Str key) { return m_strings.get(key); };
    std::set<std::wstring> named; // queue slots already told as a setting

    // Known settings first: their registry / service operations are one item each.
    for (const auto& setting : m_settings.settings()) {
        const int option = ImageSettingsController::optionIn(preset.changes, setting);
        if (option == setting.defaultOption) {
            continue;
        }
        const auto& chosen = setting.options[static_cast<std::size_t>(option)];
        const std::wstring value = setting.control == ImageSetting::Control::Toggle
                                       ? s(option == 1 ? Str::PresetsOn : Str::PresetsOff)
                                       : chosen.label.get(m_language);
        const auto ops = ImageSettingsController::operationsFor(setting, option);
        result.push_back({kTweaks, L"setting|" + utf8::toWide(setting.id), setting.label.get(m_language), value,
                          ops.empty() ? std::wstring() : plainKey(ops.front())});
        for (const auto& op : ops) {
            named.insert(slotKey(op.kind, op.target));
        }
    }

    for (const auto& op : preset.changes.operations()) {
        if (named.contains(slotKey(op.kind, op.target))) {
            continue;
        }
        const std::wstring file = std::filesystem::path(op.target).filename().wstring();
        switch (op.kind) {
        case OpKind::RemovePackage:
        case OpKind::RemoveAppx: {
            // "Microsoft.GamingApp_2410.1001.4.0_neutral_~_8wekyb3d8bbwe" → the identity.
            const std::wstring identity = op.target.substr(0, op.target.find(L'_'));
            result.push_back({kComponents, L"component|" + lowered(identity), identity, s(Str::PresetsValueRemove), {}});
            break;
        }
        case OpKind::RemoveComponent:
        case OpKind::CleanupImage: {
            const std::wstring title = core::componentTitle(op.value);
            result.push_back({kComponents, L"system|" + lowered(op.target), title.empty() ? op.target : title,
                              s(op.kind == OpKind::CleanupImage ? Str::PresetsValueRun : Str::PresetsValueRemove), {}});
            break;
        }
        case OpKind::DisableFeature:
        case OpKind::EnableFeature:
        case OpKind::RemoveCapability:
            result.push_back({kFeatures, L"feature|" + lowered(op.target), op.target,
                              s(op.kind == OpKind::EnableFeature    ? Str::PresetsValueEnable
                                : op.kind == OpKind::DisableFeature ? Str::PresetsValueDisable
                                                                    : Str::PresetsValueRemove),
                              {}});
            break;
        case OpKind::AddPackage:
            result.push_back({kUpdates, L"update|" + lowered(file), file, s(Str::PresetsValueAdd), {}});
            break;
        case OpKind::AddDriver:
            result.push_back({kDrivers, L"driver|" + lowered(op.target), file, s(Str::PresetsValueAdd), {}});
            break;
        case OpKind::SetRegistryValue:
        case OpKind::SetRegistryFirstLogon:
            result.push_back({kRegistry, plainKey(op), op.target, op.value, {}});
            break;
        case OpKind::SetServiceStart: {
            const auto start = core::startTypeFromKey(op.value);
            result.push_back({kServices, plainKey(op), op.target, start ? s(startLabel(*start)) : op.value, {}});
            break;
        }
        case OpKind::SetPostSetup:
            if (const auto plan = core::postSetupFromJson(utf8::fromWide(op.value))) {
                for (const auto& step : plan->steps) {
                    result.push_back({kPostSetup, L"step|" + lowered(step.source),
                                      step.name.empty() ? step.source : step.name, s(stepTypeName(step.type)), {}});
                }
            }
            break;
        }
    }

    if (preset.unattend) {
        const core::UnattendOptions& o = preset.unattend->options;
        auto text = [&](Str label, const std::wstring& value) {
            if (!value.empty()) {
                result.push_back({kUnattended, L"unattend|" + std::to_wstring(static_cast<int>(label)), s(label), value, {}});
            }
        };
        auto flag = [&](Str label, bool on, Str value) { text(label, on ? s(value) : std::wstring()); };
        text(Str::UnattendedUiLanguage, o.uiLanguage);
        text(Str::UnattendedLocale, o.locale);
        text(Str::UnattendedKeyboard, o.keyboard);
        text(Str::UnattendedTimeZone, o.timeZone);
        text(Str::UnattendedLocalAccount, o.accountName);
        flag(Str::UnattendedPassword, !o.password.empty(), Str::PresetsValueSet); // never the password itself
        flag(Str::UnattendedAutoLogon, o.autoLogon, Str::PresetsOn);
        text(Str::UnattendedComputerName, o.computerName);
        text(Str::UnattendedDiskLayout, o.disk == core::UnattendDisk::WipeGpt   ? s(Str::UnattendedDiskGpt)
                                        : o.disk == core::UnattendDisk::WipeMbr ? s(Str::UnattendedDiskMbr)
                                                                                : std::wstring());
        flag(Str::UnattendedAcceptEula, o.acceptEula, Str::PresetsOn);
        flag(Str::UnattendedSkipPrivacy, o.skipPrivacy, Str::PresetsOn);
        flag(Str::UnattendedMsAccount, o.bypassNro, Str::PresetsOn);
        flag(Str::UnattendedSkipOnline, o.skipOnlineAccount, Str::PresetsOn);
        flag(Str::UnattendedProductKey, !o.productKey.empty(), Str::PresetsValueSet);
        text(Str::UnattendedEdition, o.imageIndex > 0 ? std::to_wstring(o.imageIndex) : std::wstring());
        flag(Str::UnattendedTpm, o.bypassTpm, Str::PresetsOn);
        flag(Str::UnattendedSecureBoot, o.bypassSecureBoot, Str::PresetsOn);
        flag(Str::UnattendedRam, o.bypassRam, Str::PresetsOn);
        flag(Str::UnattendedIncludeInIso, preset.unattend->includeInIso, Str::PresetsOn);
    }
    return result;
}

std::vector<PresetController::DiffRow> PresetController::diff(const Preset& a, const Preset& b, bool includeSame) const {
    const auto left = items(a);
    const auto right = items(b);
    // The same thing on the other side: same key, or a setting and the plain operation under it.
    auto same = [](const Item& x, const Item& y) {
        return x.key == y.key || (!x.alias.empty() && (x.alias == y.key || x.alias == y.alias)) ||
               (!y.alias.empty() && y.alias == x.key);
    };
    auto counterpart = [&](const std::vector<Item>& side, const Item& item) {
        return std::ranges::find_if(side, [&](const Item& candidate) { return same(item, candidate); });
    };
    std::vector<DiffRow> rows;
    for (const auto& item : left) {
        const auto other = counterpart(right, item);
        if (other == right.end()) {
            rows.push_back({DiffRow::Mark::Removed, item.category, item.label, item.value, {}});
        } else if (other->value != item.value) {
            rows.push_back({DiffRow::Mark::Changed, item.category, item.label, item.value, other->value});
        } else if (includeSame) {
            rows.push_back({DiffRow::Mark::Same, item.category, item.label, item.value, other->value});
        }
    }
    for (const auto& item : right) {
        if (counterpart(left, item) == left.end()) {
            rows.push_back({DiffRow::Mark::Added, item.category, item.label, {}, item.value});
        }
    }
    std::ranges::stable_sort(rows, {}, &DiffRow::category);
    return rows;
}

PresetController::DiffSummary PresetController::summarize(const std::vector<DiffRow>& rows) {
    DiffSummary summary;
    for (const auto& row : rows) {
        summary.added += row.mark == DiffRow::Mark::Added ? 1 : 0;
        summary.removed += row.mark == DiffRow::Mark::Removed ? 1 : 0;
        summary.changed += row.mark == DiffRow::Mark::Changed ? 1 : 0;
    }
    return summary;
}

} // namespace wl::app
