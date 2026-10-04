// D-062: a desktop wallpaper keeps Windows Spotlight off the desktop; clearing it takes back only
// what it brought.
#include "app/controllers/BrandingController.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;

namespace {
std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"branding";
    std::filesystem::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE("branding: the wallpaper's Spotlight policy, and clearing it") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    BrandingController branding(state);

    const auto spotlight = BrandingController::spotlightOffOperations();
    REQUIRE(spotlight.size() == 2);
    CHECK(spotlight[0].kind == OpKind::SetRegistryFirstLogon);
    CHECK(spotlight[0].target == L"HKCU\\Software\\Policies\\Microsoft\\Windows\\CloudContent::DisableSpotlightCollectionOnDesktop");
    CHECK(spotlight[0].value == L"dword:00000001");

    // As setPicture queues them (it reads the picture first, which a test has none of).
    std::vector<Operation> ops{Operation{OpKind::SetPicture, L"wallpaper", L"C:\\p\\wall.jpg"}};
    ops.insert(ops.end(), spotlight.begin(), spotlight.end());
    state.queueMany(ops);
    CHECK(state.changes().size() == 3);
    branding.clearPicture(core::PictureSlot::Wallpaper);
    CHECK(state.changes().empty());

    // A Spotlight setting the user changed on its own (another value) stays.
    state.queue(Operation{OpKind::SetPicture, L"wallpaper", L"C:\\p\\wall.jpg"});
    state.queue(Operation{OpKind::SetRegistryFirstLogon, spotlight[0].target, L"dword:00000000"});
    branding.clearPicture(core::PictureSlot::Wallpaper);
    REQUIRE(state.changes().size() == 1);
    CHECK(state.changes().operations().front().value == L"dword:00000000");
}
