// P02: deleting editions — when the Images page allows it and what it says when it does not.
// The rewrite of a real WIM is tested on the lab copy (tools/lab_editions.ps1).
#include "app/controllers/ImageController.h"
#include "app/shell/Shell.h"
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"editions";
    std::filesystem::create_directories(dir);
    return dir / name;
}

core::SourceInfo source(core::ImageFormat format, const wchar_t* installImage, int editions) {
    core::SourceInfo info;
    info.path = scratch(L"missing") / L"install.wim"; // never on disk: nothing here may touch a file
    info.format = format;
    info.installImage = installImage;
    info.install.header.compression = core::WimCompression::Lzx;
    for (int i = 1; i <= editions; ++i) {
        core::ImageInfo image;
        image.index = i;
        image.name = L"Edition " + std::to_wstring(i);
        info.install.images.push_back(image);
    }
    return info;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::mutex mutex;
    std::vector<std::function<void()>> posted; // engine thread → "UI thread" (the test)
    std::vector<Str> refused;
    std::vector<ImageController::Failure> failed;
    std::vector<std::wstring> succeeded;

    ImageController controller{state,
                               ImageController::Events{
                                   [this](std::function<void()> fn) {
                                       std::scoped_lock lock(mutex);
                                       posted.push_back(std::move(fn));
                                   },
                                   [this](ImageController::Failure f, const Error&, int) { failed.push_back(f); },
                                   [this](Str, std::wstring detail) { succeeded.push_back(std::move(detail)); },
                                   [this](Str why) { refused.push_back(why); },
                                   [](std::wstring) {},
                                   {},
                                   {},
                               }};

    // Lets the engine job finish, then runs what it posted, in order.
    void settle() {
        state.engine().drain();
        std::vector<std::function<void()>> run;
        {
            std::scoped_lock lock(mutex);
            run.swap(posted);
        }
        for (auto& fn : run) {
            fn();
        }
    }
};

} // namespace

TEST_CASE("editions: deleting is allowed for a plain WIM, a setup folder and an ISO") {
    Fixture f;
    CHECK(f.controller.deleteRefusal() == Str::ImagesEmptyTitle);

    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    CHECK(f.controller.canDelete());
    f.state.setSource(source(core::ImageFormat::Folder, L"sources\\install.wim", 6));
    CHECK(f.controller.canDelete());
    // An ISO is copied to the work folder first: the button is no longer dead for it.
    f.state.setSource(source(core::ImageFormat::Iso, L"sources/install.wim", 6));
    CHECK(f.controller.canDelete());
}

TEST_CASE("editions: the refusal says why") {
    Fixture f;
    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 1));
    CHECK(f.controller.deleteRefusal() == Str::ImagesLastEdition);

    f.state.setSource(source(core::ImageFormat::Esd, L"install.esd", 6));
    CHECK(f.controller.deleteRefusal() == Str::ImagesDeleteNeedsWim);
    f.state.setSource(source(core::ImageFormat::Iso, L"sources/install.esd", 6));
    CHECK(f.controller.deleteRefusal() == Str::ImagesDeleteNeedsWim);

    auto split = source(core::ImageFormat::Folder, L"sources\\install.swm", 6);
    split.install.header.totalParts = 3;
    f.state.setSource(std::move(split));
    CHECK(f.controller.deleteRefusal() == Str::ImagesDeleteNeedsWim);

    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    f.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 4, L"Edition 4"});
    CHECK(f.controller.deleteRefusal() == Str::ImagesUnmountFirst);
    f.controller.removeEditions({2}, L"Edition 2");
    CHECK(f.refused == std::vector<Str>{Str::ImagesUnmountFirst});
    CHECK_FALSE(f.state.operation().has_value());
}

TEST_CASE("editions: one edition always stays") {
    Fixture f;
    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 3));
    f.controller.removeEditions({1, 2, 3}, L"3");
    CHECK(f.refused == std::vector<Str>{Str::ImagesLastEdition});
    CHECK_FALSE(f.state.operation().has_value());
}

TEST_CASE("editions: a failed removal leaves the source and the selection as they were") {
    Fixture f;
    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    f.state.select(4);
    f.controller.removeEditions({1, 2, 3, 5, 6}, L"5 editions");
    REQUIRE(f.state.operation().has_value());
    CHECK(f.state.operation()->kind == EngineOperation::Kind::Deleting);
    CHECK(f.state.operation()->edition == L"5 editions");
    CHECK(f.controller.deleteRefusal() == Str::ImagesBusy);

    f.settle(); // the WIM is not on disk: the engine reports it
    CHECK_FALSE(f.state.operation().has_value());
    CHECK(f.failed == std::vector<ImageController::Failure>{ImageController::Failure::Delete});
    CHECK(f.succeeded.empty());
    CHECK(f.state.source()->install.images.size() == 6);
    CHECK(f.state.selectedIndex() == 4);
}

TEST_CASE("editions: right click → \"Yalnız bu sürümü tut…\" → confirm deletes every other edition") {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const Localization strings = Localization::fromJson(text.str()).value();

    AppState state{scratch(L"recent-shell.json"), scratch(L"settings-shell.json")};
    std::vector<std::function<void()>> posted;
    Shell::Services services;
    services.minimize = services.toggleMaximize = services.close = services.toggleTheme = [] {};
    services.postToUi = [&](std::function<void()> fn) { posted.push_back(std::move(fn)); };
    ui::Host host(ui::HostServices{[] {}, nullptr, nullptr, test::graphics().text.get()});
    auto shell = std::make_unique<Shell>(strings, Language::Turkish, state, services);
    Shell* raw = shell.get();
    host.setRoot(std::move(shell));
    host.layout({1440, 900});

    state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    raw->showPage(PageId::Images);
    host.layout({1440, 900});

    auto key = [&](UINT vk) { host.onKeyDown(ui::KeyEvent{vk, false, false, false}); };
    // Row 4 of the table (header 148, rows of 24 from 160): the right click selects it.
    REQUIRE(host.onContextMenu({400, 244}));
    host.layout({1440, 900});
    CHECK(state.selectedIndex() == 4);
    // Bağla · Dışa aktar · Sürümü sil… · Yalnız bu sürümü tut…
    for (int i = 0; i < 4; ++i) {
        key(VK_DOWN);
    }
    key(VK_RETURN);
    host.layout({1440, 900});
    CHECK_FALSE(state.operation().has_value()); // the dialog asks first
    key(VK_TAB);                                // Vazgeç → Sil
    key(VK_RETURN);

    REQUIRE(state.operation().has_value());
    CHECK(state.operation()->kind == EngineOperation::Kind::Deleting);
    CHECK(state.operation()->edition == L"5 sürüm");
    state.engine().drain(); // the fake WIM is not on disk: the job fails, nothing changes
    for (auto& fn : std::vector(std::move(posted))) {
        fn();
    }
    CHECK_FALSE(state.operation().has_value());
    CHECK(state.source()->install.images.size() == 6);
}
