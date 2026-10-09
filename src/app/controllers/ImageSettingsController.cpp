#include "app/controllers/ImageSettingsController.h"

#include "core/image/RegistryRead.h"

#include "app/controllers/ImageValuesController.h"
#include "core/image/ImageFiles.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

ImageSettingsController::ImageSettingsController(AppState& state, ImageSettingsCatalog catalog)
    : m_state(state), m_catalog(std::move(catalog)) {}

namespace {

// A JPEG of this PC that fits into the image: what a "file" setting accepts.
bool jpegFile(const std::filesystem::path& file) {
    std::wstring extension = file.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (extension != L".jpg" && extension != L".jpeg") {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return false;
    }
    const auto size = std::filesystem::file_size(file, ec);
    return !ec && size > 0 && size <= core::kImageCopyLimit;
}

} // namespace

std::vector<Operation> ImageSettingsController::operationsFor(const ImageSetting& setting, int option,
                                                              std::wstring_view value) {
    std::vector<Operation> ops;
    if (option < 0 || option >= static_cast<int>(setting.options.size())) {
        return ops;
    }
    const auto& chosen = setting.options[static_cast<std::size_t>(option)];
    const OpKind kind = setting.firstLogon ? OpKind::SetRegistryFirstLogon : OpKind::SetRegistryValue;
    const bool typed = setting.control == ImageSetting::Control::Text;
    for (const auto& listed : chosen.writes) {
        core::RegistryWrite write = listed;
        if (typed) { // the catalog names the value, the user gives the string
            std::wstring text(value);
            std::size_t terminators = 1;
            if (setting.format == ImageSetting::Format::Pagefile) { // REG_MULTI_SZ: one entry, double NUL
                text = pagefileEntry(value).value_or(std::wstring());
                terminators = 2;
            }
            write.data.assign((text.size() + terminators) * sizeof(wchar_t), 0);
            std::memcpy(write.data.data(), text.data(), text.size() * sizeof(wchar_t));
        }
        Operation op{kind, core::registryTarget(write), core::formatRegValue(write)};
        op.risk = setting.risk;
        ops.push_back(std::move(op));
    }
    for (const auto& [name, start] : chosen.services) {
        Operation op{OpKind::SetServiceStart, name, core::startTypeKey(start)};
        op.risk = setting.risk;
        ops.push_back(std::move(op));
    }
    for (const auto& [path, content] : chosen.files) {
        Operation op{OpKind::WriteFile, path, content};
        op.risk = setting.risk;
        ops.push_back(std::move(op));
    }
    if (!chosen.copyTo.empty()) {
        Operation op{OpKind::CopyFile, chosen.copyTo, std::wstring(value)};
        op.risk = setting.risk;
        std::error_code ec;
        if (const auto size = std::filesystem::file_size(std::filesystem::path(std::wstring(value)), ec); !ec) {
            op.sizeDelta = static_cast<std::int64_t>(size);
        }
        ops.push_back(std::move(op));
    }
    return ops;
}

std::wstring ImageSettingsController::valueIn(const core::ops::ChangeSet& changes, const ImageSetting& setting) {
    if (!takesValue(setting) || setting.options.size() < 2) {
        return {};
    }
    const auto& chosen = setting.options[1];
    if (setting.control == ImageSetting::Control::File) {
        const auto* op = changes.find(OpKind::CopyFile, chosen.copyTo);
        return op ? op->value : std::wstring();
    }
    if (chosen.writes.empty()) {
        return {};
    }
    const OpKind kind = setting.firstLogon ? OpKind::SetRegistryFirstLogon : OpKind::SetRegistryValue;
    const auto* op = changes.find(kind, core::registryTarget(chosen.writes.front()));
    if (!op) {
        return {};
    }
    const auto write = core::registryWriteFrom(op->target, op->value);
    if (!write || write->kind != core::RegistryWrite::Kind::Set || write->type != chosen.writes.front().type ||
        write->data.size() % sizeof(wchar_t) != 0) {
        return {}; // the Registry page put something else in the slot
    }
    std::wstring text(write->data.size() / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), write->data.data(), write->data.size());
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    if (setting.format == ImageSetting::Format::Pagefile) {
        return pagefileTyped(text.substr(0, text.find(L'\0')));
    }
    return text;
}

bool ImageSettingsController::setValue(const ImageSetting& setting, std::wstring value) {
    if (!takesValue(setting)) {
        return false;
    }
    bool accepted = true;
    if (setting.control == ImageSetting::Control::File) {
        if (!value.empty() && !jpegFile(value)) {
            accepted = false;
            value.clear();
        }
    } else {
        std::erase_if(value, [](wchar_t c) { return c < 0x20 || c == 0x7f; });
        if (value.size() > kTextLimit) {
            value.resize(kTextLimit);
        }
        // A page file that is not "D:" or "D: min max" yet: nothing queued, the row says so.
        if (setting.format == ImageSetting::Format::Pagefile && !value.empty()) {
            if (const auto entry = pagefileEntry(value)) {
                value = pagefileTyped(*entry);
            } else {
                accepted = false;
                value.clear();
            }
        }
    }
    if (value == this->value(setting)) {
        return accepted;
    }
    std::vector<std::pair<OpKind, std::wstring>> slots;
    collectQueued(setting, slots);
    m_state.unqueueMany(slots);
    if (!value.empty()) {
        m_state.queueMany(operationsFor(setting, 1, value));
    }
    return accepted;
}

namespace {
bool queued(const core::ops::ChangeSet& changes, const Operation& op) {
    const auto* found = changes.find(op.kind, op.target);
    return found && found->value == op.value;
}
} // namespace

int ImageSettingsController::optionIn(const core::ops::ChangeSet& changes, const ImageSetting& setting) {
    if (takesValue(setting)) {
        return valueIn(changes, setting).empty() ? setting.defaultOption : 1;
    }
    // Options of one setting may share slots with different values, never a whole set; when two
    // match anyway (a preset edited by hand) the one that says more wins.
    int best = setting.defaultOption;
    std::size_t bestSize = 0;
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        const auto ops = operationsFor(setting, i);
        if (ops.size() > bestSize && std::ranges::all_of(ops, [&](const Operation& op) { return queued(changes, op); })) {
            best = i;
            bestSize = ops.size();
        }
    }
    return best;
}

void ImageSettingsController::collectQueued(const ImageSetting& setting,
                                            std::vector<std::pair<OpKind, std::wstring>>& slots) const {
    const std::wstring typed = value(setting); // text / file settings: the operations carry it
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        for (auto& op : operationsFor(setting, i, typed)) {
            if (queued(m_state.changes(), op)) {
                slots.emplace_back(op.kind, std::move(op.target));
            }
        }
    }
    for (auto& op : revertOperations(setting)) {
        if (queued(m_state.changes(), op)) {
            slots.emplace_back(op.kind, std::move(op.target));
        }
    }
}

namespace {

std::optional<core::RegistryWrite> registryWriteOf(const Operation& op) {
    if (op.kind != OpKind::SetRegistryValue && op.kind != OpKind::SetRegistryFirstLogon) {
        return std::nullopt;
    }
    auto write = core::registryWriteFrom(op.target, op.value);
    return write ? std::optional<core::RegistryWrite>(std::move(*write)) : std::nullopt;
}

std::optional<Operation> revertOf(const Operation& op) {
    const auto write = registryWriteOf(op);
    if (!write) {
        return std::nullopt;
    }
    const auto back = revertWrite(*write);
    if (!back) {
        return std::nullopt;
    }
    Operation revert{op.kind, core::registryTarget(*back), core::formatRegValue(*back)};
    revert.risk = op.risk;
    return revert;
}

} // namespace

int ImageSettingsController::holdingOption(const ImageSetting& setting, bool withQueue) const {
    const auto& changes = m_state.changes();
    // An operation holds when it is queued with its value, or — its slot and its way back not
    // queued — the image already has it. Options of one setting may share slots with different
    // values; when two hold anyway the one that says more wins.
    auto holds = [&](const Operation& op, bool& asserts) {
        if (withQueue) {
            if (const auto* q = changes.find(op.kind, op.target)) {
                asserts = true;
                return q->value == op.value;
            }
            if (const auto revert = revertOf(op); revert && queued(changes, *revert)) {
                return false;
            }
        }
        if (!m_state.imageHas(op)) {
            return false;
        }
        const auto write = registryWriteOf(op);
        asserts = asserts || !write || assertsSomething(*write);
        return true;
    };
    int best = setting.defaultOption;
    std::size_t bestSize = 0;
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        if (i == setting.defaultOption) {
            continue;
        }
        const auto ops = operationsFor(setting, i);
        bool asserts = false;
        if (ops.size() > bestSize && std::ranges::all_of(ops, [&](const Operation& op) { return holds(op, asserts); }) &&
            asserts) {
            best = i;
            bestSize = ops.size();
        }
    }
    return best;
}

int ImageSettingsController::current(const ImageSetting& setting) const {
    if (takesValue(setting)) {
        return optionIn(m_state.changes(), setting);
    }
    return holdingOption(setting, /*withQueue=*/true);
}

int ImageSettingsController::imageOption(const ImageSetting& setting) const {
    if (takesValue(setting)) {
        return setting.defaultOption;
    }
    return holdingOption(setting, /*withQueue=*/false);
}

std::vector<Operation> ImageSettingsController::revertOperations(const ImageSetting& setting) const {
    const int image = imageOption(setting);
    std::vector<Operation> ops;
    if (image == setting.defaultOption) {
        return ops;
    }
    for (const auto& op : operationsFor(setting, image)) {
        auto revert = revertOf(op);
        if (!revert) {
            return {};
        }
        ops.push_back(std::move(*revert));
    }
    return ops;
}

std::wstring ImageSettingsController::imageValue(const ImageSetting& setting) const {
    const auto& values = m_state.imageValues();
    if (setting.control != ImageSetting::Control::Text || setting.options.size() < 2 ||
        setting.options[1].writes.empty() || !values || !m_state.mounted() ||
        values->mountDir != m_state.mounted()->mountDir) {
        return {};
    }
    const auto it = values->texts.find(core::registryTarget(setting.options[1].writes.front()));
    if (it == values->texts.end()) {
        return {};
    }
    if (setting.format == ImageSetting::Format::Pagefile) { // "?:\pagefile.sys" (automatic) shows as it is
        const std::wstring typed = pagefileTyped(it->second);
        return typed.empty() ? it->second : typed;
    }
    return it->second;
}

void ImageSettingsController::select(const ImageSetting& setting, int option) {
    if (option == current(setting)) {
        return;
    }
    std::vector<std::pair<OpKind, std::wstring>> slots;
    collectQueued(setting, slots);
    const int image = imageOption(setting);
    std::vector<Operation> ops;
    if (option == image) {
        // The image has it already: taking the queued edits back is all there is to do.
    } else if (option == setting.defaultOption) {
        ops = revertOperations(setting); // empty: no way back — the control returns to the image
    } else {
        ops = operationsFor(setting, option);
        // What the image option wrote outside the new option's slots goes back to the default.
        // (A value's way back has the value's own slot: "<key>::<name>".)
        for (auto& revert : revertOperations(setting)) {
            if (std::ranges::none_of(ops, [&](const Operation& op) {
                    return op.kind == revert.kind && op.target == revert.target;
                })) {
                ops.push_back(std::move(revert));
            }
        }
    }
    m_state.unqueueMany(slots);
    m_state.queueMany(std::move(ops));
}

int ImageSettingsController::applyRecommended() {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    std::vector<Operation> ops;
    int changed = 0;
    for (const auto& setting : m_catalog.settings()) {
        if (setting.recommended < 0 || setting.recommended == current(setting)) {
            continue;
        }
        ++changed;
        collectQueued(setting, slots);
        auto add = operationsFor(setting, setting.recommended);
        ops.insert(ops.end(), std::make_move_iterator(add.begin()), std::make_move_iterator(add.end()));
    }
    m_state.unqueueMany(slots);
    m_state.queueMany(std::move(ops));
    return changed;
}

int ImageSettingsController::thisPcOption(const ImageSetting& setting) const {
    if (takesValue(setting)) {
        return setting.defaultOption;
    }
    int best = setting.defaultOption;
    std::size_t bestSize = 0;
    for (int i = 0; i < static_cast<int>(setting.options.size()); ++i) {
        if (i == setting.defaultOption) {
            continue;
        }
        const auto ops = operationsFor(setting, i);
        bool asserts = false;
        bool all = !ops.empty();
        for (const auto& op : ops) {
            const auto write = registryWriteOf(op);
            if (!write || !core::liveRegistryHolds(*write)) {
                all = false; // a service or a file: this PC is not read for those
                break;
            }
            asserts = asserts || assertsSomething(*write);
        }
        if (all && asserts && ops.size() > bestSize) {
            best = i;
            bestSize = ops.size();
        }
    }
    return best;
}

int ImageSettingsController::takeFromThisPc() {
    std::vector<std::pair<OpKind, std::wstring>> slots;
    std::vector<Operation> ops;
    int changed = 0;
    for (const auto& setting : m_catalog.settings()) {
        const int here = thisPcOption(setting);
        if (here == setting.defaultOption || here == current(setting)) {
            continue;
        }
        ++changed;
        collectQueued(setting, slots);
        auto add = operationsFor(setting, here);
        ops.insert(ops.end(), std::make_move_iterator(add.begin()), std::make_move_iterator(add.end()));
    }
    m_state.unqueueMany(slots);
    m_state.queueMany(std::move(ops));
    return changed;
}

int ImageSettingsController::changedCount() const {
    return static_cast<int>(std::ranges::count_if(
        m_catalog.settings(), [&](const ImageSetting& s) { return current(s) != imageOption(s); }));
}

} // namespace wl::app
