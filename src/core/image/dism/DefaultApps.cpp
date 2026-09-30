#include "core/image/dism/DefaultApps.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/dism/DismExe.h"
#include "core/system/Process.h"

#include <pugixml.hpp>

#include <windows.h>

#include <format>
#include <fstream>
#include <sstream>

namespace wl::core {

Result<std::vector<AppAssociation>> parseAssociations(std::string_view xml) {
    pugi::xml_document doc;
    const auto parsed = doc.load_buffer(xml.data(), xml.size());
    if (!parsed) {
        return fail(ErrorCode::ParseError, L"not an XML file", utf8::toWide(parsed.description()));
    }
    const auto root = doc.child("DefaultAssociations");
    if (!root) {
        return fail(ErrorCode::ParseError, L"not a default associations file (no <DefaultAssociations>)");
    }
    std::vector<AppAssociation> list;
    for (const auto& node : root.children("Association")) {
        AppAssociation a{utf8::toWide(node.attribute("Identifier").as_string()),
                         utf8::toWide(node.attribute("ProgId").as_string()),
                         utf8::toWide(node.attribute("ApplicationName").as_string())};
        if (a.identifier.empty() || a.progId.empty()) {
            continue;
        }
        std::erase_if(list, [&](const AppAssociation& b) { return _wcsicmp(b.identifier.c_str(), a.identifier.c_str()) == 0; });
        list.push_back(std::move(a));
    }
    return list;
}

std::string associationsXml(const std::vector<AppAssociation>& associations) {
    pugi::xml_document doc;
    auto decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    auto root = doc.append_child("DefaultAssociations");
    for (const auto& a : associations) {
        auto node = root.append_child("Association");
        node.append_attribute("Identifier") = utf8::fromWide(a.identifier).c_str();
        node.append_attribute("ProgId") = utf8::fromWide(a.progId).c_str();
        node.append_attribute("ApplicationName") = utf8::fromWide(a.applicationName).c_str();
    }
    std::ostringstream out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

Result<void> importAssociations(DismSession& session, std::string_view xml, const std::filesystem::path& scratchFolder,
                                const TaskContext& task) {
    auto parsed = parseAssociations(xml);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(scratchFolder, ec);
    const auto file = scratchFolder / std::format(L"winlove-associations-{}.xml", GetCurrentProcessId());
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        const std::string text = associationsXml(*parsed); // re-written: only what parsed goes to DISM
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return fail(ErrorCode::IoError, L"could not write the associations file", file.wstring());
        }
    }
    task.report(-1.0, L"associations");
    const auto run = runDismExe(session, std::format(L"/Import-DefaultAppAssociations:\"{}\"", file.wstring()));
    std::filesystem::remove(file, ec);
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismExeFailure(*run, L"the default app associations could not be imported"));
    }
    log::info("dism", std::format(L"{} default app association(s) imported", parsed->size()));
    task.report(1.0, L"associations");
    return {};
}

Result<std::string> exportHostAssociations(const std::filesystem::path& scratchFolder) {
    auto dism = systemTool(L"dism.exe");
    if (!dism) {
        return std::unexpected(dism.error());
    }
    std::error_code ec;
    std::filesystem::create_directories(scratchFolder, ec);
    const auto file = scratchFolder / std::format(L"winlove-host-associations-{}.xml", GetCurrentProcessId());
    std::string output;
    auto exit = runProcess(std::format(L"\"{}\" /English /Online /Export-DefaultAppAssociations:\"{}\"", *dism, file.wstring()),
                           [&](std::string_view chunk) { output.append(chunk); });
    if (!exit) {
        return std::unexpected(exit.error());
    }
    std::ifstream in(file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::filesystem::remove(file, ec);
    if (*exit != 0 || buffer.str().empty()) {
        log::error("dism", utf8::toWide(output));
        return fail(ErrorCode::DismFailure, L"this PC's default app associations could not be exported",
                    std::format(L"dism.exe exit code 0x{:08X}", *exit), static_cast<std::int32_t>(*exit));
    }
    return buffer.str();
}

} // namespace wl::core
