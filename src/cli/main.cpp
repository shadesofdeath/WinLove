// wlcli — drives the image engine without UI (docs/ENGINE.md §4).
// Every engine capability gets a command here before any page uses it. `--json` output is stable
// and parsed by integration tests and tools.
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/system/Privileges.h"

#include <json.hpp>

#include <windows.h>

#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace wl;
using nlohmann::json;

core::CancelToken g_cancel;

void print(std::wstring_view text) {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        // Redirected (pipe/file): emit UTF-8 so tests and tools can parse it.
        const std::string utf8 = utf8::fromWide(text);
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
        std::fflush(stdout);
    }
}

void printJson(const json& value) {
    const std::string text = value.dump(2) + "\n";
    print(utf8::toWide(text));
}

int reportError(const Error& error) {
    print(L"error: " + describe(error) + L"\n");
    return error.code == ErrorCode::Cancelled ? 130 : 2;
}

BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_cancel.cancel(); // long operations stop at the next chunk and clean up
        return TRUE;
    }
    return FALSE;
}

std::string narrow(std::wstring_view text) {
    return utf8::fromWide(text);
}

json imageJson(const core::ImageInfo& image) {
    json languages = json::array();
    for (const auto& l : image.languages) {
        languages.push_back(narrow(l));
    }
    return {{"index", image.index},
            {"name", narrow(image.name)},
            {"displayName", narrow(image.displayName)},
            {"description", narrow(image.description)},
            {"editionId", narrow(image.editionId)},
            {"installationType", narrow(image.installationType)},
            {"architecture", narrow(core::architectureName(image.architecture))},
            {"version", narrow(image.versionString())},
            {"build", image.build},
            {"languages", languages},
            {"defaultLanguage", narrow(image.defaultLanguage)},
            {"totalBytes", image.totalBytes},
            {"fileCount", image.fileCount},
            {"directoryCount", image.directoryCount}};
}

json wimJson(const core::WimFile& wim) {
    json images = json::array();
    for (const auto& image : wim.images) {
        images.push_back(imageJson(image));
    }
    return {{"imageCount", wim.header.imageCount},
            {"compression", narrow(core::compressionName(wim.header.compression))},
            {"solid", wim.header.solid},
            {"bootIndex", wim.header.bootIndex},
            {"parts", wim.header.totalParts},
            {"images", images}};
}

std::wstring gib(std::uint64_t bytes) {
    return std::format(L"{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

int cmdInfo(const std::wstring& path, bool asJson) {
    auto source = core::openSource(path);
    if (!source) {
        return reportError(source.error());
    }
    if (asJson) {
        json out{{"path", narrow(source->path.wstring())},
                 {"format", narrow(core::formatName(source->format))},
                 {"volumeLabel", narrow(source->volumeLabel)},
                 {"installImage", narrow(source->installImage)},
                 {"installImageSize", source->installImageSize},
                 {"install", wimJson(source->install)}};
        if (source->boot) {
            out["boot"] = wimJson(*source->boot);
        }
        printJson(out);
        return 0;
    }
    const auto& h = source->install.header;
    print(std::format(L"{}  [{}{}{}]\n", source->path.wstring(), core::formatName(source->format),
                      source->volumeLabel.empty() ? L"" : L", label ", source->volumeLabel));
    print(std::format(L"  {}  {}  compression {}{}  {} image(s)\n\n", source->installImage, gib(source->installImageSize),
                      core::compressionName(h.compression), h.solid ? L" (solid)" : L"", h.imageCount));
    for (const auto& image : source->install.images) {
        print(std::format(L"  {:>2}  {:<42} {:<6} {:<16} {:<6} {}\n", image.index, image.name,
                          core::architectureName(image.architecture), image.versionString(), image.defaultLanguage,
                          gib(image.totalBytes)));
    }
    return 0;
}

int cmdList(const std::wstring& iso, const std::wstring& directory, bool asJson) {
    auto image = core::UdfImage::open(iso);
    if (!image) {
        return reportError(image.error());
    }
    auto entries = image->list(directory);
    if (!entries) {
        return reportError(entries.error());
    }
    if (asJson) {
        json out = json::array();
        for (const auto& e : *entries) {
            out.push_back({{"name", narrow(e.name)}, {"directory", e.directory}, {"size", e.size}});
        }
        printJson(out);
        return 0;
    }
    for (const auto& e : *entries) {
        print(std::format(L"{:>14}  {}{}\n", e.directory ? std::wstring(L"<DIR>") : std::format(L"{}", e.size), e.name,
                          e.directory ? L"\\" : L""));
    }
    return 0;
}

int cmdExtract(const std::wstring& iso, const std::wstring& inner, const std::wstring& destination) {
    auto image = core::UdfImage::open(iso);
    if (!image) {
        return reportError(image.error());
    }
    auto node = image->find(inner);
    if (!node) {
        return reportError(node.error());
    }
    int lastPercent = -1;
    const core::TaskContext task{g_cancel, [&](double fraction, std::wstring_view) {
                                     const int percent = static_cast<int>(fraction * 100);
                                     if (percent != lastPercent) {
                                         lastPercent = percent;
                                         print(std::format(L"\r  {:>3}%  {}", percent, gib(static_cast<std::uint64_t>(fraction * static_cast<double>(node->size)))));
                                     }
                                 }};
    if (auto done = image->extract(*node, destination, task); !done) {
        print(L"\n");
        return reportError(done.error());
    }
    print(std::format(L"\n  extracted {} -> {}\n", inner, destination));
    return 0;
}

void printUsage() {
    print(L"wlcli " WL_VERSION_STRING L" - WinLove image engine CLI\n"
          L"\n"
          L"Usage:\n"
          L"  wlcli info <iso|wim|esd|swm> [--json]     List editions (reads ISOs in place, no admin)\n"
          L"  wlcli ls <iso> [dir] [--json]             List a directory inside an ISO\n"
          L"  wlcli extract <iso> <path-in-iso> <dest>  Copy a file out of an ISO (Ctrl+C cancels)\n"
          L"  wlcli elevated                            Print whether this process is elevated\n"
          L"  wlcli version | help\n"
          L"\n"
          L"Options: --json (machine-readable), --verbose (log to stdout)\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    std::vector<std::wstring> args;
    bool asJson = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        if (a == L"--json") {
            asJson = true;
        } else if (a == L"--verbose") {
            log::addSink(log::makeStdoutSink());
        } else {
            args.emplace_back(a);
        }
    }
    if (args.empty()) {
        printUsage();
        return 1;
    }
    const std::wstring& command = args[0];
    if (command == L"version") {
        print(L"" WL_VERSION_STRING L"\n");
        return 0;
    }
    if (command == L"info" && args.size() == 2) {
        return cmdInfo(args[1], asJson);
    }
    if (command == L"ls" && (args.size() == 2 || args.size() == 3)) {
        return cmdList(args[1], args.size() == 3 ? args[2] : L"", asJson);
    }
    if (command == L"extract" && args.size() == 4) {
        return cmdExtract(args[1], args[2], args[3]);
    }
    if (command == L"elevated") {
        print(core::isElevated() ? L"yes\n" : L"no\n");
        return 0;
    }
    if (command == L"help" || command == L"--help" || command == L"-h") {
        printUsage();
        return 0;
    }
    print(L"unknown command or wrong arguments: " + command + L"\n\n");
    printUsage();
    return 1;
}
