#include "app/state/Preset.h"

#include "base/Utf8.h"

#include <json.hpp>

#include <fstream>
#include <iterator>

namespace wl::app {

namespace {
using Json = nlohmann::json;
}

std::string presetToJson(const Preset& preset) {
    // The ChangeSet writes its own part; name and answer file are added around it.
    Json doc = Json::parse(preset.changes.toJson());
    doc["name"] = utf8::fromWide(preset.name);
    if (preset.unattend) {
        doc["unattend"] = Json{{"includeInIso", preset.unattend->includeInIso},
                               {"xml", utf8::fromWide(core::buildUnattendXml(preset.unattend->options))}};
    }
    return doc.dump(2);
}

Result<Preset> presetFromJson(std::string_view json, std::wstring fallbackName) {
    auto changes = core::ops::ChangeSet::fromJson(json);
    if (!changes) {
        return std::unexpected(changes.error());
    }
    Preset preset;
    preset.changes = std::move(*changes);
    preset.name = std::move(fallbackName);
    // fromJson accepted the document: it parses. The extra keys are user input all the same.
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    try {
        if (const auto name = doc.value("name", std::string{}); !name.empty()) {
            preset.name = utf8::toWide(name);
        }
        if (const auto it = doc.find("unattend"); it != doc.end() && it->is_object()) {
            auto options = core::parseUnattendXml(it->value("xml", std::string{}));
            if (!options) {
                return std::unexpected(options.error());
            }
            preset.unattend = AppState::Unattend{std::move(*options), it->value("includeInIso", false)};
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed preset", utf8::toWide(e.what()));
    }
    return preset;
}

Result<Preset> readPreset(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, L"could not open the preset", file.wstring());
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto preset = presetFromJson(bytes, file.stem().wstring());
    if (!preset) {
        auto error = preset.error();
        error.context = file.filename().wstring() + (error.context.empty() ? L"" : L" · " + error.context);
        return std::unexpected(std::move(error));
    }
    preset->file = file;
    return preset;
}

Result<void> writePreset(const std::filesystem::path& file, const Preset& preset) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::string bytes = presetToJson(preset);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return out ? Result<void>{} : fail(ErrorCode::IoError, L"could not write the preset", file.wstring());
}

Preset presetFromState(const AppState& state, std::wstring name) {
    Preset preset;
    preset.name = std::move(name);
    preset.changes = state.changes();
    const auto& unattend = state.unattend();
    if (unattend.includeInIso || !(unattend.options == core::UnattendOptions{})) {
        preset.unattend = unattend;
    }
    return preset;
}

} // namespace wl::app
