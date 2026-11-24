// D-153: a removed app's gap is fixed in the same queue edit (one undo step), the fix leaves with the
// removal, and what the user chose for the same slot stays. Also the preset's "Yerine koy" shape:
// emptying the queue and queuing a preset in one batch is one undo step.
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/RemovalGaps.h"
#include "base/Utf8.h"
#include "core/image/dism/DefaultApps.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;

namespace {

std::string shipped(const wchar_t* file) {
    std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog" / file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"gaps";
    std::filesystem::create_directories(dir);
    return dir / name;
}

const ImageSetting& settingOf(const ImageSettingsCatalog& catalog, std::string_view id) {
    for (const auto& s : catalog.settings()) {
        if (s.id == id) {
            return s;
        }
    }
    FAIL("no setting " << id);
    return catalog.settings().front();
}

int optionIndex(const ImageSetting& setting, std::string_view id) {
    for (std::size_t i = 0; i < setting.options.size(); ++i) {
        if (setting.options[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<core::AppAssociation> associations(const AppState& state) {
    if (const auto* op = state.changes().find(OpKind::SetDefaultApps, L"associations")) {
        if (auto list = core::parseAssociations(utf8::fromWide(op->value))) {
            return *list;
        }
    }
    return {};
}

struct Fixture {
    std::filesystem::path mount = scratch(L"mount");
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ImageSettingsCatalog catalog = *ImageSettingsCatalog::parse(shipped(L"settings.json"));
    RemovalGaps gaps{catalog, [this] { return mount; }};
    Fixture() {
        std::filesystem::create_directories(mount);
        state.setMounted(MountedImage{mount, L"C:\\w\\install.wim", 1, L"Pro"});
        state.setQueueCompanion(&gaps);
    }
    ~Fixture() { state.setQueueCompanion(nullptr); }
    [[nodiscard]] int option(std::string_view id) const {
        return ImageSettingsController::optionIn(state.changes(), settingOf(catalog, id));
    }
};

const wchar_t kSnipping[] = L"Microsoft.ScreenSketch_11.2409.25.0_x64__8wekyb3d8bbwe";
const wchar_t kPhotos[] = L"Microsoft.Windows.Photos_2024.11090.24002.0_x64__8wekyb3d8bbwe";
const wchar_t kNotepad[] = L"Microsoft.WindowsNotepad_11.2409.9.0_x64__8wekyb3d8bbwe";

} // namespace

TEST_CASE("gaps: removing the Snipping Tool makes Print Screen copy the screen, in one undo step") {
    Fixture f;
    const auto& printScreen = settingOf(f.catalog, "print-screen");
    REQUIRE(f.option("print-screen") == printScreen.defaultOption);
    f.state.queue(Operation{OpKind::RemoveAppx, kSnipping});
    CHECK(f.option("print-screen") == optionIndex(printScreen, "copy"));
    CHECK(f.gaps.fixesOf(kSnipping).size() == 1);
    f.state.undoQueue(); // the removal and its fix go together
    CHECK(f.state.changes().empty());
    f.state.redoQueue();
    CHECK(f.option("print-screen") == optionIndex(printScreen, "copy"));
    // Taking the removal out takes the fix out.
    f.state.unqueue(OpKind::RemoveAppx, kSnipping);
    CHECK(f.state.changes().empty());
}

TEST_CASE("gaps: the user's own choice for the slot stays, before and after") {
    Fixture f;
    const auto& printScreen = settingOf(f.catalog, "print-screen");
    const int snip = optionIndex(printScreen, "snip");
    f.state.queueMany(ImageSettingsController::operationsFor(printScreen, snip));
    f.state.queue(Operation{OpKind::RemoveAppx, kSnipping});
    CHECK(f.option("print-screen") == snip); // not overwritten
    f.state.unqueue(OpKind::RemoveAppx, kSnipping);
    CHECK(f.option("print-screen") == snip); // not taken out either: it was not the fix's value
}

TEST_CASE("gaps: Photos → Photo Viewer registered and default for the image types; Notepad needs its file") {
    Fixture f;
    f.state.queue(Operation{OpKind::RemoveAppx, kPhotos});
    CHECK(f.option("photo-viewer") == optionIndex(settingOf(f.catalog, "photo-viewer"), "on"));
    auto list = associations(f.state);
    CHECK(list.size() == 8);
    CHECK(std::ranges::any_of(list, [](const core::AppAssociation& a) {
        return a.identifier == L".jpg" && a.progId == L"PhotoViewer.FileAssoc.Jpeg";
    }));
    // A user's own association for another type stays when the fix leaves.
    {
        auto mine = list;
        mine.push_back({L".pdf", L"AcroExch.Document.DC", L"Adobe Acrobat"});
        f.state.queue(Operation{OpKind::SetDefaultApps, L"associations", utf8::toWide(core::associationsXml(mine))});
    }
    f.state.unqueue(OpKind::RemoveAppx, kPhotos);
    CHECK(f.option("photo-viewer") == settingOf(f.catalog, "photo-viewer").defaultOption);
    list = associations(f.state);
    REQUIRE(list.size() == 1);
    CHECK(list.front().identifier == L".pdf");

    // Notepad: nothing without Windows\System32\notepad.exe in the image …
    f.state.queue(Operation{OpKind::RemoveAppx, kNotepad});
    CHECK(associations(f.state).size() == 1);
    CHECK(f.gaps.fixesOf(kNotepad).empty());
    f.state.unqueue(OpKind::RemoveAppx, kNotepad);
    // … and .txt → the classic Notepad with it.
    std::filesystem::create_directories(f.mount / L"Windows" / L"System32");
    std::ofstream(f.mount / L"Windows" / L"System32" / L"notepad.exe") << "MZ";
    f.state.queue(Operation{OpKind::RemoveAppx, kNotepad});
    CHECK(std::ranges::any_of(associations(f.state), [](const core::AppAssociation& a) {
        return a.identifier == L".txt" && a.progId == L"txtfile";
    }));
    CHECK(f.gaps.fixesOf(kNotepad).size() == 1);
    std::filesystem::remove_all(f.mount / L"Windows");
}

TEST_CASE("gaps: an app without a gap, and a preset batch, queue nothing more") {
    Fixture f;
    f.state.queue(Operation{OpKind::RemoveAppx, L"Microsoft.BingNews_4.55.62231.0_x64__8wekyb3d8bbwe"});
    CHECK(f.state.changes().size() == 1);
    // "Yerine koy" (2026-11-24): emptying the queue and the preset in one batch = one undo step.
    {
        const auto batch = f.state.batchQueue();
        f.state.unqueueIf([](const Operation&) { return true; });
        f.state.queueMany({Operation{OpKind::RemoveAppx, kSnipping}, Operation{OpKind::DisableFeature, L"Recall"}});
    }
    CHECK(f.state.changes().find(OpKind::RemoveAppx, L"Microsoft.BingNews_4.55.62231.0_x64__8wekyb3d8bbwe") == nullptr);
    CHECK(f.option("print-screen") == optionIndex(settingOf(f.catalog, "print-screen"), "copy"));
    f.state.undoQueue();
    CHECK(f.state.changes().size() == 1);
    CHECK(f.state.changes().find(OpKind::RemoveAppx, L"Microsoft.BingNews_4.55.62231.0_x64__8wekyb3d8bbwe") != nullptr);
}
