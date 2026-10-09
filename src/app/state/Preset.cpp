#include "app/state/Preset.h"

#include "base/File.h"
#include "base/Utf8.h"

#include <json.hpp>

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
                               {"xml", utf8::fromWide(core::unattendStateXml(preset.unattend->options))},
                               {"welcome", preset.unattend->options.welcome}};
    }
    if (!preset.bootDrivers.empty()) {
        Json drivers = Json::array();
        for (const auto& inf : preset.bootDrivers) {
            drivers.push_back(utf8::fromWide(inf.wstring()));
        }
        doc["bootDrivers"] = std::move(drivers);
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
            if (const auto welcome = it->find("welcome"); welcome != it->end() && welcome->is_boolean()) {
                options->welcome = welcome->get<bool>(); // older presets: inside the text (B4)
            }
            preset.unattend = AppState::Unattend{std::move(*options), it->value("includeInIso", false)};
        }
        if (const auto it = doc.find("bootDrivers"); it != doc.end() && it->is_array()) {
            for (const auto& inf : *it) {
                if (inf.is_string() && !inf.get<std::string>().empty()) {
                    preset.bootDrivers.emplace_back(utf8::toWide(inf.get<std::string>()));
                }
            }
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed preset", utf8::toWide(e.what()));
    }
    return preset;
}

Result<Preset> readPreset(const std::filesystem::path& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return fail(ErrorCode::NotFound, L"could not open the preset", file.wstring());
    }
    auto preset = presetFromJson(*bytes, file.stem().wstring());
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
    if (auto written = writeFileAtomic(file, presetToJson(preset)); !written) {
        return fail(ErrorCode::IoError, L"could not write the preset", file.wstring());
    }
    return {};
}

Preset presetFromState(const AppState& state, std::wstring name) {
    Preset preset;
    preset.name = std::move(name);
    preset.changes = state.changes();
    const auto& unattend = state.unattend();
    if (unattend.includeInIso || !(unattend.options == core::UnattendOptions{})) {
        preset.unattend = unattend;
    }
    preset.bootDrivers = state.bootDrivers();
    return preset;
}

} // namespace wl::app
