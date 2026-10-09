#include "core/programs/OfflinePrograms.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/programs/Yaml.h"
#include "core/system/Process.h"

#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace wl::core {

namespace {

using Json = nlohmann::json;

std::wstring trimmed(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r')) {
        s.remove_suffix(1);
    }
    return std::wstring(s);
}

std::wstring stringField(const Json& node, const char* key) {
    return (node.is_object() && node.contains(key) && node[key].is_string())
               ? utf8::toWide(node[key].get<std::string>())
               : std::wstring{};
}

} // namespace

ManifestInstall parseWingetManifest(std::string_view yaml) {
    ManifestInstall result;
    const auto doc = parseYaml(yaml);
    if (!doc || !doc->is_object()) {
        return result;
    }
    auto readFrom = [&](const Json& node) {
        if (result.type.empty()) {
            result.type = stringField(node, "InstallerType");
        }
        if (result.silent.empty() && node.is_object() && node.contains("InstallerSwitches")) {
            result.silent = stringField(node["InstallerSwitches"], "Silent");
        }
    };
    readFrom(*doc); // a singleton manifest carries them at the top level
    if (doc->contains("Installers") && (*doc)["Installers"].is_array()) {
        for (const auto& installer : (*doc)["Installers"]) {
            readFrom(installer);
            if (!result.type.empty()) {
                break; // the first installer's type/switch (one arch per downloaded manifest)
            }
        }
    }
    return result;
}

std::wstring offlineInstallCommand(std::wstring_view type, std::wstring_view silent) {
    const std::wstring t = text::lower(std::wstring(type));
    const std::wstring sw = trimmed(silent);
    if (t == L"msi" || t == L"wix") {
        const std::wstring args = sw.empty() ? std::wstring(L"/quiet /norestart") : sw;
        return L"msiexec /i \"{path}\" " + args;
    }
    if (t == L"msix" || t == L"appx" || t == L"msixbundle" || t == L"appxbundle") {
        return L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Add-AppxPackage -Path '{path}'\"";
    }
    // exe / inno / nullsoft / burn / portable / zip and the rest: run the file with its silent switch.
    // Sensible defaults by family when the manifest gave none.
    std::wstring args = sw;
    if (args.empty()) {
        if (t == L"inno") {
            args = L"/VERYSILENT /NORESTART";
        } else if (t == L"nullsoft") {
            args = L"/S";
        } else if (t == L"burn") {
            args = L"/quiet /norestart";
        }
    }
    return args.empty() ? L"\"{path}\"" : L"\"{path}\" " + args;
}

Result<OfflineInstaller> downloadProgramOffline(const std::wstring& id, const std::wstring& name,
                                                const std::filesystem::path& folder, const TaskContext& task) {
    if (auto go = task.cancel.check(L"program download"); !go) {
        return std::unexpected(go.error());
    }
    std::error_code ec;
    const std::filesystem::path dest = folder / id; // the id has no path characters
    std::filesystem::remove_all(dest, ec);          // a fresh download; an older one may be a different version
    std::filesystem::create_directories(dest, ec);

    // --skip-dependencies: only the program's own installer is embedded and run; a runtime it needs
    // (VC++ redist and the like) is a winget package of its own the operator can pick, offline too.
    const std::wstring commandLine = L"winget.exe download --id \"" + id +
                                     L"\" -e --download-directory \"" + dest.wstring() +
                                     L"\" --skip-dependencies --accept-package-agreements --accept-source-agreements"
                                     L" --disable-interactivity";
    std::string output;
    task.report(0.0, L"winget download " + id);
    const auto run = runProcess(commandLine, [&](std::string_view chunk) { output += chunk; });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (*run != 0) {
        return fail(ErrorCode::IoError, L"winget download failed for " + id,
                    utf8::toWide(output), static_cast<std::int32_t>(*run));
    }

    // Find the manifest (.yaml) and the installer (the file next to it with the same stem).
    std::filesystem::path manifest;
    std::vector<std::filesystem::path> others;
    for (const auto& entry : std::filesystem::directory_iterator(dest, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (text::iequals(entry.path().extension().wstring(), L".yaml")) {
            manifest = entry.path();
        } else {
            others.push_back(entry.path());
        }
    }
    if (others.empty()) {
        return fail(ErrorCode::NotFound, L"winget download left no installer for " + id, utf8::toWide(output));
    }
    // The main installer shares the manifest's stem; otherwise the largest file is it.
    std::filesystem::path installer = others.front();
    if (!manifest.empty()) {
        for (const auto& p : others) {
            if (text::iequals(p.stem().wstring(), manifest.stem().wstring())) {
                installer = p;
                break;
            }
        }
    }
    if (installer == others.front() && others.size() > 1) {
        installer = *std::ranges::max_element(others, [&](const auto& a, const auto& b) {
            return std::filesystem::file_size(a, ec) < std::filesystem::file_size(b, ec);
        });
    }

    ManifestInstall how;
    if (!manifest.empty()) {
        std::ifstream in(manifest, std::ios::binary);
        const std::string yaml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        how = parseWingetManifest(yaml);
    }
    if (how.type.empty()) {
        how.type = text::lower(installer.extension().wstring().substr(installer.extension().wstring().empty() ? 0 : 1));
    }

    OfflineInstaller result;
    result.id = id;
    result.name = name.empty() ? id : name;
    result.file = installer.filename().wstring();
    result.command = offlineInstallCommand(how.type, how.silent);
    result.bytes = std::filesystem::file_size(installer, ec);
    task.report(1.0, L"winget download " + id);
    log::info("offline", std::format(L"downloaded {} ({}) -> {} [{}]", id, name, result.file, how.type));
    return result;
}

std::string offlineInstallersToJson(const std::vector<OfflineInstaller>& installers) {
    Json array = Json::array();
    for (const auto& i : installers) {
        array.push_back(Json{{"id", utf8::fromWide(i.id)},
                             {"name", utf8::fromWide(i.name)},
                             {"file", utf8::fromWide(i.file)},
                             {"command", utf8::fromWide(i.command)},
                             {"bytes", i.bytes}});
    }
    return array.dump();
}

Result<std::vector<OfflineInstaller>> offlineInstallersFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_array()) {
        return fail(ErrorCode::ParseError, L"not an offline-installers list");
    }
    std::vector<OfflineInstaller> installers;
    try {
        for (const auto& item : doc) {
            OfflineInstaller i;
            i.id = utf8::toWide(item.value("id", std::string{}));
            i.name = utf8::toWide(item.value("name", std::string{}));
            i.file = utf8::toWide(item.value("file", std::string{}));
            i.command = utf8::toWide(item.value("command", std::string{}));
            i.bytes = item.value("bytes", std::uint64_t{0});
            installers.push_back(std::move(i));
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed offline-installers list", utf8::toWide(e.what()));
    }
    return installers;
}

} // namespace wl::core
