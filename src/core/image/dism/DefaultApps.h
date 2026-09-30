#pragma once
// D-054: default app associations of the image — which app opens .pdf, http, .mp4 … for every
// new user. Microsoft's supported way (the one Intune / OEMs use):
//   dism.exe /Image:<mount> /Import-DefaultAppAssociations:<file.xml>
// The XML is what `dism /Online /Export-DefaultAppAssociations` writes on a PC set up the way
// wanted (exportHostAssociations: "Bu bilgisayardan al"). An association to an app that is not
// installed yet is kept by Windows and takes effect once the app is (a winget step of P14).
// In the queue: one SetDefaultApps operation, target "associations", value = the XML.
#include "core/image/dism/Dism.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct AppAssociation {
    std::wstring identifier;      // ".pdf", "http", "mailto"
    std::wstring progId;          // "AcroExch.Document.DC"
    std::wstring applicationName; // "Adobe Acrobat"

    [[nodiscard]] bool operator==(const AppAssociation&) const = default;
};

// <DefaultAssociations><Association Identifier="…" ProgId="…" ApplicationName="…"/>…
// Entries without an identifier or ProgId are dropped; an identifier appears once (the last wins).
[[nodiscard]] Result<std::vector<AppAssociation>> parseAssociations(std::string_view xml);
[[nodiscard]] std::string associationsXml(const std::vector<AppAssociation>& associations);

// Writes the XML next to `scratch` and imports it into the image (dism.exe).
[[nodiscard]] Result<void> importAssociations(DismSession& session, std::string_view xml,
                                              const std::filesystem::path& scratchFolder, const TaskContext& task);

// This PC's associations (dism /Online /Export-DefaultAppAssociations; elevated process).
[[nodiscard]] Result<std::string> exportHostAssociations(const std::filesystem::path& scratchFolder);

} // namespace wl::core
