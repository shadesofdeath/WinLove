// P02: deleting, renaming, verifying editions and marking several of them — when the Images page
// allows it and what it says when it does not.
// The rewrite of a real WIM is tested on the lab copy (tools/lab_editions.ps1).
#include "app/controllers/ImageController.h"
#include "app/pages/images/EditionSelection.h"
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
    // Bağla · Dışa aktar · Yeniden adlandır… · Sürümü sil… · Yalnız bu sürümü tut…
    for (int i = 0; i < 5; ++i) {
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

TEST_CASE("edition selection: click, Ctrl+click, Shift range, Ctrl+A") {
    EditionSelection s;
    s.only(3);
    CHECK(s.rows == std::vector<int>{3});
    CHECK(s.primary == 3);

    s.toggle(1); // Ctrl+click adds, and the row added is the one shown
    s.toggle(5);
    CHECK(s.rows == std::vector<int>{1, 3, 5});
    CHECK(s.primary == 5);
    CHECK(s.contains(3));
    CHECK_FALSE(s.contains(2));

    s.toggle(5); // taking the primary row out moves the primary to a row that stays
    CHECK(s.rows == std::vector<int>{1, 3});
    CHECK(s.primary == 3);
    s.toggle(1);
    s.toggle(3); // the last marked row stays marked
    CHECK(s.rows == std::vector<int>{3});
    CHECK(s.primary == 3);

    s.only(2);
    s.extendTo(4); // Shift: from the last plain click
    CHECK(s.rows == std::vector<int>{2, 3, 4});
    CHECK(s.primary == 4);
    s.extendTo(0); // the anchor stays, the range turns around
    CHECK(s.rows == std::vector<int>{0, 1, 2});
    CHECK(s.primary == 0);

    s.all(6);
    CHECK(s.rows == std::vector<int>{0, 1, 2, 3, 4, 5});
    CHECK(s.primary == 0);
}

TEST_CASE("editions: several can be marked; the state keeps them sorted with the primary among them") {
    Fixture f;
    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    CHECK(f.state.selection() == std::vector<int>{1});
    int notified = 0;
    f.state.subscribe([&](AppState::Change change) { notified += change == AppState::Change::Selection ? 1 : 0; });

    f.state.selectMany({5, 2, 5, 3}, 3);
    CHECK(f.state.selection() == std::vector<int>{2, 3, 5});
    CHECK(f.state.selectedIndex() == 3);
    CHECK(notified == 1);
    f.state.selectMany({2, 3, 5}, 3); // nothing new: nobody is told
    CHECK(notified == 1);
    f.state.selectMany({2}, 4); // a primary outside the set joins it
    CHECK(f.state.selection() == std::vector<int>{2, 4});
    f.state.select(6);
    CHECK(f.state.selection() == std::vector<int>{6});
}

TEST_CASE("editions: rename and verify refuse what they cannot do, and a failed rename changes nothing") {
    Fixture f;
    f.state.setSource(source(core::ImageFormat::Esd, L"install.esd", 6));
    CHECK(f.controller.editRefusal() == Str::ImagesDeleteNeedsWim);
    CHECK(f.controller.verifyRefusal() == Str::ImagesVerifyEsd);
    f.controller.verify();
    f.controller.renameEdition(1, L"x", L"");
    CHECK(f.refused == std::vector<Str>{Str::ImagesVerifyEsd, Str::ImagesDeleteNeedsWim});
    CHECK_FALSE(f.state.operation().has_value());

    // One edition is enough to rename (unlike delete), and a mounted image can still be verified.
    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 1));
    CHECK_FALSE(f.controller.editRefusal());
    CHECK(f.controller.deleteRefusal() == Str::ImagesLastEdition);
    f.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Edition 1"});
    CHECK(f.controller.editRefusal() == Str::ImagesUnmountFirst);
    CHECK_FALSE(f.controller.verifyRefusal());
    f.state.setMounted(std::nullopt);

    f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    f.state.selectMany({2, 4}, 4);
    f.controller.renameEdition(4, L"Windows 11 Pro WinLove", L"");
    REQUIRE(f.state.operation().has_value());
    CHECK(f.state.operation()->kind == EngineOperation::Kind::Renaming);
    f.settle(); // the WIM is not on disk
    CHECK(f.failed == std::vector<ImageController::Failure>{ImageController::Failure::Rename});
    CHECK(f.state.source()->install.images[3].name == L"Edition 4");
    CHECK(f.state.selection() == std::vector<int>{2, 4});

    f.controller.verify();
    REQUIRE(f.state.operation().has_value());
    CHECK(f.state.operation()->kind == EngineOperation::Kind::Verifying);
    f.settle();
    CHECK(f.failed.back() == ImageController::Failure::Verify);
}

TEST_CASE("editions: the marked ones are deleted together; \"keep only\" keeps the marked ones") {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const Localization strings = Localization::fromJson(text.str()).value();

    AppState state{scratch(L"recent-many.json"), scratch(L"settings-many.json")};
    std::vector<std::function<void()>> posted;
    Shell::Services services;
    services.minimize = services.toggleMaximize = services.close = services.toggleTheme = [] {};
    services.postToUi = [&](std::function<void()> fn) { posted.push_back(std::move(fn)); };
    ui::Host host(ui::HostServices{[] {}, nullptr, nullptr, test::graphics().text.get()});
    auto shell = std::make_unique<Shell>(strings, Language::Turkish, state, services);
    Shell* raw = shell.get();
    host.setRoot(std::move(shell));
    state.setSource(source(core::ImageFormat::Wim, L"install.wim", 6));
    raw->showPage(PageId::Images);
    host.layout({1440, 900});
    auto key = [&](UINT vk) { host.onKeyDown(ui::KeyEvent{vk, false, false, false}); };
    auto confirm = [&] {
        host.layout({1440, 900});
        key(VK_TAB);    // Vazgeç → Sil
        key(VK_RETURN);
    };
    auto settle = [&] {
        state.engine().drain();
        for (auto& fn : std::vector(std::move(posted))) {
            fn();
        }
        posted.clear();
    };

    state.selectMany({2, 3}, 3);
    raw->askDeleteSelected();
    CHECK_FALSE(state.operation().has_value()); // asks first
    confirm();
    REQUIRE(state.operation().has_value());
    CHECK(state.operation()->edition == L"2 sürüm");
    settle();

    // Keeping the two marked editions deletes the other four.
    state.selectMany({2, 3}, 3);
    raw->askDeleteSelected(/*keepOnly=*/true);
    confirm();
    REQUIRE(state.operation().has_value());
    CHECK(state.operation()->edition == L"4 sürüm");
    settle();

    // Everything marked: there is nothing to keep, so nothing is asked.
    state.selectMany({1, 2, 3, 4, 5, 6}, 1);
    raw->askDeleteSelected();
    host.layout({1440, 900});
    CHECK_FALSE(host.hasModal());
    CHECK_FALSE(state.operation().has_value());
}

TEST_CASE("edition upgrade: the dialog puts the chosen edition into the queue, as the first thing Uygula does") {
    std::ifstream file(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const Localization strings = Localization::fromJson(text.str()).value();

    AppState state{scratch(L"recent-upgrade.json"), scratch(L"settings-upgrade.json")};
    Shell::Services services;
    services.minimize = services.toggleMaximize = services.close = services.toggleTheme = [] {};
    services.postToUi = [](std::function<void()> fn) { fn(); };
    ui::Host host(ui::HostServices{[] {}, nullptr, nullptr, test::graphics().text.get()});
    auto shell = std::make_unique<Shell>(strings, Language::Turkish, state, services);
    Shell* raw = shell.get();
    host.setRoot(std::move(shell));
    state.setSource(source(core::ImageFormat::Wim, L"install.wim", 3));
    raw->showPage(PageId::Images);
    host.layout({1440, 900});

    // Nothing mounted: nothing to upgrade, nothing asked.
    raw->askUpgradeEdition();
    CHECK_FALSE(host.hasModal());

    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Edition 1"});
    raw->images().rememberEditions(core::ImageEditions{L"Core", {L"CoreSingleLanguage", L"Professional", L"Education"}});
    raw->askUpgradeEdition();
    host.layout({1440, 900});
    REQUIRE(host.hasModal());
    CHECK(state.changes().empty()); // the dialog alone changes nothing

    host.onKeyDown(ui::KeyEvent{VK_RETURN, false, false, false}); // "Kuyruğa ekle": Pro is preselected
    CHECK_FALSE(host.hasModal());
    const auto* queued = state.changes().find(core::ops::OpKind::SetEdition, L"edition");
    REQUIRE(queued != nullptr);
    CHECK(queued->value == L"Professional");
    CHECK(core::ops::plan(state.changes()).steps.front().phase == core::ops::Phase::Edition);

    // An image that is already at the top has nowhere to go.
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 2, L"Edition 2"});
    raw->images().rememberEditions(core::ImageEditions{L"ProfessionalWorkstation", {}});
    raw->askUpgradeEdition();
    host.layout({1440, 900});
    CHECK_FALSE(host.hasModal());
}
