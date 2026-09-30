#include "app/controllers/ImageSettingsController.h"

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
            write.data.resize((value.size() + 1) * sizeof(wchar_t));
            std::memcpy(write.data.data(), value.data(), value.size() * sizeof(wchar_t));
            std::memset(write.data.data() + value.size() * sizeof(wchar_t), 0, sizeof(wchar_t));
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
}

void ImageSettingsController::select(const ImageSetting& setting, int option) {
    if (option == current(setting)) {
        return;
    }
    std::vector<std::pair<OpKind, std::wstring>> slots;
    collectQueued(setting, slots);
    m_state.unqueueMany(slots);
    m_state.queueMany(operationsFor(setting, option));
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

int ImageSettingsController::changedCount() const {
    return static_cast<int>(std::ranges::count_if(
        m_catalog.settings(), [&](const ImageSetting& s) { return current(s) != s.defaultOption; }));
}

} // namespace wl::app
