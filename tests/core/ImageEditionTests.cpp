// Edition change (dism.exe /Set-Edition): what can be checked without an image — reading
// dism.exe's output, what may go onto its command line, the names editions get. The change
// itself runs on a mounted copy of the lab image: tools/lab_edition.ps1 (admin).
#include "core/image/dism/DismExe.h"
#include "core/image/dism/Edition.h"
#include "core/image/wim/WimGapi.h"
#include "core/ops/Planner.h"

#include <doctest.h>

using namespace wl;
using namespace wl::core;

TEST_CASE("editions: dism.exe's lists are read line by line") {
    const std::string_view current = "\r\nDeployment Image Servicing and Management tool\r\nVersion: 10.0.26100.5074\r\n\r\n"
                                     "Image Version: 10.0.26200.8037\r\n\r\nCurrent edition is:\r\n\r\n"
                                     "Current Edition : Core\r\n\r\nThe operation completed successfully.\r\n";
    const std::string_view targets = "Editions that can be upgraded to:\r\n\r\nTarget Edition : Professional\r\n"
                                     "Target Edition : ProfessionalEducation\r\n  Target Edition :  Education  \r\n\r\n"
                                     "The operation completed successfully.\r\n";
    const ImageEditions editions = parseEditions(current, targets);
    CHECK(editions.current == L"Core");
    CHECK(editions.targets == std::vector<std::wstring>{L"Professional", L"ProfessionalEducation", L"Education"});

    // The top edition has nowhere to go; an error text is not an edition.
    const ImageEditions none = parseEditions("Current Edition : ProfessionalWorkstation\n",
                                             "Editions that can be upgraded to:\n\n(The current edition cannot be upgraded to any target editions.)\n");
    CHECK(none.current == L"ProfessionalWorkstation");
    CHECK(none.targets.empty());
    CHECK(parseEditions("Error: 50\r\nCurrent Edition Foo\r\n", "") == ImageEditions{});
}

TEST_CASE("editions: only an edition id reaches the command line") {
    CHECK(isEditionId(L"Professional"));
    CHECK(isEditionId(L"IoTEnterpriseS"));
    CHECK_FALSE(isEditionId(L""));
    CHECK_FALSE(isEditionId(L"Pro /Foo"));
    CHECK_FALSE(isEditionId(L"Professional\""));
    CHECK_FALSE(isEditionId(L"Profesyonel\u015e"));
    CHECK_FALSE(isEditionId(std::wstring(65, L'a')));

    // A trailing backslash must not swallow the closing quote of /Image:"…".
    CHECK(dismExeCommandLine(L"C:\\Windows\\System32\\dism.exe", L"D:\\my mount\\", L"/Set-Edition:Professional") ==
          L"\"C:\\Windows\\System32\\dism.exe\" /English /Image:\"D:\\my mount\" /Set-Edition:Professional");
}

TEST_CASE("editions: the name an image gets after the change") {
    CHECK(editionDisplayName(L"Professional", 26200) == L"Windows 11 Pro");
    CHECK(editionDisplayName(L"CoreSingleLanguage", 26200) == L"Windows 11 Home Single Language");
    CHECK(editionDisplayName(L"ProfessionalWorkstation", 22631) == L"Windows 11 Pro for Workstations");
    CHECK(editionDisplayName(L"Education", 19045) == L"Windows 10 Education");
    CHECK(editionDisplayName(L"SomethingNew", 26200) == L"Windows 11 SomethingNew");
}

TEST_CASE("edition text: a name is required; the file must exist") {
    const auto missing = std::filesystem::temp_directory_path() / L"wl-tests" / L"no-such-image.wim";
    const auto unnamed = setImageText(missing, 1, ImageText{L"  \t ", L"description", std::nullopt});
    REQUIRE_FALSE(unnamed.has_value());
    CHECK(unnamed.error().code == ErrorCode::InvalidArgument);
    CHECK_FALSE(setImageText(missing, 1, ImageText{std::wstring(256, L'x'), L"", std::nullopt}).has_value());
    const auto noFile = setImageText(missing, 1, ImageText{L"Windows 11 Pro", L"", std::nullopt});
    REQUIRE_FALSE(noFile.has_value());
    CHECK(noFile.error().code != ErrorCode::InvalidArgument);
}

TEST_CASE("editions: after a change the image is named for its new edition, unless the user named it") {
    ImageInfo image;
    image.build = 26200;
    image.editionId = L"Core";
    image.name = image.description = image.displayName = image.displayDescription = L"Windows 11 Home";
    ImageText text = textAfterEditionChange(image, L"Professional");
    CHECK(text.name == L"Windows 11 Pro");
    CHECK(text.description == L"Windows 11 Pro");
    CHECK(text.flags == std::wstring(L"Professional"));

    // A name of the user's own stays; the description, still Microsoft's, follows the edition.
    image.displayName = L"Ev bilgisayar\u0131";
    text = textAfterEditionChange(image, L"Professional");
    CHECK(text.name == L"Ev bilgisayar\u0131");
    CHECK(text.description == L"Windows 11 Pro");
    CHECK(text.flags == std::wstring(L"Professional"));

    // An older image without display texts: the plain name counts.
    ImageInfo plain;
    plain.build = 19045;
    plain.editionId = L"Core";
    plain.name = L"Windows 10 Home";
    text = textAfterEditionChange(plain, L"Education");
    CHECK(text.name == L"Windows 10 Education");
    CHECK(text.description == L"Windows 10 Education");
}

TEST_CASE("editions: the change is one queue slot, saved in presets, and the first step of a plan") {
    using namespace wl::core::ops;
    ChangeSet changes;
    changes.add(Operation{OpKind::RemoveAppx, L"Microsoft.BingNews_1_x64__8wekyb3d8bbwe"});
    changes.add(Operation{OpKind::SetEdition, L"edition", L"Professional", Risk::Medium});
    changes.add(Operation{OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\X::Y", L"dword:1"});
    changes.add(Operation{OpKind::SetEdition, L"edition", L"Education", Risk::Medium}); // replaces: one edition
    CHECK(changes.count(OpKind::SetEdition) == 1);
    REQUIRE(changes.find(OpKind::SetEdition, L"edition") != nullptr);
    CHECK(changes.find(OpKind::SetEdition, L"edition")->value == L"Education");

    const auto back = ChangeSet::fromJson(changes.toJson());
    REQUIRE(back.has_value());
    REQUIRE(back->find(OpKind::SetEdition, L"edition") != nullptr);
    CHECK(back->find(OpKind::SetEdition, L"edition")->value == L"Education");

    const ApplyPlan p = plan(changes);
    REQUIRE(p.steps.size() == 3);
    CHECK(p.steps[0].phase == Phase::Edition);
    CHECK(p.steps[0].operation.kind == OpKind::SetEdition);
    CHECK(p.steps[1].phase == Phase::Remove);
}
