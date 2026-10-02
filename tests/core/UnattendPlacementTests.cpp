// P13 (schneegans-style options): every element is written in the pass and component Windows
// Setup reads it from, and the options come back unchanged from the file.
#include "base/Utf8.h"
#include "core/unattend/Unattend.h"

#include <doctest.h>
#include <pugixml.hpp>

#include <string>

using namespace wl;
using namespace wl::core;

namespace {

UnattendOptions extended() {
    UnattendOptions o;
    o.accountName = L"Berkay";
    o.password = L"p@ss";
    o.autoLogon = true;
    o.extraAccounts = {{L"Ali", L"a1", false}, {L"Ayse", L"", true}};
    o.enableAdministrator = true;
    o.administratorPassword = L"Adm1n!";
    o.passwordsNeverExpire = true;
    o.disableLockout = true;
    o.randomComputerName = true;
    o.registeredOwner = L"Berkay";
    o.registeredOrganization = L"WinLove";
    o.disk = UnattendDisk::WipeGpt;
    o.diskId = 1;
    o.recoveryPartition = true;
    o.hideWifiSetup = true;
    o.hideOemRegistration = true;
    o.preventDeviceEncryption = true;
    o.specializeCommands = {L"cmd /c echo specialize > C:\\Windows\\Temp\\s.txt"};
    o.firstLogonCommands = {L"powershell -NoProfile -Command \"Write-Output first\"", L"cmd /c echo 2"};
    return o;
}

pugi::xml_document parse(const std::wstring& xml) {
    pugi::xml_document doc;
    const std::string utf8 = utf8::fromWide(xml);
    REQUIRE(doc.load_buffer(utf8.data(), utf8.size()));
    return doc;
}

// <settings pass=…><component name=…> of the document.
pugi::xml_node component(const pugi::xml_document& doc, const char* pass, const char* name) {
    for (const auto& settings : doc.child("unattend").children("settings")) {
        if (std::string(settings.attribute("pass").as_string()) == pass) {
            for (const auto& c : settings.children("component")) {
                if (std::string(c.attribute("name").as_string()) == name) {
                    return c;
                }
            }
        }
    }
    return {};
}

std::string text(const pugi::xml_node& node) {
    return node.text().as_string();
}

} // namespace

TEST_CASE("unattend: the extended options are written where Setup reads them") {
    const auto doc = parse(buildUnattendXml(extended()));

    // windowsPE · Microsoft-Windows-Setup: the chosen disk, a recovery partition first, Windows last.
    const auto setup = component(doc, "windowsPE", "Microsoft-Windows-Setup");
    REQUIRE(setup);
    const auto disk = setup.child("DiskConfiguration").child("Disk");
    CHECK(text(disk.child("DiskID")) == "1");
    std::vector<std::string> types;
    for (const auto& p : disk.child("CreatePartitions").children("CreatePartition")) {
        types.push_back(text(p.child("Type")));
    }
    CHECK(types == std::vector<std::string>{"Primary", "EFI", "MSR", "Primary"});
    const auto recovery = disk.child("ModifyPartitions").child("ModifyPartition");
    CHECK(text(recovery.child("TypeID")) == "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC");
    CHECK(text(recovery.child("Label")) == "Recovery");
    CHECK(text(setup.child("DiskConfiguration").child("WillShowUI")) == "OnError");
    CHECK(text(setup.child("ImageInstall").child("OSImage").child("InstallTo").child("DiskID")) == "1");
    CHECK(text(setup.child("ImageInstall").child("OSImage").child("InstallTo").child("PartitionID")) == "4");

    // specialize · Shell-Setup: random name, owner, organisation.
    const auto specialize = component(doc, "specialize", "Microsoft-Windows-Shell-Setup");
    CHECK(text(specialize.child("ComputerName")) == "*");
    CHECK(text(specialize.child("RegisteredOwner")) == "Berkay");
    CHECK(text(specialize.child("RegisteredOrganization")) == "WinLove");

    // specialize · Deployment: the SYSTEM commands, in order, user commands last.
    const auto deployment = component(doc, "specialize", "Microsoft-Windows-Deployment");
    std::vector<std::string> commands;
    for (const auto& c : deployment.child("RunSynchronous").children("RunSynchronousCommand")) {
        commands.push_back(text(c.child("Path")));
        CHECK(text(c.child("Order")) == std::to_string(commands.size()));
    }
    REQUIRE(commands.size() == 5);
    CHECK(commands[0] == "net.exe user Administrator /active:yes");
    CHECK(commands[1] == "net.exe accounts /maxpwage:UNLIMITED");
    CHECK(commands[2] == "net.exe accounts /lockoutthreshold:0");
    CHECK(commands[3].find("PreventDeviceEncryption") != std::string::npos);
    CHECK(commands[4].find("specialize") != std::string::npos);

    // oobeSystem · Shell-Setup: AutoLogon, FirstLogonCommands, OOBE, UserAccounts — in that order.
    const auto shell = component(doc, "oobeSystem", "Microsoft-Windows-Shell-Setup");
    std::vector<std::string> order;
    for (const auto& child : shell.children()) {
        order.push_back(child.name());
    }
    CHECK(order == std::vector<std::string>{"AutoLogon", "FirstLogonCommands", "OOBE", "UserAccounts"});
    const auto first = shell.child("FirstLogonCommands").child("SynchronousCommand");
    CHECK(text(first.child("Order")) == "1");
    CHECK(text(first.child("RequiresUserInput")) == "false");
    CHECK(text(shell.child("OOBE").child("HideWirelessSetupInOOBE")) == "true");
    CHECK(text(shell.child("OOBE").child("HideOEMRegistrationScreen")) == "true");
    const auto accounts = shell.child("UserAccounts");
    CHECK(std::string(accounts.first_child().name()) == "AdministratorPassword");
    CHECK(text(accounts.child("AdministratorPassword").child("PlainText")) == "false");
    std::vector<std::string> groups;
    for (const auto& a : accounts.child("LocalAccounts").children("LocalAccount")) {
        groups.push_back(text(a.child("Name")) + ":" + text(a.child("Group")));
    }
    CHECK(groups == std::vector<std::string>{"Berkay:Administrators", "Ali:Users", "Ayse:Administrators"});
}

TEST_CASE("unattend: the extended options come back from the file") {
    const UnattendOptions o = extended();
    const auto back = parseUnattendXml(utf8::fromWide(buildUnattendXml(o)));
    REQUIRE(back);
    CHECK(back->extraAccounts == o.extraAccounts);
    CHECK(back->enableAdministrator);
    CHECK(back->administratorPassword == L"Adm1n!"); // encoded with its own element name
    CHECK(back->passwordsNeverExpire);
    CHECK(back->disableLockout);
    CHECK(back->randomComputerName);
    CHECK(back->computerName.empty());
    CHECK(back->registeredOwner == L"Berkay");
    CHECK(back->registeredOrganization == L"WinLove");
    CHECK(back->diskId == 1);
    CHECK(back->recoveryPartition);
    CHECK(back->hideWifiSetup);
    CHECK(back->hideOemRegistration);
    CHECK(back->preventDeviceEncryption);
    CHECK(back->specializeCommands == o.specializeCommands); // own commands are not repeated here
    CHECK(back->firstLogonCommands == o.firstLogonCommands);
    // Written again, the file is the same.
    CHECK(buildUnattendXml(*back) == buildUnattendXml(o));
}

TEST_CASE("unattend: MBR with a recovery partition, and what the extended options refuse") {
    UnattendOptions o;
    o.disk = UnattendDisk::WipeMbr;
    o.recoveryPartition = true;
    const auto doc = parse(buildUnattendXml(o));
    const auto setup = component(doc, "windowsPE", "Microsoft-Windows-Setup");
    CHECK(text(setup.child("DiskConfiguration").child("Disk").child("ModifyPartitions").child("ModifyPartition").child("TypeID")) == "0x27");
    CHECK(text(setup.child("ImageInstall").child("OSImage").child("InstallTo").child("PartitionID")) == "3");

    UnattendOptions bad;
    bad.accountName = L"Ali";
    bad.extraAccounts = {{L"ali", L"", false}}; // the same name again
    bad.diskId = 99;
    const auto problems = validateUnattend(bad);
    CHECK(std::ranges::find(problems, UnattendProblem::ExtraAccountName) != problems.end());
    CHECK(std::ranges::find(problems, UnattendProblem::DiskId) != problems.end());
    // A random name needs no valid typed name.
    UnattendOptions random;
    random.randomComputerName = true;
    random.computerName = L"not valid name";
    CHECK(validateUnattend(random).empty());
}
