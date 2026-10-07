// D-082: what keeps a removal or a disabled service out of the queue — the runtimes kept apps need,
// and the guards the user keeps on.
#include "core/ops/Compat.h"

#include <doctest.h>

using namespace wl;
using namespace wl::core::ops;

namespace {

Operation removeApp(const wchar_t* fullName) {
    return {OpKind::RemoveAppx, fullName, L"", Risk::Low, 0};
}

const AppxNeeds kNeeds{
    {L"Microsoft.WindowsStore", {L"Microsoft.NET.Native.Framework.2.2", L"Microsoft.UI.Xaml.2.8", L"Microsoft.VCLibs.140.00"}},
    {L"Microsoft.WindowsCalculator", {L"Microsoft.UI.Xaml.2.8", L"Microsoft.VCLibs.140.00"}},
    {L"Microsoft.VCLibs.140.00", {}},
    {L"Microsoft.UI.Xaml.2.8", {L"Microsoft.VCLibs.140.00"}},
};
constexpr const wchar_t* kVcLibs = L"Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe";
constexpr const wchar_t* kXaml = L"Microsoft.UI.Xaml.2.8_8.2310.30001.0_x64__8wekyb3d8bbwe";
constexpr const wchar_t* kCalc = L"Microsoft.WindowsCalculator_11.2502.2.0_neutral_~_8wekyb3d8bbwe";
constexpr const wchar_t* kStore = L"Microsoft.WindowsStore_22509.1401.1.0_neutral_~_8wekyb3d8bbwe";

} // namespace

TEST_CASE("compat: a runtime stays while a kept app needs it, and goes with the apps") {
    CHECK(appxIdentity(kVcLibs) == L"Microsoft.VCLibs.140.00");
    CHECK(appxIdentity(L"NoUnderscore") == L"NoUnderscore");

    ChangeSet queue;
    auto block = compatBlock(removeApp(kVcLibs), {}, kNeeds, queue);
    CHECK(block.guards.empty());
    REQUIRE(block.neededBy.size() == 3); // Store, Calculator, and UI.Xaml (a runtime of runtimes)
    CHECK(block.neededBy[0] == L"Microsoft.UI.Xaml.2.8");

    queue.add(removeApp(kCalc));
    queue.add(removeApp(kStore));
    queue.add(removeApp(kXaml));
    CHECK(compatBlock(removeApp(kVcLibs), {}, kNeeds, queue).empty());
    queue.add(removeApp(kVcLibs));
    CHECK(compatConflicts(queue, {}, kNeeds).empty());

    // Calculator taken back: UI.Xaml and VCLibs must stay (VCLibs also because UI.Xaml stays).
    queue.remove(OpKind::RemoveAppx, kCalc);
    const auto conflicts = compatConflicts(queue, {}, kNeeds);
    REQUIRE(conflicts.size() == 2);
    CHECK(conflicts[0].op.target == kXaml);
    CHECK(conflicts[1].op.target == kVcLibs);
    CHECK(conflicts[1].block.neededBy.size() == 2); // Calculator and UI.Xaml; the Store goes
    // An app no one needs is never held.
    CHECK(compatBlock(removeApp(kCalc), {}, kNeeds, queue).empty());
}

TEST_CASE("compat: an active guard keeps its apps, components and services; an inactive one nothing") {
    const std::vector<CompatGuard> guards{
        {L"store", {L"Microsoft.WindowsStore", L"Microsoft.VCLibs"}, {}, {L"InstallService", L"AppXSvc"}},
        {L"windows-update", {}, {L"component-store-shrink", L"deep-modem"}, {L"wuauserv"}},
    };
    ChangeSet queue;
    CHECK(compatBlock(removeApp(kStore), guards, {}, queue).guards == std::vector<std::wstring>{L"store"});
    // "Microsoft.VCLibs" covers its ".140.00" and ".UWPDesktop" names, not a longer word.
    CHECK_FALSE(compatBlock(removeApp(L"Microsoft.VCLibs.140.00.UWPDesktop_14.0_x64__8wekyb3d8bbwe"), guards, {}, queue).empty());
    CHECK(compatBlock(removeApp(L"Microsoft.VCLibsExtra_1_x64__8wekyb3d8bbwe"), guards, {}, queue).empty());
    CHECK(compatBlock(removeApp(kCalc), guards, {}, queue).empty());

    CHECK(compatBlock({OpKind::ShrinkStore, L"component-store-shrink", L"{}", Risk::High, 0}, guards, {}, queue).guards ==
          std::vector<std::wstring>{L"windows-update"});
    CHECK(compatBlock({OpKind::RemoveComponent, L"DEEP-MODEM", L"{}", Risk::High, 0}, guards, {}, queue).guards.size() == 1);
    CHECK(compatBlock({OpKind::RemoveComponent, L"edge", L"{}", Risk::Medium, 0}, guards, {}, queue).empty());

    // A service may be set to manual or automatic; only "disabled" is kept out.
    CHECK(compatBlock({OpKind::SetServiceStart, L"WUAUSERV", L"disabled", Risk::Low, 0}, guards, {}, queue).guards.size() == 1);
    CHECK(compatBlock({OpKind::SetServiceStart, L"wuauserv", L"manual", Risk::Low, 0}, guards, {}, queue).empty());
    CHECK(compatBlock({OpKind::SetServiceStart, L"Spooler", L"disabled", Risk::Low, 0}, guards, {}, queue).empty());

    // Both guards hold one thing: both are named.
    const std::vector<CompatGuard> two{{L"a", {}, {}, {L"BITS"}}, {L"b", {}, {}, {L"bits"}}};
    CHECK(compatBlock({OpKind::SetServiceStart, L"BITS", L"disabled", Risk::Low, 0}, two, {}, queue).guards.size() == 2);

    queue.add({OpKind::SetServiceStart, L"wuauserv", L"disabled", Risk::Low, 0});
    queue.add({OpKind::SetServiceStart, L"Spooler", L"disabled", Risk::Low, 0});
    CHECK(compatConflicts(queue, {}, {}).empty());
    const auto conflicts = compatConflicts(queue, guards, {});
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].op.target == L"wuauserv");
}
